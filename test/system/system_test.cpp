// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// System test: the real firmware (firmware/tcu/tcu.ino, setup() and loop())
// running against a simulated RAV4 and simulated TCU hardware, one failure at
// a time. Each scenario checks the car ends up safe: the factory ECU driving
// the solenoids (relays released, no current through the TCU's drivers) or
// the TCU still in sane control, and the screen saying what happened.
//
// Built by `make system`, which compiles a copy of the firmware with the
// watchdog self-test step off (a host process can't be reset by a watchdog;
// the loop's watchdog kicks are timed instead), CALIBRATED = true, and the
// analog ATF sender (the DS18B20's 1-Wire timing isn't simulated).
//
// What's simulated:
//   * the ECU: shifts by road speed, its S1/S2/SL outputs seen by the optos
//   * relays: a relay is pulled in only if D6 is high AND the ON/OFF
//     switch's power pole feeds the coils; welded contacts can be injected
//   * the solenoids' current through each ACS712 (open wires injectable)
//   * engine rpm from road speed and whichever gear is really engaged
//   * VSS / tach pulses, throttle, battery, ATF temperature, the coil sense
//   * the I2C screen and gyro (the cable can be unplugged)
//   * the watchdog: the longest gap between kicks is recorded
#include <sys/wait.h>
#include <unistd.h>

#include "Arduino.h"
uint32_t g_ms = 0;
int g_pin[100];
int g_out[100];
uint8_t MCUSR = 0;
uint32_t g_wdtLastKick = 0, g_wdtMaxGap = 0;
bool g_wdtEnabled = false;
HardwareSerial Serial, Serial1;
#include "Wire.h"
TwoWire Wire;
int analogImpl(uint8_t p);
int (*g_analog)(uint8_t) = analogImpl;

#include "tcu.ino"

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                                        \
  do {                                                                                     \
    g_checks++;                                                                            \
    if (!(cond)) {                                                                         \
      g_failures++;                                                                        \
      std::printf("    FAIL line %d: %s  (t=%u ms, ctrl=%d, gear=%d, faults=0x%x, warn=0x%x)\n", __LINE__, \
                  #cond, g_ms, controller.outputs().control, controller.outputs().gear,                \
                  controller.outputs().faults, controller.outputs().warnings);                         \
    }                                                                                      \
  } while (0)

// ------------------------------------------------------------------ the car
struct Car {
  float mph = 0, tps = 0, atfC = 70, battV = 12.6f;
  bool engineRunning = false, cranking = false;
  bool leverInD = false, brake = false;
  bool switchOn = false;          // the ON/OFF switch (both poles)
  bool eco = true, sport = false, manual = false;
  // Injected faults
  bool coilFuseBlown = false;     // power pole open: no coil power even when ON
  bool cutBroken = false;         // power pole shorted: coil power even when OFF
  bool readPoleShorted = false;   // the read pole's wire shorted to ground: reads ON
  bool welded[3] = {false, false, false};
  bool solenoidOpen[3] = {false, false, false};
  bool ecuSenseDead = false;
  bool leverStuckD = false;       // the lever-D input shorted: always reads D
  // Calibration: this car's real speed-sensor pulses per mile vs. the
  // firmware's setting, and the O/D OFF button (the ECU stops at 3rd).
  float vssScale = 1.0f;
  bool odOff = false;
  bool tachLost = false, vssLost = false;
} car;

bool coilPowered() { return (car.switchOn || car.cutBroken) && !car.coilFuseBlown; }
bool relayIn(int i) { return (g_out[6] && coilPowered()) || car.welded[i]; }

// The ECU's own gear: a simple speed schedule.
uint8_t ecuGear() {
  const uint8_t g = car.mph < 12 ? 1 : car.mph < 24 ? 2 : car.mph < 40 ? 3 : 4;
  return car.odOff && g > 3 ? 3 : g;
}
// The gear the transaxle is really in: the solenoids decide, and each
// solenoid is fed by the TCU's driver if its relay is in, else by the ECU.
uint8_t engagedGear() {
  bool ecuS1, ecuS2;
  tcu::solenoidsForGear(ecuGear(), ecuS1, ecuS2);
  const bool s1 = relayIn(0) ? (g_out[pin::S1] && !car.solenoidOpen[0]) : ecuS1;
  const bool s2 = relayIn(1) ? (g_out[pin::S2] && !car.solenoidOpen[1]) : ecuS2;
  return tcu::gearFromSolenoids(s1, s2);
}
float engineRpm() {
  if (car.cranking) return 250;
  if (!car.engineRunning) return 0;
  const float rpm = car.mph * cfg::RPM_PER_MPH[engagedGear()] * 1.05f;  // a little converter slip
  return rpm < 750 ? 750 : rpm;
}

int toRaw(float v) { int r = (int)(v / 5.0f * 1023.0f + 0.5f); return r < 0 ? 0 : r > 1023 ? 1023 : r; }
int analogImpl(uint8_t p) {
  if (p == A0) return toRaw(cfg::TPS_CLOSED_V + car.tps / 100.0f * (cfg::TPS_WOT_V - cfg::TPS_CLOSED_V));
  if (p == A1) return toRaw(0.0f);
  if (p == A2) {
    const float r = cfg::ATF_R25_OHMS * expf(cfg::ATF_BETA * (1.0f / (car.atfC + 273.15f) - 1.0f / 298.15f));
    return toRaw(5.0f * r / (r + cfg::ATF_PULLUP_OHMS));
  }
  if (p == A3) return toRaw(car.battV / cfg::VBAT_DIVIDER);
  if (p == A7) return toRaw(coilPowered() ? car.battV / cfg::VBAT_DIVIDER : 0.0f);
  if (p >= A4 && p <= A6) {
    const int i = p - A4;
    const uint8_t drv[3] = {pin::S1, pin::S2, pin::SL};
    const bool flows = g_out[drv[i]] && relayIn(i) && !car.solenoidOpen[i];
    return toRaw(cfg::IS_ZERO_V + (flows ? 0.95f : 0.0f) / cfg::IS_AMPS_PER_VOLT);
  }
  return 0;
}

void applyCarToPins() {
  auto lowIf = [](bool on) { return on ? LOW : HIGH; };  // optos and switches pull low
  g_pin[pin::MASTER] = lowIf(car.switchOn || car.readPoleShorted);
  g_pin[pin::ECO] = lowIf(car.eco);
  g_pin[pin::SPORT] = lowIf(car.sport);
  g_pin[pin::MANUAL] = lowIf(car.manual);
  g_pin[pin::LEVER_D] = lowIf(car.leverInD || car.leverStuckD);
  g_pin[pin::BRAKE] = lowIf(car.brake);
  bool s1, s2;
  tcu::solenoidsForGear(ecuGear(), s1, s2);
  const bool sl = ecuGear() >= 3 && car.mph > 40 && !car.brake;
  g_pin[pin::ECU_S1] = lowIf(s1 && !car.ecuSenseDead);
  g_pin[pin::ECU_S2] = lowIf(s2 && !car.ecuSenseDead);
  g_pin[pin::ECU_SL] = lowIf(sl && !car.ecuSenseDead);
}

void tick() {
  g_ms++;
  applyCarToPins();
  const float rpm = car.tachLost ? 0 : engineRpm();
  if (car.mph > 0.3f && !car.vssLost) {
    vss.periodUs = (uint32_t)(1e6f / (car.mph * cfg::VSS_PULSES_PER_MILE * car.vssScale / 3600.0f));
    vss.lastUs = micros();
  } else {
    vss.periodUs = 0;
  }
  if (rpm > 50) {
    tach.periodUs = (uint32_t)(1e6f / (rpm * cfg::TACH_PULSES_PER_REV / 60.0f));
    tach.lastUs = micros();
  } else {
    tach.periodUs = 0;
  }
  loop();
}
void run(uint32_t ms) { for (uint32_t i = 0; i < ms; i++) tick(); }
void rampTo(float mph, float mphPerS = 8.0f) {
  while (fabsf(car.mph - mph) > 0.01f) {
    const float step = mphPerS / 1000.0f;
    car.mph = car.mph < mph ? fminf(mph, car.mph + step) : fmaxf(mph, car.mph - step);
    tick();
  }
}
void press(uint8_t p, uint32_t holdMs) {
  g_pin[p] = LOW;
  for (uint32_t i = 0; i < holdMs; i++) { g_ms++; loop(); g_pin[p] = LOW; }
  g_pin[p] = HIGH;
  run(150);
}

// Key on, engine started, parked; then the start-of-drive routine:
// ON/OFF flipped OFF -> ON, lever into D.
void boot() {
  for (int i = 0; i < 100; i++) g_pin[i] = HIGH;
  Serial.echo = false;
  applyCarToPins();
  setup();
  run(2500);  // PBIT at 1.5 s, then the key-on relay check
}
// The starter pulls the battery down for a moment, then the alternator charges.
void startEngine() {
  car.cranking = true;
  car.battV = 10.8f;
  run(600);
  car.cranking = false;
  car.engineRunning = true;
  car.battV = 14.2f;
  run(500);
}
void armAndDrive() {
  car.switchOn = false;
  run(200);
  car.switchOn = true;
  run(200);
  car.leverInD = true;
  run(300);
}
bool tcuInControl() { return g_out[pin::RELAY] && coilPowered(); }
float tcuDriverAmps() {
  float a = 0;
  for (int i = 0; i < 3; i++) a += (analogImpl(A4 + i) - toRaw(cfg::IS_ZERO_V)) * 5.0f / 1023.0f * cfg::IS_AMPS_PER_VOLT;
  return a;
}
// Safe state: the ECU drives every solenoid (no relay in), no current
// through the TCU's drivers.
bool factorySafe() { return !relayIn(0) && !relayIn(1) && !relayIn(2) && tcuDriverAmps() < 0.1f; }
const char* screenWords() {
  static std::string s;
  s.clear();
  // The text the OLED driver currently holds (what's on the glass).
  for (int r = 0; r < 8; r++) { s += oled.debugLine(r); s += "|"; }
  return s.c_str();
}
bool screenSays(const char* words) { return strstr(screenWords(), words) != nullptr; }

// ------------------------------------------------------------------ scenarios
void s_boot_parked() {
  boot();
  CHECK(selfTest.result(tcu::T_RELAY_RELEASE) == tcu::TR_PASS);
  CHECK(selfTest.result(tcu::T_ONOFF_CUT) == tcu::TR_PASS);
  CHECK(!g_out[pin::RELAY]);
  CHECK(factorySafe());
  CHECK(screenSays("STOCK"));
  CHECK(screenSays("TCU IS OFF"));
  CHECK(g_wdtEnabled);
}

void s_switch_on_at_key_on_does_not_arm() {
  car.switchOn = true;
  boot();
  startEngine();  // in P: the starter won't turn in D
  car.leverInD = true;
  run(1000);
  CHECK(!tcuInControl());
  CHECK(screenSays("WAITING"));
}

void s_normal_drive() {
  boot();
  startEngine();
  armAndDrive();
  CHECK(tcuInControl());
  CHECK(controller.outputs().gear == 1);
  car.tps = 25;
  rampTo(60, 6);
  run(3000);
  CHECK(tcuInControl());
  CHECK(controller.outputs().gear == 4);
  CHECK(engagedGear() == 4);
  CHECK(!controller.outputs().faults);
  CHECK(g_wdtMaxGap <= 20);  // the loop kicks the watchdog every few ms
  CHECK(screenSays("ECO"));
}

void s_onoff_off_while_driving() {
  boot();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(50, 6);
  CHECK(tcuInControl());
  car.switchOn = false;
  run(60);
  CHECK(factorySafe());
  CHECK(controller.outputs().control == tcu::CTRL_STOCK);
}

void s_hardware_cut_with_firmware_hung() {
  // The Mega hangs with the relays ON (D6 high). Turning ON/OFF off must
  // still drop the relays, through the switch's power pole alone.
  boot();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(40, 6);
  CHECK(tcuInControl());
  car.switchOn = false;  // no loop() runs from here: the firmware is "hung"
  applyCarToPins();
  CHECK(g_out[pin::RELAY]);  // the firmware still commands the relays...
  CHECK(factorySafe());      // ...but they have no power: the ECU drives
}

void s_read_pole_shorted() {
  // The switch's read wire shorted to ground: it reads ON for ever. The power
  // pole disagrees when the driver turns it OFF: hardware drops the relays
  // and the firmware sees the mismatch.
  boot();
  startEngine();
  armAndDrive();
  rampTo(30, 6);
  CHECK(tcuInControl());
  car.readPoleShorted = true;
  car.switchOn = false;
  run(500);
  CHECK(factorySafe());
  CHECK(controller.outputs().control == tcu::CTRL_STOCK);
  CHECK(controller.outputs().faults & tcu::FAULT_ONOFF);
  CHECK(screenSays("ON/OFF SWITCH"));
}

void s_power_pole_shorted() {
  // The power pole stuck closed: OFF no longer cuts the coils. Found at key-on
  // and before any take-over.
  car.cutBroken = true;
  boot();
  CHECK(selfTest.result(tcu::T_ONOFF_CUT) == tcu::TR_FAIL);
  startEngine();
  armAndDrive();  // ON again: both poles agree now, but the key-on check failed
  run(500);
  // It may take over (the cut is a backup to the firmware's own OFF), but the
  // fault is on the screen and in the self-test result.
  car.switchOn = false;
  run(500);
  CHECK(controller.outputs().faults & tcu::FAULT_ONOFF);
  CHECK(!g_out[pin::RELAY]);  // the firmware's own OFF still works
  CHECK(screenSays("PROBLEM"));
}

void s_coil_fuse_blown() {
  boot();
  startEngine();
  car.coilFuseBlown = true;
  armAndDrive();
  run(500);
  CHECK(controller.outputs().control == tcu::CTRL_STOCK);
  CHECK(controller.outputs().faults & tcu::FAULT_ONOFF);
  CHECK(factorySafe());
}

void s_welded_relay_at_key_on() {
  car.welded[1] = true;
  boot();
  CHECK(selfTest.result(tcu::T_RELAY_RELEASE) == tcu::TR_FAIL);
  startEngine();
  armAndDrive();
  run(500);
  CHECK(!g_out[pin::RELAY]);
  CHECK(controller.outputs().faults & tcu::FAULT_RELAY);
  CHECK(screenSays("RELAY STUCK"));
}

void s_lever_input_stuck_on() {
  car.leverStuckD = true;  // reads D even in P
  boot();
  CHECK(selfTest.result(tcu::T_RELAY_RELEASE) == tcu::TR_SKIP);  // "lever in D": not run
  startEngine();  // the starter only turns in P or N...
  CHECK(controller.outputs().faults & tcu::FAULT_LEVER);  // ...so the input is lying
  armAndDrive();
  rampTo(20, 6);
  CHECK(!g_out[pin::RELAY]);
  CHECK(factorySafe());
  CHECK(screenSays("LEVER SENSOR"));
}

void s_solenoid_wire_breaks() {
  boot();
  startEngine();
  armAndDrive();
  car.tps = 20;
  rampTo(20, 6);  // 2nd: S1 and S2 on
  CHECK(tcuInControl());
  car.solenoidOpen[0] = true;
  run(500);
  CHECK(factorySafe());
  CHECK(controller.outputs().faults & tcu::FAULT_SOLENOID);
  CHECK(screenSays("SOLENOID WIRE"));
}

void s_mode_switch_mistake() {
  boot();
  startEngine();
  armAndDrive();
  rampTo(30, 6);
  CHECK(tcuInControl());
  car.sport = true;  // ECO still on
  run(500);
  CHECK(tcuInControl());  // within the 1 s grace
  run(800);
  CHECK(factorySafe());
  CHECK(screenSays("MODE ERROR"));
  car.eco = false;  // fixed: SPORT only
  run(300);
  CHECK(tcuInControl());
  CHECK(controller.outputs().mode == tcu::MODE_SPORT);
  CHECK(engagedGear() == controller.outputs().gear);  // re-synced, no jump
}

void s_lever_out_of_d() {
  boot();
  startEngine();
  armAndDrive();
  rampTo(10, 6);
  CHECK(tcuInControl());
  car.leverInD = false;
  run(100);
  CHECK(factorySafe());
}

void s_vss_lost() {
  boot();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(45, 6);
  CHECK(tcuInControl());
  car.vssLost = true;
  run(300);
  CHECK(factorySafe());
  CHECK(controller.outputs().faults & tcu::FAULT_VSS);
}

void s_tach_lost() {
  boot();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(40, 6);
  car.tachLost = true;
  run(3000);
  CHECK(tcuInControl());  // still safe: the guards use speed x ratio
  CHECK(controller.outputs().warnings & tcu::WARN_TACH);
  CHECK(screenSays("NO RPM"));
}

void s_screen_unplugged() {
  boot();
  startEngine();
  armAndDrive();
  Wire.unplugged = true;
  car.tps = 25;
  rampTo(50, 6);
  run(3000);
  CHECK(tcuInControl());
  CHECK(controller.outputs().gear >= 3);
  CHECK(g_wdtMaxGap <= 20);  // a dead bus never stalls the loop
}

void s_battery_drops() {
  boot();
  startEngine();
  armAndDrive();
  rampTo(30, 6);
  car.battV = 9.5f;
  run(800);
  CHECK(factorySafe());
  CHECK(controller.outputs().faults & tcu::FAULT_SUPPLY);
}

void s_ecu_sense_dead_take_over_moving() {
  car.ecuSenseDead = true;
  boot();
  startEngine();
  car.leverInD = true;
  car.tps = 25;
  rampTo(30, 6);  // the ECU drives; really in 3rd, the sense reads "4th"
  car.switchOn = false;
  run(200);
  car.switchOn = true;
  run(500);
  CHECK(!tcuInControl());
  CHECK(screenSays("STOP FIRST"));
  rampTo(0, 8);
  run(300);
  CHECK(tcuInControl());
  CHECK(controller.outputs().gear == 1);
}

// Build book 13.4, step by step: what the book says the screen shows.
void s_build_book_jack_stands() {
  boot();
  car.switchOn = false;
  startEngine();                       // 1. engine on in P, ON/OFF off
  CHECK(screenSays("STOCK"));
  car.brake = true;
  car.leverInD = true;                 // 2. foot on the brake, lever to D
  run(500);
  CHECK(controller.outputs().gear == 1);
  car.switchOn = true;                 // 3. ON/OFF on
  run(300);
  CHECK(tcuInControl());
  CHECK(screenSays("TCU ON") && screenSays("ECO"));
  car.brake = false;                   // 4-5. gently up to 25 mph: 1 -> 2 -> 3
  car.tps = 15;
  bool saw2 = false;
  while (car.mph < 25) {
    rampTo(car.mph + 1, 4);
    saw2 |= controller.outputs().gear == 2;
  }
  run(1500);
  CHECK(saw2);
  CHECK(controller.outputs().gear == 3);
  car.tps = 0;                         // 6. lift off, brake to a stop: back to 1
  car.brake = true;
  rampTo(0, 4);
  run(1000);
  CHECK(controller.outputs().gear == 1);
  car.brake = false;                   // 7. at 20 mph ON/OFF off: the ECU carries on
  car.tps = 15;
  rampTo(20, 4);
  car.switchOn = false;
  run(100);
  CHECK(!tcuInControl());
  run(400);  // the screen redraws in chunks
  if (!screenSays("TCU OFF")) std::printf("    screen: %s\n", screenWords());
  CHECK(screenSays("TCU OFF"));
  car.switchOn = true;
  run(300);
  car.tps = 0;                         // 8. stop, MANUAL: shift with the lever
  car.brake = true;
  rampTo(0, 4);
  run(500);
  car.brake = false;
  car.eco = false;
  car.manual = true;
  run(1500);
  CHECK(tcuInControl());
  CHECK(screenSays("MANUAL"));
  car.tps = 15;
  rampTo(10, 4);
  press(pin::UP, 60);
  CHECK(controller.outputs().gear == 2);
  rampTo(20, 4);
  press(pin::UP, 60);
  CHECK(controller.outputs().gear == 3);
  run(1500);
  press(pin::DOWN, 60);
  CHECK(controller.outputs().gear == 2);
  rampTo(30, 4);                       // 9. 1st at 30 mph: refused
  run(1500);
  press(pin::DOWN, 60);
  CHECK(controller.outputs().gear == 2);
  CHECK(screenSays("TOO FAST"));
}

// MANUAL at the redline: the TCU leaves the gear alone and says SHIFT UP.
void s_manual_redline() {
  boot();
  startEngine();
  car.eco = false;
  car.manual = true;
  armAndDrive();
  CHECK(tcuInControl());
  car.tps = 100;
  rampTo(cfg::SHIFT_LIGHT_RPM / (cfg::RPM_PER_MPH[1] * 1.05f) + 2, 8);  // 1st, past 6,000 rpm
  run(600);
  CHECK(controller.outputs().gear == 1);  // still 1st: your call
  if (!screenSays("SHIFT UP!")) std::printf("    screen: %s\n", screenWords());
  CHECK(screenSays("SHIFT UP!"));
  press(pin::UP, 60);
  run(600);
  CHECK(controller.outputs().gear == 2);
  CHECK(!screenSays("SHIFT UP!"));
}

// Hold TEST and answer the self-test's prompts as they come, like a person.
void runIbitAnsweringPrompts() {
  car.switchOn = true;
  run(300);
  press(pin::TEST, 2100);
  for (int guard = 0; guard < 60000 && selfTest.active(); guard++) {
    const tcu::TestPrompt pr = selfTest.prompt();
    car.eco = pr == tcu::PR_ECO;
    car.sport = pr == tcu::PR_SPORT;
    car.manual = pr == tcu::PR_MANUAL;
    car.brake = pr == tcu::PR_BRAKE;
    if (pr == tcu::PR_ONOFF_OFF) car.switchOn = false;
    g_ms++;
    applyCarToPins();
    if (pr == tcu::PR_UP) g_pin[pin::UP] = LOW;
    if (pr == tcu::PR_DOWN) g_pin[pin::DOWN] = LOW;
    loop();
    g_pin[pin::UP] = g_pin[pin::DOWN] = HIGH;
  }
  car.eco = true;
  car.brake = false;
  run(500);
}

void s_ibit_full() {
  boot();
  // Parked, engine off, lever in P, ON/OFF ON (the relays need its power).
  car.switchOn = true;
  run(300);
  press(pin::TEST, 2100);
  CHECK(selfTest.ibitActive());
  // Answer the prompts as they come.
  for (int guard = 0; guard < 60000 && selfTest.active(); guard++) {
    const tcu::TestPrompt pr = selfTest.prompt();
    g_pin[pin::UP] = pr == tcu::PR_UP ? LOW : HIGH;
    car.eco = pr == tcu::PR_ECO;
    car.sport = pr == tcu::PR_SPORT;
    car.manual = pr == tcu::PR_MANUAL;
    car.brake = pr == tcu::PR_BRAKE;
    if (pr == tcu::PR_ONOFF_OFF) car.switchOn = false;
    g_ms++;
    applyCarToPins();
    if (pr == tcu::PR_UP) g_pin[pin::UP] = LOW;
    if (pr == tcu::PR_DOWN) g_pin[pin::DOWN] = LOW;
    loop();
    g_pin[pin::UP] = g_pin[pin::DOWN] = HIGH;
  }
  run(500);  // the screen redraws every 100 ms
  CHECK(!selfTest.active());
  CHECK(!selfTest.aborted());
  for (uint8_t i = tcu::T_SOL_S1; i <= tcu::T_ONOFF_CUT; i++) {
    if (selfTest.result((tcu::TestId)i) != tcu::TR_PASS) std::printf("    %s: %d\n", tcu::testName((tcu::TestId)i), selfTest.result((tcu::TestId)i));
    CHECK(selfTest.result((tcu::TestId)i) == tcu::TR_PASS);
  }
  CHECK(screenSays("ALL OK"));
  CHECK(factorySafe());
}

void s_ibit_refused_while_engine_runs() {
  boot();
  startEngine();
  car.switchOn = true;
  run(300);
  press(pin::TEST, 2100);
  CHECK(!selfTest.ibitActive());
  CHECK(screenSays("ENGINE IS RUNNING"));
  CHECK(factorySafe());
}

void s_reset_mid_drive() {
  // The Mega resets at 50 mph (watchdog, brown-out): its pins go high-Z, so
  // the relays drop at once, and after the reboot it must NOT take over again
  // by itself, even with ON/OFF still ON.
  boot();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(50, 6);
  CHECK(tcuInControl());
  for (int i = 0; i < 100; i++) g_out[i] = 0;  // reset: every pin an input again
  CHECK(factorySafe());
  MCUSR = _BV(WDRF);
  captureResetFlags();  // what the chip runs before main(): reads why it reset
  setup();
  run(3000);
  CHECK(!tcuInControl());
  CHECK(factorySafe());
  CHECK(controller.outputs().warnings & tcu::WARN_WDT_RESET);
  CHECK(screenSays("WAITING"));  // needs a fresh OFF -> ON flip
}

void s_solenoid_wire_shorted() {
  boot();
  startEngine();
  armAndDrive();
  rampTo(20, 6);
  CHECK(tcuInControl());
  // Model a short as far more current than a solenoid draws.
  CHECK(g_out[pin::S2]);  // S2 is on in both 2nd and 3rd
  struct Short { static int analog(uint8_t p) { return p == A5 && g_out[pin::S2] ? toRaw(cfg::IS_ZERO_V + 4.0f / cfg::IS_AMPS_PER_VOLT) : analogImpl(p); } };
  g_analog = Short::analog;
  run(500);
  g_analog = analogImpl;
  CHECK(!g_out[pin::RELAY]);
  CHECK(controller.outputs().faults & tcu::FAULT_SOLENOID);
}

void s_atf_probe_unplugged() {
  boot();
  startEngine();
  armAndDrive();
  rampTo(40, 6);
  car.atfC = -80;  // reads as an open circuit
  run(2000);
  CHECK(tcuInControl());  // a warning, not a reason to hand back
  CHECK(controller.outputs().warnings & tcu::WARN_ATF_SENSOR);
  CHECK(screenSays("NO TEMP"));
}

void s_atf_overheats() {
  boot();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(60, 6);
  car.atfC = 130;
  run(3000);
  CHECK(controller.outputs().warnings & tcu::WARN_ATF_CRITICAL);
  CHECK(screenSays("TRANS HOT!"));
  CHECK(controller.outputs().lockup);  // hot: lockup on, to stop converter heating
}

struct Scenario {
  const char* name;
  void (*fn)();
};
const Scenario kScenarios[] = {
    {"boots parked: key-on checks pass, factory ECU drives", s_boot_parked},
    {"ON/OFF already ON at key-on: waits for a fresh flip", s_switch_on_at_key_on_does_not_arm},
    {"normal drive: takes over in D, shifts 1-4, watchdog kicked", s_normal_drive},
    {"ON/OFF turned OFF while driving: ECU back within 60 ms", s_onoff_off_while_driving},
    {"firmware hung with relays on: ON/OFF still cuts them in hardware", s_hardware_cut_with_firmware_hung},
    {"ON/OFF read wire shorted: power pole cuts, mismatch reported", s_read_pole_shorted},
    {"ON/OFF power pole shorted: found at key-on and reported", s_power_pole_shorted},
    {"relay coil fuse blown: no take-over, reported", s_coil_fuse_blown},
    {"relay welded: found at key-on, no take-over", s_welded_relay_at_key_on},
    {"solenoid wire breaks while driving: ECU back, reported", s_solenoid_wire_breaks},
    {"two mode switches on: ECU after 1 s, back when fixed", s_mode_switch_mistake},
    {"lever out of D: ECU back", s_lever_out_of_d},
    {"MANUAL at the redline: no automatic upshift, SHIFT UP on the screen", s_manual_redline},
    {"lever-D input stuck on: found at engine start, no take-over", s_lever_input_stuck_on},
    {"speed signal lost at 45 mph: ECU back", s_vss_lost},
    {"tach lost: still in control, warned", s_tach_lost},
    {"screen cable cut: control unaffected, loop never stalls", s_screen_unplugged},
    {"battery voltage collapses: ECU back", s_battery_drops},
    {"ECU sense dead, take-over on the move: waits for a stop", s_ecu_sense_dead_take_over_moving},
    {"reset at 50 mph: relays drop, no take-over after the reboot", s_reset_mid_drive},
    {"solenoid wire shorted: over-current, ECU back", s_solenoid_wire_shorted},
    {"ATF probe unplugged: warned, still in control", s_atf_probe_unplugged},
    {"ATF overheats: warned, lockup held to cool it", s_atf_overheats},
    {"self-test (IBIT): every check passes, incl. the hardware cut", s_ibit_full},
    {"build book 13.4 (jack stands), step by step: the screen says what the book says", s_build_book_jack_stands},
    {"self-test refused with the engine running", s_ibit_refused_while_engine_runs},
};

// --screens DIR: capture the pictures in docs/oled-screens.png (rendered by
// tools/render_screens.py) from what the simulated screen really shows.
const char* g_shotDir = nullptr;
void snap(const char* name) {
  run(400);  // let the screen redraw and the chunked writes finish
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.pbm", g_shotDir, name);
  FILE* f = fopen(path, "w");
  if (!f) return;
  fprintf(f, "P1\n128 64\n");
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 128; x++) {
      const int on = (Wire.fb[y / 8][x] >> (y % 8)) & 1;
      fprintf(f, "%d ", Wire.inverted ? !on : on);
    }
    fprintf(f, "\n");
  }
  fclose(f);
}
void captureScreens() {
  boot();
  snap("1_main_parked");
  press(pin::TEST, 200);
  snap("2_sensors");
  press(pin::TEST, 200);
  press(pin::TEST, 200);  // back to MAIN
  // A refused self-test, then one that finds a broken S2 wire.
  car.engineRunning = true;
  car.battV = 14.1f;
  car.switchOn = true;
  run(500);
  press(pin::TEST, 2100);
  snap("4_test_refused_engine_running");
  car.engineRunning = false;
  car.battV = 12.6f;
  run(3500);
  car.solenoidOpen[1] = true;
  press(pin::TEST, 2100);
  while (selfTest.active() && selfTest.prompt() != tcu::PR_UP) { g_ms++; applyCarToPins(); loop(); }
  snap("6_ibit_prompt");
  for (int guard = 0; guard < 60000 && selfTest.active(); guard++) {
    const tcu::TestPrompt pr = selfTest.prompt();
    car.eco = pr == tcu::PR_ECO;
    car.sport = pr == tcu::PR_SPORT;
    car.manual = pr == tcu::PR_MANUAL;
    car.brake = pr == tcu::PR_BRAKE;
    if (pr == tcu::PR_ONOFF_OFF) car.switchOn = false;
    g_ms++;
    applyCarToPins();
    if (pr == tcu::PR_UP) g_pin[pin::UP] = LOW;
    if (pr == tcu::PR_DOWN) g_pin[pin::DOWN] = LOW;
    loop();
    g_pin[pin::UP] = g_pin[pin::DOWN] = HIGH;
  }
  snap("7_ibit_done_with_fault");
  // Drive in SPORT, then a corner, then two mode switches on: the wire
  // repaired and a new key cycle (RAM starts fresh, as in the car).
  car.solenoidOpen[1] = false;
  car.eco = false;
  car.sport = true;
  car.brake = false;
  car.switchOn = false;
  selfTest = tcu::SelfTest();
  controller = tcu::Controller();
  boot();
  startEngine();
  armAndDrive();
  press(pin::TEST, 200);  // TEST page -> MAIN
  car.tps = 25;
  rampTo(51, 6);
  car.tps = 15;
  run(4000);
  snap("8_main_driving_sport");
  Wire.yawRaw = (int16_t)(0.35f * 9.81f / (car.mph * 0.44704f) * 57.2958f * 65.5f);
  run(300);
  snap("9_main_corner_hold");
  Wire.yawRaw = 0;
  run(1500);
  car.eco = true;
  run(1300);
  snap("11_mode_error");
}

// --callog FILE: the USB log of a calibration session (tools/calibrate.py's
// test reads it): parked with the pedal up and down, then a drive with
// steady 30, 45 (O/D OFF: 3rd, locked) and 60 mph. This car's speed sensor
// gives 6 % more pulses per mile than the firmware's setting, and its engine
// turns 5 % more rpm per mph than RPM_PER_MPH (see engineRpm()).
void captureCalibrationLog(const char* path) {
  car.vssScale = 1.06f;
  boot();
  runIbitAnsweringPrompts();  // the bench step reads its solenoid currents
  auto mark = [](const char* m) { Serial.put(std::string("#! ") + m + "\n"); };
  mark("pedal_up begin");
  run(3000);
  mark("pedal_up end");
  car.tps = 100;
  run(500);
  mark("pedal_down begin");
  run(3000);
  mark("pedal_down end");
  car.tps = 0;
  run(500);
  mark("battery begin");
  run(3000);
  mark("battery end");
  startEngine();
  car.leverInD = true;
  run(300);
  car.tps = 20;
  rampTo(30, 4);
  run(25000);
  car.odOff = true;
  rampTo(45, 4);
  run(25000);
  car.odOff = false;
  rampTo(60, 4);
  run(25000);
  car.tps = 0;
  rampTo(0, 4);
  run(1000);
  FILE* f = fopen(path, "w");
  fputs(Serial.out.c_str(), f);
  fclose(f);
}

int main(int argc, char** argv) {
  if (argc > 2 && strcmp(argv[1], "--callog") == 0) {
    captureCalibrationLog(argv[2]);
    return 0;
  }
  if (argc > 2 && strcmp(argv[1], "--screens") == 0) {
    g_shotDir = argv[2];
    captureScreens();
    return 0;
  }
  // Each scenario runs in its own process: the firmware's globals start fresh.
  int failed = 0, n = 0;
  for (const Scenario& s : kScenarios) {
    if (argc > 1 && !strstr(s.name, argv[1])) continue;
    n++;
    fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
      s.fn();
      std::printf("%s %s  (%d checks)\n", g_failures ? "FAIL" : "ok  ", s.name, g_checks);
      fflush(stdout);
      _exit(g_failures ? 1 : 0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      failed++;
      if (!WIFEXITED(status)) std::printf("CRASH %s\n", s.name);
    }
  }
  std::printf("\n%d scenarios, %d failed\n", n, failed);
  return failed ? 1 : 0;
}
