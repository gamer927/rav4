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
constexpr uint8_t CONSOLE_LED = 7;   // overhead "CUSTOM" indicator (via transistor)
constexpr uint8_t S1 = 8;            // high-side driver IN, shift solenoid 1
constexpr uint8_t S2 = 9;            // high-side driver IN, shift solenoid 2
constexpr uint8_t SL = 10;           // high-side driver IN, lockup solenoid
constexpr uint8_t STATUS_LED = 13;
// Active-LOW inputs: console switches close to ground, optos pull low.
constexpr uint8_t UP = 22;           // [+] shift button
constexpr uint8_t DOWN = 23;         // [-] shift button
constexpr uint8_t ECO = 24;          // ECO switch
constexpr uint8_t SPORT = 25;        // SPORT switch (both off = NORMAL)
constexpr uint8_t MASTER = 26;       // ON/OFF switch: closed = ON (CUSTOM); open/broken wire = STOCK
constexpr uint8_t BRAKE = 27;        // opto from stop-lamp switch (STP)
constexpr uint8_t LEVER_D = 28;      // opto from park/neutral switch "D" output
constexpr uint8_t ECU_S1 = 29;       // opto from ECU S1 output (ECU side of relay)
constexpr uint8_t ECU_S2 = 30;
constexpr uint8_t ECU_SL = 31;
constexpr uint8_t TEST = 32;         // console TEST button
constexpr uint8_t ATF_PROBE = 33;    // DS18B20 1-Wire data (4.7k pull-up to 5 V)
constexpr uint8_t TPS = A0;          // ECM VTA (10k series tap)
constexpr uint8_t TPS_E2 = A1;       // ECM E2 sensor ground (10k series tap)
constexpr uint8_t ATF = A2;
constexpr uint8_t VBAT = A3;
constexpr uint8_t IS_S1 = A4;        // high-side driver current-sense outputs
constexpr uint8_t IS_S2 = A5;
constexpr uint8_t IS_SL = A6;
// I2C (SDA = 20, SCL = 21): console OLED (0x3C) and MPU-6050 gyro (0x68).
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
Ema tpsF, atfF, vbatF, speedF, rpmF, solF[3];

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
tcu::Debouncer dbUp, dbDown, dbEco, dbSport, dbMaster, dbBrake, dbLeverD, dbEcuS1, dbEcuS2, dbEcuSL, dbTest;

inline bool activeLow(uint8_t p) { return digitalRead(p) == LOW; }

void sampleSwitches(uint32_t now) {
  dbUp.update(activeLow(pin::UP), now, cfg::DEBOUNCE_SWITCH_MS);
  dbDown.update(activeLow(pin::DOWN), now, cfg::DEBOUNCE_SWITCH_MS);
  dbEco.update(activeLow(pin::ECO), now, cfg::DEBOUNCE_SWITCH_MS);
  dbSport.update(activeLow(pin::SPORT), now, cfg::DEBOUNCE_SWITCH_MS);
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
  // Until the car is calibrated the ON/OFF switch is ignored (STOCK only).
  in.customRequested = cfg::CALIBRATED && dbMaster.state();
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

  in.imuPresent = imu.present();
  in.latValid = cfg::IMU_INSTALLED && imu.present() && gyro.stillSamples >= 150 && gyro.misses < 5;
  in.latG = gyro.latG;

  in.solSenseValid = cfg::SOLENOID_SENSE_INSTALLED;
  const uint8_t isPins[3] = {pin::IS_S1, pin::IS_S2, pin::IS_SL};
  for (uint8_t i = 0; i < 3; i++) {
    in.solAmps[i] = solF[i].update(rawToVolts(readRaw(isPins[i])) * cfg::IS_AMPS_PER_VOLT, 0.5f);
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
  digitalWrite(pin::CONSOLE_LED, t.lamps ? HIGH : LOW);
  digitalWrite(pin::STATUS_LED, t.lamps ? HIGH : LOW);
}

void updateLeds(const tcu::Outputs& o, uint32_t now) {
  bool on;
  if (o.faults && o.armed) on = (now / 125) & 1;                        // fast blink: fault
  else if (o.control != tcu::CTRL_STOCK) on = true;                     // solid: TCU active
  else if (o.armed && o.event == tcu::EV_NEED_SYNC) on = (now / 500) & 1;  // slow: stop to engage
  else on = false;
  digitalWrite(pin::CONSOLE_LED, on ? HIGH : LOW);
  digitalWrite(pin::STATUS_LED, on ? HIGH : LOW);
}

// --------------------------------------------------------------- text
const char* controlText(tcu::Control c) {
  switch (c) {
    case tcu::CTRL_AUTO: return "AUTO";
    case tcu::CTRL_MANUAL: return "MAN";
    default: return "STOCK";
  }
}

const char* modeText(tcu::DriveMode m) {
  switch (m) {
    case tcu::MODE_ECO: return "ECO";
    case tcu::MODE_SPORT: return "SPORT";
    default: return "NORM";
  }
}

const char* eventText(tcu::Event e) {
  switch (e) {
    case tcu::EV_CUSTOM: return "TCU ACTIVE";
    case tcu::EV_STOCK: return "FACTORY ECU";
    case tcu::EV_FAULT: return "FAULT - FACTORY ECU";
    case tcu::EV_NEED_SYNC: return "STOP TO ENGAGE";
    case tcu::EV_SHIFT: return "";
    case tcu::EV_DENY_OVERREV: return "DENIED: OVER-REV";
    case tcu::EV_DENY_TOO_SLOW: return "DENIED: TOO SLOW";
    case tcu::EV_DENY_BUSY: return "DENIED: SHIFTING";
    case tcu::EV_DENY_LIMIT: return "";
    case tcu::EV_AUTO_DOWNSHIFT: return "AUTO DOWNSHIFT";
    case tcu::EV_REV_LIMIT_UPSHIFT: return "REV LIMIT UPSHIFT";
    case tcu::EV_AUTO_RESUMED: return "AUTO";
    default: return "";
  }
}

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
    case tcu::INH_MASTER: return "ON/OFF SWITCH IS ON";
    case tcu::INH_LEVER_D: return "SHIFT OUT OF D";
    case tcu::INH_MOVING: return "VEHICLE MOVING";
    case tcu::INH_ENGINE: return "ENGINE RUNNING";  // or a charger connected
    case tcu::INH_SUPPLY: return "BATTERY LOW";
    default: return "";
  }
}

const char* resultText(tcu::TestResult r) {
  switch (r) {
    case tcu::TR_PASS: return "PASS";
    case tcu::TR_FAIL: return "FAIL";
    case tcu::TR_CHECK: return "CHK";
    case tcu::TR_SKIP: return "SKIP";
    default: return "--";
  }
}

const char* promptText(tcu::TestPrompt p) {
  switch (p) {
    case tcu::PR_UP: return "PRESS [+]";
    case tcu::PR_DOWN: return "PRESS [-]";
    case tcu::PR_ECO: return "ECO ON, SPORT OFF";
    case tcu::PR_SPORT: return "SPORT ON, ECO OFF";
    case tcu::PR_BRAKE: return "PRESS BRAKE";
    default: return "";
  }
}

// "SOL S2    FAIL 0.02A"
Text itemLine(tcu::TestId id) {
  Text t;
  const tcu::TestResult r = selfTest.result(id);
  t.s(tcu::testName(id)).pad(10).s(resultText(r)).pad(15);
  if (r == tcu::TR_NONE || r == tcu::TR_SKIP) return t;
  const float v = selfTest.value(id);
  switch (id) {
    case tcu::T_SUPPLY: t.fixed(v, 1).s("V"); break;
    case tcu::T_TPS: t.n((int32_t)v).s("%"); break;
    case tcu::T_ATF: t.n((int32_t)v).s("C"); break;
    case tcu::T_SOL_S1:
    case tcu::T_SOL_S2:
    case tcu::T_SOL_SL:
    case tcu::T_RELAY_RELEASE:
      if (r != tcu::TR_CHECK) t.fixed(v, 2).s("A");
      break;
    default: break;
  }
  return t;
}

void bitStatus(Text& t) {
  if (selfTest.active()) {
    t.s("TESTING");
  } else if (selfTest.aborted()) {
    t.s("ABORTED");
  } else if (!selfTest.hasRun()) {
    t.s("NOT RUN");
  } else {
    const tcu::TestSummary m = selfTest.summary();
    if (m.fail) t.s("FAIL ").n(m.fail);
    else t.s("PASS ").n(m.pass);
    if (m.check) t.s(" CHK ").n(m.check);
  }
}

void appendFlags(Text& t, uint16_t bits, const char* const* names, uint8_t count) {
  if (!bits) {
    t.s("none");
    return;
  }
  for (uint8_t i = 0; i < count; i++) {
    if (bits & (1u << i)) t.s(names[i]).s(" ");
  }
}
const char* const kFaultNames[] = {"SUPPLY", "VSS", "TPS", "RATIO", "SOLND"};
const char* const kWarnNames[] = {"ATFSNS", "HOT", "CRIT", "ECO+SPT", "BTN", "WDT"};

uint32_t inhibitShownUntil = 0;  // show an IBIT refusal for a few seconds

// Event / prompt line on the MAIN page.
void messageText(Text& t, const tcu::Outputs& o, uint32_t now) {
  if (selfTest.active()) {
    const tcu::TestPrompt pr = selfTest.prompt();
    t.s(pr ? promptText(pr) : "SELF TEST RUNNING");
  } else if ((int32_t)(inhibitShownUntil - now) > 0) {
    t.s("NO TEST: ").s(inhibitText(selfTest.inhibit()));
  } else if (!cfg::CALIBRATED && dbMaster.state()) {
    t.s("NOT CALIBRATED");
  } else {
    t.s(eventText(o.event));
  }
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

// --------------------------------------------------------------- console OLED
enum Page : uint8_t { PG_MAIN, PG_SENSORS, PG_TEST, PG_COUNT };
uint8_t page = PG_MAIN;

void renderMain(const tcu::Outputs& o, uint32_t now) {
  const char gear[2] = {o.gear ? (char)('0' + o.gear) : '-', '\0'};
  Text big;
  big.s(gear).s("  ").s(o.control == tcu::CTRL_MANUAL ? "MANUAL" : modeText(o.mode));
  oled.setBig(big.buf);
  Text r2, r3, r4, r5, r6, r7;
  r2.s(controlText(o.control)).pad(7);
  if (!cfg::CALIBRATED) {
    r2.s("NOT CALIBRATED");
  } else {
    if (o.lockup) r2.s("LOCKUP ");
    if (o.cornerHold) r2.s("CORNER");
    else if (o.gearHold) r2.s("HOLD");
  }
  r3.s("BIT  ");
  bitStatus(r3);
  r4.s("FLT  ");
  appendFlags(r4, o.faults, kFaultNames, 5);
  r5.s("WRN  ");
  appendFlags(r5, o.warnings, kWarnNames, 6);
  messageText(r6, o, now);
  r7.n((int32_t)(lastIn.speedMph + 0.5f)).s(" mph  ATF ");
  if (lastIn.atfValid) r7.n((int32_t)lastIn.atfC).s("C");
  else r7.s("--");
  oled.setLine(2, r2.buf);
  oled.setLine(3, r3.buf);
  oled.setLine(4, r4.buf);
  oled.setLine(5, r5.buf);
  oled.setLine(6, r6.buf);
  oled.setLine(7, r7.buf);
}

void renderSensors() {
  oled.setBig("SENSORS");
  Text r2, r3, r4, r5, r6, r7;
  r2.s("SPEED").pad(9).n((int32_t)(lastIn.speedMph + 0.5f)).s(" mph");
  r3.s("ENGINE").pad(9);
  if (lastIn.rpmValid) r3.n((int32_t)lastIn.engineRpm).s(" rpm");
  else r3.s("--");
  r4.s("THROTTLE").pad(9);
  if (lastIn.tpsValid) r4.n((int32_t)(lastIn.tpsPct + 0.5f)).s(" %");
  else r4.s("FAULT");
  r5.s("ATF TEMP").pad(9);
  if (lastIn.atfValid) r5.n((int32_t)lastIn.atfC).s(" C");
  else r5.s("FAULT");
  r6.s("BATT ").fixed(lastIn.batteryV, 1).s("V").pad(11).s("LAT ");
  if (lastIn.latValid) r6.fixed(lastIn.latG < 0 ? -lastIn.latG : lastIn.latG, 2).s("g");
  else r6.s("--");
  if (lastIn.solSenseValid) {
    r7.s("SOL A ");
    for (uint8_t i = 0; i < 3; i++) r7.fixed(lastIn.solAmps[i], 2).s(" ");
  } else {
    r7.s("SOL A  not wired");
  }
  oled.setLine(2, r2.buf);
  oled.setLine(3, r3.buf);
  oled.setLine(4, r4.buf);
  oled.setLine(5, r5.buf);
  oled.setLine(6, r6.buf);
  oled.setLine(7, r7.buf);
}

void renderTest(uint32_t now) {
  Text big;
  if (selfTest.active()) big.s("TESTING");
  else if (selfTest.aborted()) big.s("ABORTED");
  else if (!selfTest.hasRun()) big.s("BIT");
  else big.s(selfTest.ibitDone() ? "BIT " : "PBIT ").s(selfTest.summary().fail ? "FAIL" : "PASS");
  oled.setBig(big.buf);
  uint8_t row = 2;
  auto line = [&](const char* t) {
    if (row <= 7) oled.setLine(row++, t);
  };

  if ((int32_t)(inhibitShownUntil - now) > 0) {
    line("CAN'T TEST:");
    line(inhibitText(selfTest.inhibit()));
    line("");
    line("NEEDS: PARKED, ENGINE");
    line("OFF, LEVER NOT IN D,");
    line("ON/OFF SWITCH OFF");
    return;
  }
  if (selfTest.active()) {
    Text t;
    const tcu::TestPrompt pr = selfTest.prompt();
    if (pr) {
      line(promptText(pr));
      t.n((int32_t)((selfTest.promptRemainingMs(now) + 999) / 1000)).s("s  (TEST = SKIP)");
      line(t.buf);
    } else if (selfTest.phase() == tcu::PH_LAMPS) {
      line("LAMP TEST: CHECK LEDS");
      line("AND SCREEN");
    } else {
      line(tcu::testName(selfTest.currentTest()));
      line("");
    }
  } else if (selfTest.aborted()) {
    line("ABORTED:");
    line(inhibitText(selfTest.inhibit()));
  } else if (selfTest.hasRun()) {
    const tcu::TestSummary m = selfTest.summary();
    Text t;
    t.s("P").n(m.pass).s(" F").n(m.fail).s(" CHK").n(m.check).s(" SKIP").n(m.skip);
    line(t.buf);
  } else {
    line("HOLD TEST 2s TO RUN");
    line("");
  }
  // Remaining rows: failures first, then items needing a human check.
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
    t.s("+").n(count - shown).s(" MORE (USB LOG)");
    line(t.buf);
  }
  if (!selfTest.active() && !selfTest.aborted() && selfTest.hasRun() && !count) line("ALL CHECKS PASSED");
  if (!selfTest.active()) line(selfTest.hasRun() ? "HOLD TEST 2s = RUN" : "");
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
    tin.customRequested = dbMaster.state();  // the raw switch, even when uncalibrated
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
    oled.setBig("WATCHDOG");
    oled.setLine(2, "TEST: BOARD SHOULD");
    oled.setLine(3, "RESTART NOW");
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
  pinMode(pin::CONSOLE_LED, OUTPUT);
  pinMode(pin::STATUS_LED, OUTPUT);

  // External 2.2k pull-ups do the real work; internal ones are a backstop.
  const uint8_t inputs[] = {pin::UP,     pin::DOWN,    pin::ECO,    pin::SPORT,  pin::MASTER, pin::BRAKE,
                            pin::LEVER_D, pin::ECU_S1, pin::ECU_S2, pin::ECU_SL, pin::TEST};
  for (uint8_t p : inputs) pinMode(p, INPUT_PULLUP);
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

  if (cfg::OLED_INSTALLED) oled.begin(cfg::OLED_I2C_ADDR, cfg::OLED_SH1106);
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
    if (selfTest.active()) {
      // IBIT owns the outputs. Its interlock requires the ON/OFF switch to be
      // OFF, so the controller is not driving anything meanwhile.
      tcu::Inputs tin = lastIn;
      tin.customRequested = dbMaster.state();
      selfTest.step(tin, testEdgePending);
      testEdgePending = false;
      if (selfTest.outputs().triggerWatchdog) runWatchdogTest();  // does not return
      applyTestOutputs(selfTest.outputs());
      if (cfg::OLED_INSTALLED) oled.setInvert(selfTest.outputs().lamps);
      if (!selfTest.active()) logBitTable(selfTest.aborted() ? "IBIT aborted" : "IBIT done");
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
