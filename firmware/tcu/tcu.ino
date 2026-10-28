/*
  1999 RAV4 Transmission Controller - hardware layer
  Vehicle: 1999 Toyota RAV4 (SXA10/SXA11), 4-speed ECT automatic, 3S-FE
  Target:  Arduino Mega 2560 (5 V I/O, 16 ADC channels, 4 UARTs)

  This file only conditions inputs, drives outputs and runs the console screen.
  Every decision is made in tcu_logic.cpp (driving) and self_test.cpp (built-in
  test), which are unit-tested on the host.

  Fail-safe contract (hardware must honour it too - see docs/02-revised-spec.md):
    * Relay coil de-energised  => factory ECU drives the real solenoids.
    * MCU reset / hang / power loss => pins go high-Z, pull-downs turn every
      driver and the relay off => car reverts to factory ECU control.
*/
#include <Arduino.h>
#include <avr/wdt.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "ds18b20.h"
#include "imu.h"
#include "oled.h"
#include "self_test.h"
#include "tcu_logic.h"

namespace pin {
constexpr uint8_t VSS = 2;           // INT4, conditioned VSS square wave
constexpr uint8_t TACH = 3;          // INT5, conditioned tach square wave
constexpr uint8_t RELAY = 6;         // relay-bank driver, HIGH = TCU in control
constexpr uint8_t S1 = 8;            // high-side driver on (via ULN2803 + P-MOSFET), solenoid S1
constexpr uint8_t S2 = 9;            // high-side driver on, shift solenoid 2
constexpr uint8_t SL = 10;           // high-side driver on, lockup solenoid
constexpr uint8_t STATUS_LED = 13;
// Active-LOW inputs: dash and shifter switches close to ground, optos pull low.
constexpr uint8_t UP = 22;           // shifter lever upshift microswitch (X7)
constexpr uint8_t DOWN = 23;         // shifter lever downshift microswitch (X7)
constexpr uint8_t ECO = 24;          // ECO switch
constexpr uint8_t SPORT = 25;        // SPORT switch (exactly one of ECO/SPORT/MANUAL on)
constexpr uint8_t MASTER = 26;       // ON/OFF switch: closed = ON (CUSTOM); open/broken wire = STOCK
constexpr uint8_t BRAKE = 27;        // opto from stop-lamp switch (STP)
constexpr uint8_t LEVER_D = 28;      // opto from park/neutral switch "D" output
constexpr uint8_t ECU_S1 = 29;       // opto from ECU S1 output (ECU side of relay)
constexpr uint8_t ECU_S2 = 30;
constexpr uint8_t ECU_SL = 31;
constexpr uint8_t TEST = 32;         // dash pod TEST button
constexpr uint8_t ATF_PROBE = 33;    // DS18B20 1-Wire data (4.7k pull-up to 5 V)
constexpr uint8_t MANUAL = 34;       // MANUAL switch: closed = the shifter lever shifts
constexpr uint8_t TPS = A0;          // ECM VTA (10k series tap)
constexpr uint8_t TPS_E2 = A1;       // ECM E2 sensor ground (10k series tap)
constexpr uint8_t ATF = A2;
constexpr uint8_t VBAT = A3;
constexpr uint8_t IS_S1 = A4;        // solenoid current sensors (ACS712 outputs)
constexpr uint8_t IS_S2 = A5;
constexpr uint8_t IS_SL = A6;
constexpr uint8_t COIL_SENSE = A7;  // relay coil supply, after the ON/OFF switch's power pole (47k/10k)
// I2C (SDA = 20, SCL = 21): dash pod OLED (0x3C) and MPU-6050 gyro (0x68).
}  // namespace pin

// USB serial carries a CSV log for calibration (docs/02-revised-spec.md §8).
constexpr bool USB_LOG_ENABLED = true;
constexpr uint32_t USB_LOG_PERIOD_MS = 100;

// Reset cause, captured before the C runtime and the Arduino core run.
uint8_t resetFlags __attribute__((section(".noinit")));
void captureResetFlags() __attribute__((naked, used, section(".init3")));
void captureResetFlags() {
  resetFlags = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

// Built-in test results survive the deliberate watchdog reset at the end of
// IBIT in .noinit RAM, guarded by a magic number and checksum.
constexpr uint16_t kWdtTestMagic = 0xB17E;
struct SavedBit {
  uint16_t magic;
  uint8_t results[tcu::T_COUNT];
  float values[tcu::T_COUNT];
  uint8_t check;
};
SavedBit savedBit __attribute__((section(".noinit")));

uint8_t savedBitChecksum() {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&savedBit);
  uint8_t sum = 0x5A;
  for (size_t i = 0; i < offsetof(SavedBit, check); i++) sum = (uint8_t)((sum << 1 | sum >> 7) ^ p[i]);
  return sum;
}

tcu::Controller controller;
tcu::SelfTest selfTest;
Oled oled;
Imu imu;
Ds18b20Bus atfBus;
tcu::Inputs lastIn;
bool unexpectedWdtReset = false;

// --------------------------------------------------------------- pulse inputs
struct PulseInput {
  volatile uint32_t lastUs = 0;
  volatile uint32_t periodUs = 0;
};
PulseInput vss, tach;

void onPulse(PulseInput& p, uint32_t minPeriodUs) {
  const uint32_t t = micros();
  const uint32_t period = t - p.lastUs;
  if (period < minPeriodUs) return;  // glitch: ignore and keep the previous edge
  p.periodUs = period;
  p.lastUs = t;
}
void isrVss() { onPulse(vss, cfg::VSS_MIN_PERIOD_US); }
void isrTach() { onPulse(tach, cfg::TACH_MIN_PERIOD_US); }

// Pulse frequency in Hz. When pulses stop, decays as 1/elapsed instead of
// holding the last value, so a lost signal shows up as a collapse.
float pulseHz(PulseInput& p, uint32_t timeoutUs) {
  noInterrupts();
  const uint32_t last = p.lastUs;
  const uint32_t period = p.periodUs;
  interrupts();
  const uint32_t since = micros() - last;
  if (period == 0 || since > timeoutUs) return 0.0f;
  return 1e6f / (float)(since > period ? since : period);
}

// --------------------------------------------------------------- analog inputs
struct Ema {
  float y = 0.0f;
  bool init = false;
  float update(float x, float alpha) {
    y = init ? y + alpha * (x - y) : x;
    init = true;
    return y;
  }
};
Ema tpsF, atfF, vbatF, coilF, speedF, rpmF, solF[3];

uint16_t readRaw(uint8_t p) {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += analogRead(p);
  return sum / 4;
}
float rawToVolts(uint16_t raw) { return raw * (cfg::ADC_VREF / 1023.0f); }

// --------------------------------------------------------------- ATF probe
// One 12-bit conversion per second: start it, read it 800 ms later. A few
// bad reads in a row (unplugged, shorted, noise) = sensor fault.
struct {
  float celsius = 0.0f;
  bool valid = false;
  bool have = false;
  uint8_t misses = 0;
  bool converting = false;
  uint32_t startMs = 0;
} atfProbe;

void atfMiss() {
  if (++atfProbe.misses >= cfg::DS18B20_MAX_MISSES) atfProbe.valid = false;
}

void serviceAtfProbe(uint32_t now) {
  if (cfg::ATF_SENSOR != cfg::ATF_DS18B20) return;
  if (!atfProbe.converting) {
    if ((uint32_t)(now - atfProbe.startMs) < cfg::DS18B20_PERIOD_MS) return;
    atfProbe.startMs = now;
    if (!atfBus.reset()) return atfMiss();
    atfBus.writeByte(0xCC);  // skip ROM: one probe on the bus
    atfBus.writeByte(0x44);  // convert T
    atfProbe.converting = true;
    return;
  }
  if ((uint32_t)(now - atfProbe.startMs) < 800) return;  // 12-bit conversion: 750 ms
  atfProbe.converting = false;
  if (!atfBus.reset()) return atfMiss();
  atfBus.writeByte(0xCC);
  atfBus.writeByte(0xBE);  // read scratchpad
  uint8_t sp[9];
  for (uint8_t i = 0; i < 9; i++) sp[i] = atfBus.readByte();
  float c;
  const ds18b20::Result r = ds18b20::decode(sp, c);
  // 85.0 C is also the chip's power-on value. Accept it only when it agrees
  // with the previous reading (85 C is a perfectly normal ATF temperature).
  const bool ok = r == ds18b20::OK ||
                  (r == ds18b20::POWER_ON_VALUE && atfProbe.have && fabs(atfProbe.celsius - c) < 5.0f);
  if (!ok) return atfMiss();
  atfProbe.celsius = c;
  atfProbe.valid = atfProbe.have = true;
  atfProbe.misses = 0;
}

// --------------------------------------------------------------- gyro
// Yaw rate x speed = lateral acceleration. The gyro's zero offset drifts, so
// it is re-learned whenever the car is stopped; until it has been learned
// (about 3 s stationary after power-up) lateral g is reported invalid and
// corner hold stays off.
struct {
  float biasDps = 0.0f;
  uint16_t stillSamples = 0;
  uint8_t misses = 0;
  float latG = 0.0f;
  uint32_t lastMs = 0;
} gyro;

void serviceGyro(uint32_t now) {
  if (!cfg::IMU_INSTALLED || (uint32_t)(now - gyro.lastMs) < 20) return;
  gyro.lastMs = now;
  float dps;
  if (!imu.readYawDps(dps)) {
    if (gyro.misses < 255) gyro.misses++;
    return;
  }
  gyro.misses = 0;
  if (lastIn.speedMph < 0.6f && fabs(dps - gyro.biasDps) < 10.0f) {
    gyro.biasDps = gyro.stillSamples ? gyro.biasDps + 0.02f * (dps - gyro.biasDps) : dps;
    if (gyro.stillSamples < 1000) gyro.stillSamples++;
  }
  const float yawRadS = (dps - gyro.biasDps) * (3.14159265f / 180.0f);
  gyro.latG = (lastIn.speedMph * 0.44704f) * yawRadS / 9.81f;  // mph -> m/s
}

// --------------------------------------------------------------- switches
tcu::Debouncer dbUp, dbDown, dbEco, dbSport, dbManual, dbMaster, dbBrake, dbLeverD, dbEcuS1, dbEcuS2, dbEcuSL, dbTest;

inline bool activeLow(uint8_t p) { return digitalRead(p) == LOW; }

void sampleSwitches(uint32_t now) {
  dbUp.update(activeLow(pin::UP), now, cfg::DEBOUNCE_SWITCH_MS);
  dbDown.update(activeLow(pin::DOWN), now, cfg::DEBOUNCE_SWITCH_MS);
  dbEco.update(activeLow(pin::ECO), now, cfg::DEBOUNCE_SWITCH_MS);
  dbSport.update(activeLow(pin::SPORT), now, cfg::DEBOUNCE_SWITCH_MS);
  dbManual.update(activeLow(pin::MANUAL), now, cfg::DEBOUNCE_SWITCH_MS);
  dbMaster.update(activeLow(pin::MASTER), now, cfg::DEBOUNCE_SWITCH_MS);
  dbBrake.update(activeLow(pin::BRAKE), now, cfg::DEBOUNCE_OPTO_MS);
  dbLeverD.update(activeLow(pin::LEVER_D), now, cfg::DEBOUNCE_OPTO_MS);
  dbEcuS1.update(activeLow(pin::ECU_S1), now, cfg::DEBOUNCE_OPTO_MS);
  dbEcuS2.update(activeLow(pin::ECU_S2), now, cfg::DEBOUNCE_OPTO_MS);
  dbEcuSL.update(activeLow(pin::ECU_SL), now, cfg::DEBOUNCE_OPTO_MS);
  dbTest.update(activeLow(pin::TEST), now, cfg::DEBOUNCE_SWITCH_MS);
}

tcu::Inputs readInputs(uint32_t now) {
  tcu::Inputs in;
  in.nowMs = now;
  in.up = cfg::SWAP_SHIFT_INPUTS ? dbDown.state() : dbUp.state();
  in.down = cfg::SWAP_SHIFT_INPUTS ? dbUp.state() : dbDown.state();
  in.eco = dbEco.state();
  in.sport = dbSport.state();
  in.manual = dbManual.state();
  // Until the car is calibrated the ON/OFF switch is ignored (STOCK only).
  in.switchOn = dbMaster.state();
  in.customRequested = cfg::CALIBRATED && in.switchOn;
  in.brake = dbBrake.state();
  in.leverInD = dbLeverD.state();
  in.ecuSenseValid = cfg::ECU_SENSE_INSTALLED;
  in.ecuS1 = dbEcuS1.state();
  in.ecuS2 = dbEcuS2.state();
  in.ecuSL = dbEcuSL.state();

  // Speed is not low-pass filtered heavily: the VSS-loss check needs to see a
  // collapse, and the over-rev guard must not lag a real slowdown.
  in.speedMph = speedF.update(pulseHz(vss, cfg::VSS_TIMEOUT_US) * 3600.0f / cfg::VSS_PULSES_PER_MILE, 0.5f);  // pulses/s -> mph
  in.engineRpm = rpmF.update(pulseHz(tach, cfg::TACH_TIMEOUT_US) * 60.0f / cfg::TACH_PULSES_PER_REV, 0.5f);
  // A dead tach wire while driving reads 0 rpm: treat as invalid so the logic
  // falls back to speed x ratio instead of trusting it.
  in.rpmValid = cfg::TACH_INSTALLED && !(in.engineRpm < 300.0f && in.speedMph > 6.0f);

  // VTA relative to the ECM's sensor ground E2 (differential, no ground loop).
  const float e2V = cfg::TPS_E2_SENSE ? rawToVolts(readRaw(pin::TPS_E2)) : 0.0f;
  const float tpsV = tpsF.update(rawToVolts(readRaw(pin::TPS)) - e2V, 0.4f);
  in.tpsValid = tpsV >= cfg::TPS_VALID_MIN_V && tpsV <= cfg::TPS_VALID_MAX_V;
  float tps = (tpsV - cfg::TPS_CLOSED_V) / (cfg::TPS_WOT_V - cfg::TPS_CLOSED_V) * 100.0f;
  in.tpsPct = tps < 0.0f ? 0.0f : (tps > 100.0f ? 100.0f : tps);

  if (cfg::ATF_SENSOR == cfg::ATF_DS18B20) {
    in.atfValid = atfProbe.valid;
    in.atfC = atfProbe.celsius;
  } else {
    const uint16_t atfRaw = readRaw(pin::ATF);
    in.atfValid = atfRaw > cfg::ATF_ADC_SHORT && atfRaw < cfg::ATF_ADC_OPEN;
  }
  if (cfg::ATF_SENSOR == cfg::ATF_NTC && in.atfValid) {
    const float v = rawToVolts(readRaw(pin::ATF));
    const float r = cfg::ATF_PULLUP_OHMS * v / (cfg::ADC_VREF - v);
    const float kelvin = 1.0f / (1.0f / 298.15f + log(r / cfg::ATF_R25_OHMS) / cfg::ATF_BETA);
    in.atfC = atfF.update(kelvin - 273.15f, 0.05f);
  }

  in.batteryV = vbatF.update(rawToVolts(readRaw(pin::VBAT)) * cfg::VBAT_DIVIDER, 0.1f);
  in.coilSenseValid = cfg::COIL_SENSE_INSTALLED;
  in.coilPowered = coilF.update(rawToVolts(readRaw(pin::COIL_SENSE)) * cfg::VBAT_DIVIDER, 0.3f) > cfg::COIL_ON_V;
  // A relay contact found stuck closed (key-on check or IBIT) blocks take-over
  // until a later check passes.
  in.relayStuck = selfTest.result(tcu::T_RELAY_RELEASE) == tcu::TR_FAIL;

  in.imuPresent = imu.present();
  in.latValid = cfg::IMU_INSTALLED && imu.present() && gyro.stillSamples >= 150 && gyro.misses < 5;
  in.latG = gyro.latG;

  in.solSenseValid = cfg::SOLENOID_SENSE_INSTALLED;
  const uint8_t isPins[3] = {pin::IS_S1, pin::IS_S2, pin::IS_SL};
  const uint8_t drvPins[3] = {pin::S1, pin::S2, pin::SL};
  static float isZero[3] = {cfg::IS_ZERO_V, cfg::IS_ZERO_V, cfg::IS_ZERO_V};
  // Each sensor's zero is tracked only when no current can flow through it:
  // relays released (so even a shorted driver's output goes nowhere) AND its
  // driver off (so a welded relay can't be mistaken for the zero).
  const bool relaysReleased = digitalRead(pin::RELAY) == LOW;
  for (uint8_t i = 0; i < 3; i++) {
    const float v = rawToVolts(readRaw(isPins[i]));
    if (relaysReleased && digitalRead(drvPins[i]) == LOW &&
        fabs(v - cfg::IS_ZERO_V) < cfg::IS_ZERO_LEARN_WINDOW_V) {
      isZero[i] += (v - isZero[i]) * 0.02f;
    }
    in.solAmps[i] = solF[i].update((v - isZero[i]) * cfg::IS_AMPS_PER_VOLT, 0.5f);
  }
  return in;
}

// --------------------------------------------------------------- outputs
void applyOutputs(const tcu::Outputs& o) {
  if (o.relayCustom) {
    // Drivers first (already synced to the ECU's state), then take over.
    digitalWrite(pin::S1, o.s1 ? HIGH : LOW);
    digitalWrite(pin::S2, o.s2 ? HIGH : LOW);
    digitalWrite(pin::SL, o.sl ? HIGH : LOW);
    digitalWrite(pin::RELAY, HIGH);
  } else {
    // Hand back first, then switch our drivers off.
    digitalWrite(pin::RELAY, LOW);
    digitalWrite(pin::S1, LOW);
    digitalWrite(pin::S2, LOW);
    digitalWrite(pin::SL, LOW);
  }
}

// Self-test needs drivers on with the relay off (relay-release check), which
// applyOutputs() deliberately never does, so it writes the pins directly.
void applyTestOutputs(const tcu::TestOutputs& t) {
  digitalWrite(pin::RELAY, t.relay ? HIGH : LOW);
  digitalWrite(pin::S1, t.s1 ? HIGH : LOW);
  digitalWrite(pin::S2, t.s2 ? HIGH : LOW);
  digitalWrite(pin::SL, t.sl ? HIGH : LOW);
  digitalWrite(pin::STATUS_LED, t.lamps ? HIGH : LOW);
}

void updateLeds(const tcu::Outputs& o, uint32_t now) {
  bool on;
  if (o.faults && o.armed) on = (now / 125) & 1;                        // fast blink: fault
  else if (o.control != tcu::CTRL_STOCK) on = true;                     // solid: TCU active
  else if (o.armed && o.event == tcu::EV_NEED_SYNC) on = (now / 500) & 1;  // slow: stop to engage
  else on = false;
  digitalWrite(pin::STATUS_LED, on ? HIGH : LOW);
}

// --------------------------------------------------------------- text
// Everything on the screen is plain English: no codes to look up.
const char* modeWord(const tcu::Outputs& o) {
  if (o.control == tcu::CTRL_STOCK) return "STOCK";
  if (o.control == tcu::CTRL_MANUAL) return "MANUAL";
  return o.mode == tcu::MODE_ECO ? "ECO" : "SPORT";
}

int32_t toDisplayTemp(float c) {
  const float t = cfg::TEMP_IN_F ? c * 1.8f + 32.0f : c;
  return (int32_t)(t + (t < 0 ? -0.5f : 0.5f));
}
const char* tempUnit() { return cfg::TEMP_IN_F ? "F" : "C"; }

// Small fixed-width text builder (AVR printf has no %f).
struct Text {
  char buf[Oled::LINE_CHARS + 1];
  uint8_t len = 0;
  Text() { buf[0] = '\0'; }
  Text& s(const char* t) {
    while (*t && len < Oled::LINE_CHARS) buf[len++] = *t++;
    buf[len] = '\0';
    return *this;
  }
  Text& n(int32_t v) {
    char tmp[12];
    ltoa(v, tmp, 10);
    return s(tmp);
  }
  Text& fixed(float v, uint8_t decimals) {
    const int32_t scale = decimals == 2 ? 100 : 10;
    int32_t t = (int32_t)(v * scale + (v < 0 ? -0.5f : 0.5f));
    if (t < 0) {
      s("-");
      t = -t;
    }
    n(t / scale).s(".");
    if (decimals == 2 && t % scale < 10) s("0");
    return n(t % scale);
  }
  Text& pad(uint8_t col) {
    while (len < col && len < Oled::LINE_CHARS) buf[len++] = ' ';
    buf[len] = '\0';
    return *this;
  }
};

const char* inhibitText(tcu::TestInhibit i) {
  switch (i) {
    case tcu::INH_MASTER: return "ON/OFF IS OFF";
    case tcu::INH_NO_COIL: return "NO RELAY POWER";
    case tcu::INH_LEVER_D: return "LEVER IS IN D";
    case tcu::INH_MOVING: return "THE CAR IS MOVING";
    case tcu::INH_ENGINE: return "ENGINE IS RUNNING";  // or a charger connected
    case tcu::INH_SUPPLY: return "BATTERY IS TOO LOW";
    default: return "";
  }
}

// OK = passed, BAD = failed, CHECK = done, but you have to check it yourself
// (did the screen flash? a solenoid without current sensing), SKIP = skipped.
const char* resultText(tcu::TestResult r) {
  switch (r) {
    case tcu::TR_PASS: return "OK";
    case tcu::TR_FAIL: return "BAD";
    case tcu::TR_CHECK: return "CHECK";
    case tcu::TR_SKIP: return "SKIP";
    default: return "--";
  }
}

const char* promptText(tcu::TestPrompt p) {
  switch (p) {
    case tcu::PR_UP: return "UPSHIFT THE LEVER";
    case tcu::PR_DOWN: return "DOWNSHIFT THE LEVER";
    case tcu::PR_ECO: return "TURN ON ONLY ECO";
    case tcu::PR_SPORT: return "TURN ON ONLY SPORT";
    case tcu::PR_MANUAL: return "TURN ON ONLY MANUAL";
    case tcu::PR_BRAKE: return "PRESS THE BRAKE";
    case tcu::PR_ONOFF_OFF: return "TURN ON/OFF OFF";
    default: return "";
  }
}

// "SOLENOID 2 BAD  0.01A" or "MANUAL SWITCH   OK"
Text itemLine(tcu::TestId id) {
  Text t;
  const tcu::TestResult r = selfTest.result(id);
  const float v = selfTest.value(id);
  bool hasValue = r != tcu::TR_NONE && r != tcu::TR_SKIP;
  switch (id) {
    case tcu::T_SUPPLY: case tcu::T_TPS: case tcu::T_ATF: break;
    case tcu::T_SOL_S1: case tcu::T_SOL_S2: case tcu::T_SOL_SL: case tcu::T_RELAY_RELEASE: case tcu::T_ONOFF_CUT:
      hasValue = hasValue && r != tcu::TR_CHECK;
      break;
    default: hasValue = false; break;
  }
  t.s(tcu::testName(id)).pad(hasValue ? 11 : 16).s(resultText(r));
  if (!hasValue) return t;
  t.pad(16);
  switch (id) {
    case tcu::T_SUPPLY: t.fixed(v, 1).s("V"); break;
    case tcu::T_TPS: t.n((int32_t)v).s("%"); break;
    case tcu::T_ATF: t.n(toDisplayTemp(v)).s(tempUnit()); break;
    default: t.fixed(v, 2).s("A"); break;
  }
  return t;
}

// The first fault, in words (at most 14 characters).
const char* faultWords(uint16_t faults) {
  if (faults & tcu::FAULT_SUPPLY) return "BATTERY VOLTS";
  if (faults & tcu::FAULT_VSS) return "NO SPEED SIGNL";
  if (faults & tcu::FAULT_TPS) return "PEDAL SENSOR";
  if (faults & tcu::FAULT_RATIO) return "WRONG GEAR";
  if (faults & tcu::FAULT_SOLENOID) return "SOLENOID WIRE";
  if (faults & tcu::FAULT_ONOFF) return "ON/OFF SWITCH";
  if (faults & tcu::FAULT_RELAY) return "RELAY STUCK";
  if (faults & tcu::FAULT_LEVER) return "LEVER SENSOR";
  return "";
}

uint32_t inhibitShownUntil = 0;  // show a refused self-test for a few seconds

// What the MAIN page says: a headline (double size, 10 chars, under the
// gear) and up to four short lines (14 chars, beside the gear). Highest
// priority first; returns false when there's nothing to report.
struct Message {
  Text head;
  Text line[4];
};

bool alertMessage(const tcu::Outputs& o, uint32_t now, Message& m) {
  static uint8_t seenSeq = 0;
  static uint32_t eventAt = 0;
  if (o.eventSeq != seenSeq) {
    seenSeq = o.eventSeq;
    eventAt = now;
  }
  const bool switchOn = dbMaster.state();
  auto say = [&](const char* h, const char* a, const char* b = "", const char* c = "", const char* d = "") {
    m.head.s(h);
    m.line[0].s(a);
    m.line[1].s(b);
    m.line[2].s(c);
    m.line[3].s(d);
    return true;
  };
  if (o.faults & tcu::FAULT_RELAY) return say("PROBLEM", "RELAY STUCK", "STOCK SHIFTS.", "HOLD TEST TO", "RE-CHECK.");
  if (o.faults & tcu::FAULT_LEVER) return say("PROBLEM", "LEVER SENSOR", "STUCK ON D.", "STOCK SHIFTS.", "CHECK D WIRE.");
  if (o.faults & tcu::FAULT_ONOFF) return say("PROBLEM", "ON/OFF SWITCH:", "ITS 2 HALVES", "DISAGREE.", "CHECK WIRING.");
  if (o.faults) return say("PROBLEM", faultWords(o.faults), "STOCK SHIFTS.", "ON/OFF OFF+ON", "TO TRY AGAIN.");
  if (switchOn && o.modeSelect == tcu::SEL_SEVERAL)
    return say("MODE ERROR", "TWO MODES ON.", "TURN ON ONLY", "ECO, SPORT OR", "MANUAL.");
  if (switchOn && o.modeSelect == tcu::SEL_NONE)
    return say("PICK MODE", "NO MODE ON.", "TURN ON ONE:", "ECO, SPORT OR", "MANUAL.");
  if (switchOn && !cfg::CALIBRATED) return say("NOT READY", "NOT CALIBRATED", "STOCK SHIFTS", "UNTIL IT IS:", "BUILD BOOK 13");
  if (o.warnings & tcu::WARN_ATF_CRITICAL) {
    Text t;
    t.s("OVER ").n(toDisplayTemp(cfg::ATF_CRITICAL_C)).s(tempUnit()).s("!");
    return say("TRANS HOT!", t.buf, "PULL OVER AND", "LET IT COOL.");
  }
  if (o.warnings & tcu::WARN_ATF_HOT) {
    Text t;
    t.s("OVER ").n(toDisplayTemp(cfg::ATF_HOT_C)).s(tempUnit()).s(".");
    return say("TRANS HOT", t.buf, "EASE OFF.");
  }
  if ((uint32_t)(now - eventAt) < 4000 || o.event == tcu::EV_NEED_SYNC) {
    switch (o.event) {
      case tcu::EV_DENY_OVERREV: return say("NO SHIFT", "TOO FAST FOR", "THAT GEAR: THE", "ENGINE WOULD", "OVER-REV.");
      case tcu::EV_DENY_TOO_SLOW: return say("NO SHIFT", "TOO SLOW FOR", "THAT GEAR.");
      case tcu::EV_DENY_BUSY: return say("NO SHIFT", "STILL DOING", "THE LAST ONE.", "TRY AGAIN.");
      case tcu::EV_DENY_NOT_MANUAL: return say("MANUAL OFF", "FLIP MANUAL ON", "TO SHIFT WITH", "THE LEVER.");
      case tcu::EV_AUTO_DOWNSHIFT: return say("DOWNSHIFT", "AUTOMATIC:", "SLOWING DOWN.");
      case tcu::EV_REV_LIMIT_UPSHIFT: return say("UPSHIFT", "AUTOMATIC:", "ENGINE AT", "REDLINE.");
      case tcu::EV_NEED_SYNC: return say("STOP FIRST", "TCU TAKES", "OVER WHEN", "STOPPED.");
      case tcu::EV_CUSTOM: return say("TCU ON", "TCU IS DOING", "THE SHIFTING.");
      case tcu::EV_STOCK: return say("TCU OFF", "STOCK", "SHIFTING.");
      case tcu::EV_MANUAL: return say("MANUAL", "YOU SHIFT WITH", "THE LEVER.");
      case tcu::EV_AUTO_RESUMED: return say("AUTO", "TCU SHIFTS", "FOR YOU.");
      default: break;
    }
  }
  if (switchOn && !o.armed) return say("WAITING", "ON/OFF WAS ON", "AT KEY-ON.", "FLIP IT OFF,", "THEN ON.");
  if (o.warnings & tcu::WARN_ATF_SENSOR) return say("NO TEMP", "TEMP PROBE", "NOT WORKING.", "CHECK THE PLUG");
  if (o.warnings & tcu::WARN_TACH) return say("NO RPM", "ENGINE SPEED", "SIGNAL LOST.", "STILL SAFE.", "CHECK TACH.");
  if (o.warnings & tcu::WARN_BUTTON_STUCK) return say("SHIFTER?", "A LEVER SWITCH", "IS STUCK ON.", "CHECK THE", "SHIFTER.");
  return false;
}

// In STOCK, log every shift and lockup change the factory ECU makes, with
// the speed and throttle it happened at. Driving normally with the switch
// OFF then gives the factory shift map to compare NORMAL against (spec §8).
void logEcuShifts(const tcu::Outputs& o) {
  static uint8_t prevGear = 0;
  static bool prevLockup = false;
  if (o.control != tcu::CTRL_STOCK) {
    prevGear = 0;
    return;
  }
  const bool shifted = prevGear && o.gear && o.gear != prevGear;
  const bool lockupChanged = o.gear && o.lockup != prevLockup;
  if (USB_LOG_ENABLED && (shifted || lockupChanged) && Serial.availableForWrite() >= 56) {
    if (shifted) {
      Serial.print(F("# ECU shift "));
      Serial.print(prevGear);
      Serial.print('>');
      Serial.print(o.gear);
    } else {
      Serial.print(o.lockup ? F("# ECU lockup ON") : F("# ECU lockup OFF"));
    }
    Serial.print(F(" at "));
    Serial.print((int16_t)(lastIn.speedMph + 0.5f));
    Serial.print(F(" mph, TPS "));
    Serial.print((int16_t)(lastIn.tpsPct + 0.5f));
    Serial.print(F(" %, "));
    Serial.print((int32_t)lastIn.engineRpm);
    Serial.println(F(" rpm"));
  }
  prevGear = o.gear;
  prevLockup = o.lockup;
}

void serviceUsbLog(uint32_t now) {
  static uint32_t last = 0;
  if (!USB_LOG_ENABLED || (uint32_t)(now - last) < USB_LOG_PERIOD_MS) return;
  if (Serial.availableForWrite() < 60) return;  // never block on a slow host
  last = now;
  const tcu::Outputs& o = controller.outputs();
  // ms,ctrl,mode,gear,lockup,mph,rpm,rpm_per_mph,tps,atf,volts,faults,warnings
  Serial.print(now);
  Serial.print(',');
  Serial.print(o.control);
  Serial.print(',');
  Serial.print(o.mode);
  Serial.print(',');
  Serial.print(o.gear);
  Serial.print(',');
  Serial.print(o.lockup);
  Serial.print(',');
  Serial.print(lastIn.speedMph, 1);
  Serial.print(',');
  Serial.print((int32_t)lastIn.engineRpm);
  Serial.print(',');
  Serial.print(lastIn.speedMph > 5.0f ? lastIn.engineRpm / lastIn.speedMph : 0.0f, 1);
  Serial.print(',');
  Serial.print((int16_t)lastIn.tpsPct);
  Serial.print(',');
  Serial.print((int16_t)lastIn.atfC);
  Serial.print(',');
  Serial.print(lastIn.batteryV, 1);
  Serial.print(',');
  Serial.print(o.faults);
  Serial.print(',');
  Serial.println(o.warnings);
}

// -------------------------------------------------------------- dash pod OLED
enum Page : uint8_t { PG_MAIN, PG_SENSORS, PG_TEST, PG_COUNT };
uint8_t page = PG_MAIN;

// MAIN page: the gear as one giant digit (18 mm tall on a 2.42" screen),
// the mode beside it (ECO / SPORT / MANUAL / STOCK), and one big line under
// both: the trans temperature, or what needs your attention. The small lines
// beside the gear explain the big one.
void renderMain(const tcu::Outputs& o, uint32_t now) {
  oled.setHero(o.gear ? (char)('0' + o.gear) : '-');
  oled.setBigLine(0, modeWord(o));
  Message m;
  if (!alertMessage(o, now, m)) {
    m.head.s("TRANS ");
    if (lastIn.atfValid) m.head.n(toDisplayTemp(lastIn.atfC)).s(tempUnit());
    else m.head.s("--");
    if (!dbMaster.state()) {
      m.line[0].s("TCU IS OFF:");
      m.line[1].s("STOCK SHIFTING");
    } else if (o.control == tcu::CTRL_STOCK && !lastIn.leverInD) {
      m.line[0].s("LEVER NOT IN D");
      m.line[1].s("STOCK SHIFTING");
    } else if (o.cornerHold) {
      m.line[0].s("CORNER:");
      m.line[1].s("HOLDING GEAR");
    } else if (o.gearHold) {
      m.line[0].s("HOLDING GEAR");
    }
    if (o.warnings & tcu::WARN_WDT_RESET) {
      m.line[2].s("TCU RESTARTED");
    }
    if (selfTest.hasRun() && !selfTest.active() && selfTest.summary().fail) {
      m.line[2].s("SELF-TEST BAD:");
      m.line[3].s("TAP TEST: INFO");
    }
  }
  for (uint8_t i = 0; i < 4; i++) oled.setLine(2 + i, m.line[i].buf);
  oled.setBigLine(6, m.head.buf);
}

void renderSensors() {
  oled.setHero('\0');
  oled.setLine(0, "SENSORS   (TAP: NEXT)");
  Text r[7];
  r[0].s("SPEED").pad(12).n((int32_t)(lastIn.speedMph + 0.5f)).s(" MPH");
  r[1].s("ENGINE").pad(12);
  if (lastIn.rpmValid) r[1].n((int32_t)lastIn.engineRpm).s(" RPM");
  else r[1].s("--");
  r[2].s("GAS PEDAL").pad(12);
  if (lastIn.tpsValid) r[2].n((int32_t)(lastIn.tpsPct + 0.5f)).s(" %");
  else r[2].s("NOT WORKING");
  r[3].s("TRANS TEMP").pad(12);
  if (lastIn.atfValid) r[3].n(toDisplayTemp(lastIn.atfC)).s(" ").s(tempUnit());
  else r[3].s("NOT WORKING");
  r[4].s("BATTERY").pad(12).fixed(lastIn.batteryV, 1).s(" V");
  r[5].s("SIDE FORCE").pad(12);
  if (lastIn.latValid) r[5].fixed(lastIn.latG < 0 ? -lastIn.latG : lastIn.latG, 2).s(" G");
  else r[5].s("--");
  r[6].s("BRAKE ").s(lastIn.brake ? "ON " : "OFF").s("  ").s(lastIn.leverInD ? "LEVER IN D" : "NOT IN D");
  for (uint8_t i = 0; i < 7; i++) oled.setLine(1 + i, r[i].buf);
}

void renderTest(uint32_t now) {
  oled.setHero('\0');
  const tcu::TestSummary sum = selfTest.summary();
  const bool refused = (int32_t)(inhibitShownUntil - now) > 0;
  if (refused) oled.setBig("CAN'T TEST");
  else if (selfTest.active()) oled.setBig("TESTING");
  else if (selfTest.aborted()) oled.setBig("STOPPED");
  else if (!selfTest.hasRun()) oled.setBig("SELF-TEST");
  else oled.setBig(sum.fail ? "PROBLEM" : "ALL OK");
  uint8_t row = 2;
  auto line = [&](const char* t) {
    if (row <= 7) oled.setLine(row++, t);
  };

  if (refused) {
    line("NOT NOW, BECAUSE:");
    line(inhibitText(selfTest.inhibit()));
    line("");
    line("IT NEEDS: PARKED,");
    line("ENGINE OFF, LEVER IN");
    line("P, ON/OFF ON");
    return;
  }
  if (selfTest.active()) {
    const tcu::TestPrompt pr = selfTest.prompt();
    if (pr) {
      Text t;
      line(promptText(pr));
      t.n((int32_t)((selfTest.promptRemainingMs(now) + 999) / 1000)).s("s LEFT. TEST = SKIP");
      line(t.buf);
    } else if (selfTest.phase() == tcu::PH_LAMPS) {
      line("THE SCREEN SHOULD");
      line("FLASH NOW");
    } else {
      Text t;
      t.s("CHECKING ").s(tcu::testName(selfTest.currentTest()));
      line(t.buf);
      line("");
    }
  } else if (selfTest.aborted()) {
    line("STOPPED BECAUSE");
    line(inhibitText(selfTest.inhibit()));
  } else if (selfTest.hasRun()) {
    Text t;
    t.n(sum.pass).s(" OK, ").n(sum.fail).s(" BAD, ").n(sum.check).s(" CHECK");
    line(t.buf);
  } else {
    line("HOLD TEST 2 SECONDS");
    line("TO RUN IT");
  }
  // Remaining rows: what failed first, then what needs you to look.
  uint8_t ids[tcu::T_COUNT];
  uint8_t count = 0;
  const tcu::TestResult order[] = {tcu::TR_FAIL, tcu::TR_CHECK};
  for (tcu::TestResult want : order) {
    for (uint8_t i = 0; i < tcu::T_COUNT; i++) {
      if (selfTest.result((tcu::TestId)i) == want) ids[count++] = i;
    }
  }
  const uint8_t rowsLeft = 8 - row;
  const uint8_t shown = count <= rowsLeft ? count : rowsLeft - 1;  // keep a row for "+n more"
  for (uint8_t k = 0; k < shown; k++) line(itemLine((tcu::TestId)ids[k]).buf);
  if (shown < count) {
    Text t;
    t.s("+").n(count - shown).s(" MORE ON LAPTOP LOG");
    line(t.buf);
  }
  if (!selfTest.active() && !selfTest.aborted() && selfTest.hasRun() && !count) line("EVERY CHECK PASSED");
  if (!selfTest.active()) line(selfTest.hasRun() ? "HOLD TEST 2s: AGAIN" : "");
  while (row <= 7) line("");
}

void serviceOled(uint32_t now) {
  if (!cfg::OLED_INSTALLED) return;
  static uint32_t lastRender = 0;
  if ((uint32_t)(now - lastRender) >= 100) {
    lastRender = now;
    if (page == PG_MAIN) renderMain(controller.outputs(), now);
    else if (page == PG_SENSORS) renderSensors();
    else renderTest(now);
  }
  oled.service(now);
}

void logBitTable(const char* title) {
  if (!USB_LOG_ENABLED) return;
  Serial.print(F("# "));
  Serial.println(title);
  for (uint8_t i = 0; i < tcu::T_COUNT; i++) {
    Serial.print(F("#   "));
    Serial.println(itemLine((tcu::TestId)i).buf);
  }
}

// --------------------------------------------------------------- TEST button
// Short press: next OLED page. Hold 2 s: start IBIT. During IBIT: skip prompt.
bool testEdgePending = false;

void handleTestButton(uint32_t now) {
  static bool prev = false, longFired = false;
  static uint32_t since = 0;
  const bool down = dbTest.state();
  if (down && !prev) {
    since = now;
    longFired = false;
    if (selfTest.active()) testEdgePending = true;
  }
  if (down && !longFired && !selfTest.active() && (uint32_t)(now - since) >= cfg::TEST_LONG_PRESS_MS) {
    longFired = true;
    page = PG_TEST;
    tcu::Inputs tin = lastIn;
    if (selfTest.start(tin)) {
      inhibitShownUntil = now;
      if (USB_LOG_ENABLED) Serial.println(F("# IBIT start"));
    } else {
      inhibitShownUntil = now + 3000;
    }
  }
  if (!down && prev && !longFired && !selfTest.active()) page = (page + 1) % PG_COUNT;
  prev = down;
}

// Last IBIT step: save results, then stop kicking the watchdog. A working
// watchdog resets the board within 250 ms; setup() restores the results and
// records the pass. If the screen stays on "WATCHDOG TEST", it failed.
void runWatchdogTest() {
  applyTestOutputs(tcu::TestOutputs());
  savedBit.magic = kWdtTestMagic;
  memcpy(savedBit.results, selfTest.results(), sizeof(savedBit.results));
  memcpy(savedBit.values, selfTest.values(), sizeof(savedBit.values));
  savedBit.check = savedBitChecksum();
  if (USB_LOG_ENABLED) {
    Serial.println(F("# IBIT watchdog test: expecting a reset"));
    Serial.flush();
  }
  if (cfg::OLED_INSTALLED) {
    oled.setHero('\0');
    oled.setBig("RESTART");
    oled.setLine(2, "SAFETY RESET TEST:");
    oled.setLine(3, "THE TCU SHOULD");
    oled.setLine(4, "RESTART NOW");
    for (uint8_t i = 0; i < 40 && oled.present(); i++) {  // flush the changed pages
      wdt_reset();
      oled.service(millis());
    }
  }
  wdt_enable(WDTO_250MS);
  for (;;) {
  }
}

// --------------------------------------------------------------- setup / loop
void setup() {
  // Outputs to the safe state before anything else (external pull-downs hold
  // them off until this point).
  digitalWrite(pin::RELAY, LOW);
  digitalWrite(pin::S1, LOW);
  digitalWrite(pin::S2, LOW);
  digitalWrite(pin::SL, LOW);
  pinMode(pin::RELAY, OUTPUT);
  pinMode(pin::S1, OUTPUT);
  pinMode(pin::S2, OUTPUT);
  pinMode(pin::SL, OUTPUT);
  pinMode(pin::STATUS_LED, OUTPUT);

  // External 2.2k pull-ups do the real work; internal ones are a backstop.
  const uint8_t inputs[] = {pin::UP,     pin::DOWN,    pin::ECO,    pin::SPORT,  pin::MASTER, pin::BRAKE,
                            pin::LEVER_D, pin::ECU_S1, pin::ECU_S2, pin::ECU_SL, pin::TEST,     pin::MANUAL};
  for (uint8_t p : inputs) pinMode(p, INPUT_PULLUP);
  // Start every switch filter from the real level (after the input RC filters
  // charge), not from "off": otherwise a switch already ON at power-up would
  // look like a fresh OFF -> ON flip.
  delay(10);
  tcu::Debouncer* const dbs[] = {&dbUp,     &dbDown,   &dbEco,    &dbSport,  &dbManual, &dbMaster,
                                 &dbBrake,  &dbLeverD, &dbEcuS1,  &dbEcuS2,  &dbEcuSL,  &dbTest};
  const uint8_t dbPins[] = {pin::UP,    pin::DOWN,   pin::ECO,    pin::SPORT,  pin::MANUAL, pin::MASTER,
                            pin::BRAKE, pin::LEVER_D, pin::ECU_S1, pin::ECU_S2, pin::ECU_SL, pin::TEST};
  for (uint8_t i = 0; i < sizeof(dbPins); i++) *dbs[i] = tcu::Debouncer(activeLow(dbPins[i]));
  pinMode(pin::VSS, INPUT);
  pinMode(pin::TACH, INPUT);

  Serial.begin(115200);

  attachInterrupt(digitalPinToInterrupt(pin::VSS), isrVss, FALLING);
  attachInterrupt(digitalPinToInterrupt(pin::TACH), isrTach, FALLING);

  // Was this reset the IBIT watchdog test, or a real hang?
  const bool wdtReset = resetFlags & _BV(WDRF);
  const bool bitSaved = savedBit.magic == kWdtTestMagic && savedBit.check == savedBitChecksum();
  if (bitSaved) {
    // Saved results + watchdog reset = the watchdog works. Saved results +
    // any other reset = it didn't fire and someone pressed reset/power.
    selfTest.restoreAfterWatchdog(savedBit.results, savedBit.values, wdtReset);
    page = PG_TEST;
  }
  savedBit.magic = 0;
  unexpectedWdtReset = wdtReset && !bitSaved;

  if (cfg::OLED_INSTALLED) oled.begin(cfg::OLED_I2C_ADDR, (OledType)cfg::OLED_CHIP);
  if (cfg::IMU_INSTALLED) imu.begin();
  if (cfg::ATF_SENSOR == cfg::ATF_DS18B20) {
    atfBus.begin(pin::ATF_PROBE);
    atfProbe.startMs = millis() - cfg::DS18B20_PERIOD_MS;  // first reading before PBIT
  }

  const uint32_t now = millis();
  controller.begin(now, unexpectedWdtReset);
  if (USB_LOG_ENABLED) {
    Serial.print(F("# TCU boot, reset flags 0x"));
    Serial.println(resetFlags, HEX);
  }
  // A hung loop stops kicking the dog; the reset drops the relays.
  wdt_enable(WDTO_250MS);
}

void loop() {
  wdt_reset();
  const uint32_t now = millis();
  sampleSwitches(now);
  handleTestButton(now);

  static uint32_t lastControl = 0;
  static bool pbitDone = false;
  if ((uint32_t)(now - lastControl) >= cfg::CONTROL_PERIOD_MS) {
    lastControl = now;
    lastIn = readInputs(now);
    const tcu::Outputs& out = controller.step(lastIn);
    if (!pbitDone && now >= cfg::PBIT_DELAY_MS) {
      pbitDone = true;
      selfTest.runPowerOn(lastIn, unexpectedWdtReset);
      logBitTable(selfTest.ibitDone() ? "IBIT done (after watchdog reset)" : "PBIT");
    }
    static bool ibitWasActive = false;
    if (selfTest.active()) {
      // The self-test owns the outputs. Its interlock requires the lever out
      // of D (and the key-on relay check only runs then too), so the
      // controller is in STOCK and not driving anything meanwhile.
      if (selfTest.ibitActive()) ibitWasActive = true;
      selfTest.step(lastIn, testEdgePending);
      testEdgePending = false;
      if (selfTest.outputs().triggerWatchdog) runWatchdogTest();  // does not return
      applyTestOutputs(selfTest.outputs());
      if (cfg::OLED_INSTALLED) oled.setInvert(selfTest.outputs().lamps);
      if (!selfTest.active()) {
        logBitTable(ibitWasActive ? (selfTest.aborted() ? "IBIT aborted" : "IBIT done") : "PBIT relay check");
        ibitWasActive = false;
      }
    } else {
      applyOutputs(out);
      updateLeds(out, now);
    }
    logEcuShifts(out);
  }

  serviceAtfProbe(now);
  serviceGyro(now);
  serviceOled(now);
  serviceUsbLog(now);
}
