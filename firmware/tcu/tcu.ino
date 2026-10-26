/*
  Unified TCU & Telemetry Controller - hardware layer
  Vehicle: 1999 Toyota RAV4 (SXA10/SXA11), 4-speed ECT automatic, 3S-FE
  Target:  Arduino Mega 2560 (5 V I/O, 16 ADC channels, 4 UARTs)

  This file only conditions inputs, drives outputs and talks to the display.
  Every decision is made in tcu_logic.cpp, which is unit-tested on the host.

  Fail-safe contract (hardware must honour it too - see docs/02-revised-spec.md):
    * Relay coil de-energised  => factory ECU drives the real solenoids.
    * MCU reset / hang / power loss => pins go high-Z, pull-downs turn every
      driver and the relay off => car reverts to factory ECU control.
*/
#include <Arduino.h>
#include <avr/wdt.h>

#include "config.h"
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
constexpr uint8_t UP = 22;
constexpr uint8_t DOWN = 23;
constexpr uint8_t ECO = 24;
constexpr uint8_t SPORT = 25;
constexpr uint8_t MASTER = 26;       // closed = CUSTOM; open/broken wire = STOCK
constexpr uint8_t BRAKE = 27;        // opto from stop-lamp switch (STP)
constexpr uint8_t LEVER_D = 28;      // opto from park/neutral switch "D" output
constexpr uint8_t ECU_S1 = 29;       // opto from ECU S1 output (ECU side of relay)
constexpr uint8_t ECU_S2 = 30;
constexpr uint8_t ECU_SL = 31;
constexpr uint8_t TPS = A0;
constexpr uint8_t OIL = A1;
constexpr uint8_t ATF = A2;
constexpr uint8_t VBAT = A3;
}  // namespace pin

// Nextion on Serial1 (TX1 = pin 18). USB Serial is kept for the CSV log.
#define NEXTION Serial1
constexpr uint32_t NEXTION_BAUD = 115200;
constexpr uint32_t TELEMETRY_PERIOD_MS = 20;
constexpr uint32_t TELEMETRY_REFRESH_MS = 2000;
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

tcu::Controller controller;
tcu::Inputs lastIn;

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
Ema tpsF, oilF, atfF, vbatF, speedF, rpmF;

uint16_t readRaw(uint8_t p) {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += analogRead(p);
  return sum / 4;
}
float rawToVolts(uint16_t raw) { return raw * (cfg::ADC_VREF / 1023.0f); }

// --------------------------------------------------------------- switches
tcu::Debouncer dbUp, dbDown, dbEco, dbSport, dbMaster, dbBrake, dbLeverD, dbEcuS1, dbEcuS2, dbEcuSL;

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
}

tcu::Inputs readInputs(uint32_t now) {
  tcu::Inputs in;
  in.nowMs = now;
  in.up = dbUp.state();
  in.down = dbDown.state();
  in.eco = dbEco.state();
  in.sport = dbSport.state();
  // Until the car is calibrated the master switch is ignored (STOCK only).
  in.customRequested = cfg::CALIBRATED && dbMaster.state();
  in.brake = dbBrake.state();
  in.leverInD = dbLeverD.state();
  in.ecuSenseValid = cfg::ECU_SENSE_INSTALLED;
  in.ecuS1 = dbEcuS1.state();
  in.ecuS2 = dbEcuS2.state();
  in.ecuSL = dbEcuSL.state();

  // Speed is not low-pass filtered heavily: the VSS-loss check needs to see a
  // collapse, and the over-rev guard must not lag a real slowdown.
  in.speedKmh = speedF.update(pulseHz(vss, cfg::VSS_TIMEOUT_US) * 3600.0f / cfg::VSS_PULSES_PER_KM, 0.5f);
  in.engineRpm = rpmF.update(pulseHz(tach, cfg::TACH_TIMEOUT_US) * 60.0f / cfg::TACH_PULSES_PER_REV, 0.5f);
  // A dead tach wire while driving reads 0 rpm: treat as invalid so the logic
  // falls back to speed x ratio instead of trusting it.
  in.rpmValid = cfg::TACH_INSTALLED && !(in.engineRpm < 300.0f && in.speedKmh > 10.0f);

  const float tpsV = tpsF.update(rawToVolts(readRaw(pin::TPS)), 0.4f);
  in.tpsValid = tpsV >= cfg::TPS_VALID_MIN_V && tpsV <= cfg::TPS_VALID_MAX_V;
  float tps = (tpsV - cfg::TPS_CLOSED_V) / (cfg::TPS_WOT_V - cfg::TPS_CLOSED_V) * 100.0f;
  in.tpsPct = tps < 0.0f ? 0.0f : (tps > 100.0f ? 100.0f : tps);

  const float oilV = oilF.update(rawToVolts(readRaw(pin::OIL)), 0.2f);
  in.oilValid = oilV >= cfg::OIL_VALID_MIN_V && oilV <= cfg::OIL_VALID_MAX_V;
  const float psi = (oilV - cfg::OIL_V_ZERO) * (cfg::OIL_PSI_FULL / (cfg::OIL_V_FULL - cfg::OIL_V_ZERO));
  in.oilPsi = psi < 0.0f ? 0.0f : psi;

  const uint16_t atfRaw = readRaw(pin::ATF);
  in.atfValid = atfRaw > cfg::ATF_ADC_SHORT && atfRaw < cfg::ATF_ADC_OPEN;
  if (in.atfValid) {
    const float v = rawToVolts(atfRaw);
    const float r = cfg::ATF_PULLUP_OHMS * v / (cfg::ADC_VREF - v);
    const float kelvin = 1.0f / (1.0f / 298.15f + log(r / cfg::ATF_R25_OHMS) / cfg::ATF_BETA);
    in.atfC = atfF.update(kelvin - 273.15f, 0.05f);
  }

  in.batteryV = vbatF.update(rawToVolts(readRaw(pin::VBAT)) * cfg::VBAT_DIVIDER, 0.1f);
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

void updateLeds(const tcu::Outputs& o, uint32_t now) {
  bool on;
  if (o.faults && o.armed) on = (now / 125) & 1;                        // fast blink: fault
  else if (o.control != tcu::CTRL_STOCK) on = true;                     // solid: TCU active
  else if (o.armed && o.event == tcu::EV_NEED_SYNC) on = (now / 500) & 1;  // slow: stop to engage
  else on = false;
  digitalWrite(pin::CONSOLE_LED, on ? HIGH : LOW);
  digitalWrite(pin::STATUS_LED, on ? HIGH : LOW);
}

// --------------------------------------------------------------- telemetry
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

void nexEnd() {
  NEXTION.write(0xFF);
  NEXTION.write(0xFF);
  NEXTION.write(0xFF);
}

// Display fields; names must match the objects on the Nextion page.
// Numeric fields come first, then text fields (from F_CTRL on).
enum Field : uint8_t {
  F_GEAR, F_SPD, F_RPM, F_TPS, F_OIL, F_ATF, F_VOLT, F_LU, F_FLT, F_WRN, F_CTRL, F_MODE, F_MSG, F_COUNT
};
const char* const kFieldName[F_COUNT] = {"gear", "spd",  "rpm", "tps", "oil",  "atf", "volt",
                                         "lu",   "flt",  "wrn", "ctrl", "mode", "msg"};
int32_t fieldValue[F_COUNT];
bool fieldDirty[F_COUNT];

void serviceTelemetry(uint32_t now) {
  static uint32_t lastTick = 0, lastRefresh = 0;
  static bool first = true;
  static uint8_t cursor = 0;
  if ((uint32_t)(now - lastTick) < TELEMETRY_PERIOD_MS) return;
  lastTick = now;
  // Periodic full refresh in case the display rebooted or dropped a command.
  const bool refresh = first || (uint32_t)(now - lastRefresh) >= TELEMETRY_REFRESH_MS;
  if (refresh) lastRefresh = now;
  first = false;

  const tcu::Outputs& o = controller.outputs();
  int32_t v[F_COUNT];
  v[F_GEAR] = o.gear;
  v[F_SPD] = (int32_t)(lastIn.speedKmh + 0.5f);
  v[F_RPM] = lastIn.rpmValid ? (int32_t)(lastIn.engineRpm / 50.0f + 0.5f) * 50 : -1;
  v[F_TPS] = lastIn.tpsValid ? (int32_t)(lastIn.tpsPct + 0.5f) : -1;
  v[F_OIL] = lastIn.oilValid ? (int32_t)(lastIn.oilPsi + 0.5f) : -1;
  v[F_ATF] = lastIn.atfValid ? (int32_t)(lastIn.atfC + 0.5f) : -1;
  v[F_VOLT] = (int32_t)(lastIn.batteryV * 10.0f + 0.5f);  // 142 = 14.2 V
  v[F_LU] = o.lockup ? 1 : 0;
  v[F_FLT] = o.faults;
  v[F_WRN] = o.warnings;
  v[F_CTRL] = o.control;
  v[F_MODE] = o.mode;
  const bool uncalibrated = !cfg::CALIBRATED && dbMaster.state();
  v[F_MSG] = uncalibrated ? -1 : o.eventSeq;
  for (uint8_t i = 0; i < F_COUNT; i++) {
    if (refresh || v[i] != fieldValue[i]) {
      fieldValue[i] = v[i];
      fieldDirty[i] = true;
    }
  }

  // Never block the control loop: at most two commands per tick, only when the
  // UART buffer has room, round-robin so fast-changing fields can't starve
  // the rest.
  uint8_t budget = 2;
  for (uint8_t n = 0; n < F_COUNT && budget; n++) {
    const uint8_t i = (cursor + n) % F_COUNT;
    if (!fieldDirty[i]) continue;
    if (NEXTION.availableForWrite() < 40) return;
    NEXTION.print(kFieldName[i]);
    if (i < F_CTRL) {
      NEXTION.print(F(".val="));
      NEXTION.print(v[i]);
    } else {
      NEXTION.print(F(".txt=\""));
      NEXTION.print(i == F_CTRL   ? controlText(o.control)
                    : i == F_MODE ? modeText(o.mode)
                    : uncalibrated ? "NOT CALIBRATED"
                                   : eventText(o.event));
      NEXTION.print('"');
    }
    nexEnd();
    fieldDirty[i] = false;
    cursor = (i + 1) % F_COUNT;
    budget--;
  }
}

void serviceUsbLog(uint32_t now) {
  static uint32_t last = 0;
  if (!USB_LOG_ENABLED || (uint32_t)(now - last) < USB_LOG_PERIOD_MS) return;
  if (Serial.availableForWrite() < 60) return;  // never block on a slow host
  last = now;
  const tcu::Outputs& o = controller.outputs();
  // ms,ctrl,mode,gear,lockup,kmh,rpm,rpm_per_kmh,tps,atf,oil,volts,faults,warnings
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
  Serial.print(lastIn.speedKmh, 1);
  Serial.print(',');
  Serial.print((int32_t)lastIn.engineRpm);
  Serial.print(',');
  Serial.print(lastIn.speedKmh > 5.0f ? lastIn.engineRpm / lastIn.speedKmh : 0.0f, 1);
  Serial.print(',');
  Serial.print((int16_t)lastIn.tpsPct);
  Serial.print(',');
  Serial.print((int16_t)lastIn.atfC);
  Serial.print(',');
  Serial.print((int16_t)lastIn.oilPsi);
  Serial.print(',');
  Serial.print(lastIn.batteryV, 1);
  Serial.print(',');
  Serial.print(o.faults);
  Serial.print(',');
  Serial.println(o.warnings);
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
  const uint8_t inputs[] = {pin::UP,      pin::DOWN,   pin::ECO,    pin::SPORT,  pin::MASTER,
                            pin::BRAKE,   pin::LEVER_D, pin::ECU_S1, pin::ECU_S2, pin::ECU_SL};
  for (uint8_t p : inputs) pinMode(p, INPUT_PULLUP);
  pinMode(pin::VSS, INPUT);
  pinMode(pin::TACH, INPUT);

  Serial.begin(115200);
  // Nextion powers up at its saved baud (factory 9600). Ask it to switch for
  // this session with `baud=` (RAM only, no EEPROM wear), then follow it.
  NEXTION.begin(9600);
  NEXTION.print(F("baud="));
  NEXTION.print(NEXTION_BAUD);
  nexEnd();
  NEXTION.flush();
  delay(50);
  NEXTION.begin(NEXTION_BAUD);

  attachInterrupt(digitalPinToInterrupt(pin::VSS), isrVss, FALLING);
  attachInterrupt(digitalPinToInterrupt(pin::TACH), isrTach, FALLING);

  const uint32_t now = millis();
  controller.begin(now, resetFlags & _BV(WDRF));
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

  static uint32_t lastControl = 0;
  if ((uint32_t)(now - lastControl) >= cfg::CONTROL_PERIOD_MS) {
    lastControl = now;
    lastIn = readInputs(now);
    const tcu::Outputs& out = controller.step(lastIn);
    applyOutputs(out);
    updateLeds(out, now);
  }

  serviceTelemetry(now);
  serviceUsbLog(now);
}
