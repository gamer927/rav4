// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Host unit tests for firmware/tcu/tcu_logic.cpp. Run: make test
#include <cmath>
#include <cstdio>
#include <utility>

#include "../firmware/tcu/config.h"
#include "../firmware/tcu/ds18b20.h"
#include "../firmware/tcu/tcu_logic.h"

using namespace tcu;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    g_checks++;                                                            \
    if (!(cond)) {                                                         \
      g_failures++;                                                        \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                      \
  } while (0)

// Drives the controller in 10 ms steps with a healthy, stationary car in D.
struct Sim {
  Controller c;
  Inputs in;
  Sim(bool wdt = false) {
    in.nowMs = 1000;
    in.leverInD = true;
    in.ecuSenseValid = true;
    in.ecuS1 = true;  // ECU holding 1st
    in.ecuS2 = false;
    in.rpmValid = true;
    in.engineRpm = 750;
    in.tpsValid = true;
    in.atfValid = true;
    in.atfC = 80;
    in.batteryV = 14.2f;
    in.eco = true;  // exactly one mode switch on
    c.begin(in.nowMs, wdt);
  }
  // When true, engine rpm follows speed x ratio of the engaged gear (as with a
  // locked converter), so the ratio plausibility check stays quiet.
  bool autoRpm = true;
  const Outputs& step(uint32_t ms = 10) {
    const Outputs* o = &c.outputs();
    for (uint32_t t = 0; t < ms; t += 10) {
      in.nowMs += 10;
      if (autoRpm) {
        const uint8_t g = c.outputs().gear ? c.outputs().gear : 1;
        const float rpm = in.speedMph * cfg::RPM_PER_MPH[g];
        in.engineRpm = rpm < 750 ? 750 : rpm;
      }
      o = &c.step(in);
    }
    return *o;
  }
  // Change speed at a physically plausible rate (default 12 mph per second).
  const Outputs& rampTo(float mph, float mphPerS = 12.0f) {
    const float stepMph = mphPerS / 100.0f;
    while (in.speedMph != mph) {
      if (in.speedMph < mph) in.speedMph = in.speedMph + stepMph > mph ? mph : in.speedMph + stepMph;
      else in.speedMph = in.speedMph - stepMph < mph ? mph : in.speedMph - stepMph;
      step(10);
    }
    return c.outputs();
  }
  // Flip the master switch STOCK -> CUSTOM.
  const Outputs& arm() {
    in.customRequested = false;
    step(cfg::ARM_OFF_MIN_MS + 50);
    in.customRequested = true;
    return step(50);
  }
  const Outputs& tap(bool& button) {
    button = true;
    step(50);
    button = false;
    return step(50);
  }
  void setEcuGear(uint8_t g) {
    solenoidsForGear(g, in.ecuS1, in.ecuS2);
  }
  void cruise(float mph) { rampTo(mph); }
  // Pick a mode: exactly one of ECO, SPORT, MANUAL on.
  void select(bool eco, bool sport, bool manual) {
    in.eco = eco;
    in.sport = sport;
    in.manual = manual;
  }
  // Arm, then flip MANUAL on (and ECO off).
  const Outputs& armManual() {
    arm();
    select(false, false, true);
    return step(50);
  }
};

static void test_solenoid_table() {
  std::printf("solenoid truth table (1999 RAV4)\n");
  bool s1, s2;
  solenoidsForGear(1, s1, s2); CHECK(s1 && !s2);
  solenoidsForGear(2, s1, s2); CHECK(s1 && s2);
  solenoidsForGear(3, s1, s2); CHECK(!s1 && s2);
  solenoidsForGear(4, s1, s2); CHECK(!s1 && !s2);
  for (uint8_t g = 1; g <= 4; g++) {
    solenoidsForGear(g, s1, s2);
    CHECK(gearFromSolenoids(s1, s2) == g);
  }
  solenoidsForGear(0, s1, s2); CHECK(!s1 && !s2);
  solenoidsForGear(9, s1, s2); CHECK(!s1 && !s2);
}

static void test_boot_requires_switch_cycle() {
  std::printf("arming: switch left in CUSTOM at boot stays STOCK\n");
  Sim s;
  s.in.customRequested = true;
  const Outputs& o = s.step(500);
  CHECK(o.control == CTRL_STOCK);
  CHECK(!o.relayCustom);
  CHECK(!o.s1 && !o.s2 && !o.sl);
  s.arm();
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(s.c.outputs().relayCustom);
  CHECK(s.c.outputs().gear == 1);
  CHECK(s.c.outputs().s1 && !s.c.outputs().s2);
}

static void test_boot_glitch_does_not_arm() {
  std::printf("arming: a short OFF glitch (inputs settling at boot) doesn't count as a flip\n");
  Sim s;
  s.in.customRequested = false;  // the filters read OFF for 30 ms at power-up...
  s.step(30);
  s.in.customRequested = true;   // ...then the real level: ON since key-on
  s.step(500);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  s.arm();                       // a real flip still works
  CHECK(s.c.outputs().control == CTRL_AUTO);
}

static void test_watchdog_flag() {
  std::printf("watchdog reset is reported and needs re-arm\n");
  Sim s(true);
  s.in.customRequested = true;
  CHECK(s.step(100).control == CTRL_STOCK);
  CHECK(s.c.outputs().warnings & WARN_WDT_RESET);
}

static void test_entry_sync() {
  std::printf("entry while moving syncs to ECU gear; refuses without sense\n");
  Sim s;
  s.setEcuGear(3);
  s.in.ecuSL = true;
  s.cruise(43.5);
  s.arm();
  CHECK(s.c.outputs().control == CTRL_AUTO || s.c.outputs().control == CTRL_MANUAL);
  // AUTO may legitimately upshift later, but at entry the gear must match the ECU.
  Sim s2;
  s2.setEcuGear(3);
  s2.in.ecuSL = true;
  s2.in.speedMph = 43.5;
  s2.in.customRequested = false;
  s2.step(cfg::ARM_OFF_MIN_MS + 50);
  s2.in.customRequested = true;
  const Outputs& o = s2.step(10);
  CHECK(o.control != CTRL_STOCK);
  CHECK(o.gear == 3);
  CHECK(!o.s1 && o.s2);
  CHECK(o.sl);  // lockup carried over bumplessly

  Sim s3;
  s3.in.ecuSenseValid = false;
  s3.in.speedMph = 37.3;
  s3.arm();
  CHECK(s3.c.outputs().control == CTRL_STOCK);
  CHECK(s3.c.outputs().event == EV_NEED_SYNC);
  s3.in.speedMph = 0;  // car stopped: now it can engage in 1st
  CHECK(s3.step(20).control != CTRL_STOCK);
  CHECK(s3.c.outputs().gear == 1);
}

static void test_lever_out_of_d() {
  std::printf("lever out of D hands back to ECU, re-engages in D\n");
  Sim s;
  s.arm();
  CHECK(s.c.outputs().control == CTRL_AUTO);
  s.in.leverInD = false;
  CHECK(s.step(20).control == CTRL_STOCK);
  CHECK(!s.c.outputs().relayCustom);
  s.in.leverInD = true;
  CHECK(s.step(20).control == CTRL_AUTO);
}

static void test_no_autorepeat_and_interval() {
  std::printf("held button shifts once; min shift interval enforced\n");
  Sim s;
  s.armManual();
  s.cruise(24.9);
  s.in.up = true;
  s.step(900);  // held for 0.9 s: the original 250 ms auto-repeat gave 1->4
  CHECK(s.c.outputs().gear == 2);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  s.in.up = false;
  s.step(1000);
  // Two quick taps: the second lands inside the shift interval and is refused.
  s.in.up = true;
  s.step(30);
  s.in.up = false;
  s.step(30);
  CHECK(s.c.outputs().gear == 3);
  s.in.up = true;
  s.step(30);
  CHECK(s.c.outputs().gear == 3);
  CHECK(s.c.outputs().event == EV_DENY_BUSY);
  s.in.up = false;
  s.step(1000);
  s.tap(s.in.up);
  CHECK(s.c.outputs().gear == 4);
  s.step(1000);
  s.tap(s.in.up);
  CHECK(s.c.outputs().gear == 4);
  CHECK(s.c.outputs().event == EV_DENY_LIMIT);
}

static void test_downshift_overrev_guard() {
  std::printf("downshift guards (directive 1, all gears)\n");
  Sim s;
  s.armManual();
  s.cruise(18.6);
  s.tap(s.in.up); s.step(800);
  s.cruise(37.3);
  s.tap(s.in.up); s.step(800);
  s.cruise(62.1);
  s.tap(s.in.up); s.step(800);
  CHECK(s.c.outputs().gear == 4);
  // Just above the speed where 3rd would exceed the downshift rpm limit.
  s.cruise(cfg::MAX_DOWNSHIFT_RPM / cfg::RPM_PER_MPH[3] + 5);
  s.tap(s.in.down);
  CHECK(s.c.outputs().gear == 4);
  CHECK(s.c.outputs().event == EV_DENY_OVERREV);
  s.step(800);
  s.cruise(68.4);  // 3rd well under the limit
  s.tap(s.in.down);
  s.step(300);
  CHECK(s.c.outputs().gear == 3);
  s.step(800);
  s.cruise(49.7);  // 2nd under the limit
  s.tap(s.in.down);
  s.step(300);
  CHECK(s.c.outputs().gear == 2);
  s.step(800);
  s.cruise(49.7);  // 1st would be far over the limit
  s.tap(s.in.down);
  CHECK(s.c.outputs().gear == 2);
  CHECK(s.c.outputs().event == EV_DENY_OVERREV);
  s.step(800);
  s.cruise(cfg::FIRST_GEAR_MAX_MPH + 2);  // under rpm limit but over hard cap
  s.tap(s.in.down);
  CHECK(s.c.outputs().gear == 2);
  s.step(800);
  s.cruise(18.6);
  s.tap(s.in.down);
  CHECK(s.c.outputs().gear == 1);
}

static void test_upshift_too_slow() {
  std::printf("upshift into forced-downshift zone is refused\n");
  Sim s;
  s.armManual();
  s.cruise(3.1);
  s.tap(s.in.up);
  CHECK(s.c.outputs().gear == 1);
  CHECK(s.c.outputs().event == EV_DENY_TOO_SLOW);
}

static void test_manual_auto_downshift_to_stop() {
  std::printf("MANUAL: forced downshifts while stopping, ends in 1st\n");
  Sim s;
  s.armManual();
  float v = 18.6f;
  s.cruise(v); s.tap(s.in.up); s.step(800);
  s.cruise(37.3); s.tap(s.in.up); s.step(800);
  s.cruise(55.9); s.tap(s.in.up); s.step(800);
  CHECK(s.c.outputs().gear == 4);
  s.in.brake = true;
  for (v = 56; v > 0; v -= 0.155f) {  // ~15.5 mph/s braking
    s.in.speedMph = v;
    s.step(10);
    CHECK(!s.c.outputs().sl);
  }
  s.in.speedMph = 0;
  s.step(3000);
  CHECK(s.c.outputs().gear == 1);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
}

static void test_manual_switch() {
  std::printf("MANUAL switch: on = shifter shifts, off = AUTO; shifter ignored in AUTO\n");
  Sim s;
  s.arm();
  s.cruise(24.9);
  s.step(3000);  // let AUTO settle
  CHECK(s.c.outputs().control == CTRL_AUTO);
  const uint8_t g = s.c.outputs().gear;
  s.tap(s.in.down);  // MANUAL off: a click doesn't shift, and says why
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(s.c.outputs().event == EV_DENY_NOT_MANUAL);
  CHECK(s.c.outputs().gear == g);
  s.select(false, false, true);
  s.step(20);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  CHECK(s.c.outputs().event == EV_MANUAL);
  CHECK(s.c.outputs().gear == g);  // flipping the switch never shifts by itself
  s.step(800);
  s.tap(s.in.up);
  s.step(300);
  CHECK(s.c.outputs().gear == g + 1);
  s.select(true, false, false);
  s.step(20);
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(s.c.outputs().event == EV_AUTO_RESUMED);

  // Switch already on when the TCU takes over: starts in MANUAL.
  Sim m;
  m.select(false, false, true);
  m.arm();
  CHECK(m.c.outputs().control == CTRL_MANUAL);
  // And the switch does nothing in STOCK.
  m.in.customRequested = false;
  m.step(50);
  CHECK(m.c.outputs().control == CTRL_STOCK);
}

static void test_auto_schedule() {
  std::printf("AUTO: upshifts with speed; ECO earlier than SPORT; kickdown\n");
  auto upAt = [](bool eco) {
    Sim s;
    s.select(eco, !eco, false);
    s.arm();
    s.in.tpsPct = 25;
    for (float v = 0; v < 93; v += 0.31f) {
      s.in.speedMph = v;
      s.step(50);
      if (s.c.outputs().gear == 2) return v;
    }
    return -1.0f;
  };
  const float eco = upAt(true), sport = upAt(false);
  std::printf("  1->2 at 25%%: eco %.1f  sport %.1f mph\n", eco, sport);
  CHECK(eco > 0 && sport > 0);
  CHECK(eco < sport);

  Sim s;
  s.arm();
  s.in.tpsPct = 20;
  for (float v = 0; v <= 62; v += 0.31f) {
    s.in.speedMph = v;
    s.step(50);
  }
  s.step(3000);
  CHECK(s.c.outputs().gear == 4);
  s.in.tpsPct = 100;  // kickdown at 62 mph -> 3rd, but not on to 2nd
  s.step(3000);
  CHECK(s.c.outputs().gear == 3);
}

static void test_kickdown_is_immediate() {
  std::printf("kickdown right after a shift goes 4->3->2 without the dwell\n");
  Sim s;
  s.select(false, true, false);
  s.cruise(40.4);
  s.setEcuGear(4);
  s.arm();
  CHECK(s.c.outputs().gear == 4);
  s.step(300);  // only 0.3 s in 4th: a part-throttle downshift would still wait
  s.in.tpsPct = 100;
  s.step(20);
  CHECK(s.c.outputs().gear == 3);
  s.step(cfg::MIN_SHIFT_INTERVAL_MS);
  CHECK(s.c.outputs().gear == 2);  // 2nd at 40 mph is under the rpm limit
  s.step(2000);
  CHECK(s.c.outputs().gear == 2);  // and it doesn't go on to 1st (28 mph cap)

  // Part throttle in the same spot still respects the dwell.
  Sim t;
  t.select(false, true, false);
  t.cruise(40.4);
  t.setEcuGear(4);
  t.arm();
  t.step(300);
  t.in.tpsPct = 50;  // below SPORT's 60 % kickdown threshold
  t.step(20);
  CHECK(t.c.outputs().gear == 4);
  t.step(cfg::AUTO_MIN_TIME_IN_GEAR_MS);
  CHECK(t.c.outputs().gear == 3);
}

static void test_corner_hold() {
  std::printf("corner hold: no automatic shifts mid-corner, manual still works\n");
  Sim s;
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(24.9);
  CHECK(s.c.outputs().gear == 2);
  s.in.latValid = true;
  s.in.latG = -0.35f;  // turning left
  s.step(50);
  CHECK(s.c.outputs().cornerHold);
  s.cruise(37.3);  // past the 2->3 point, but mid-corner
  s.step(1000);
  CHECK(s.c.outputs().gear == 2);
  s.in.latG = 0.05f;  // straightening out
  s.step(cfg::CORNER_EXIT_MS - 100);
  CHECK(s.c.outputs().gear == 2);
  s.step(400);
  CHECK(!s.c.outputs().cornerHold);
  CHECK(s.c.outputs().gear == 3);

  // A long sweeper (or a stuck sensor) only holds for CORNER_HOLD_MAX_MS.
  Sim t;
  t.arm();
  t.in.tpsPct = 20;
  t.cruise(24.9);
  t.in.latValid = true;
  t.in.latG = 0.3f;
  t.step(20);
  t.cruise(37.3);
  t.step(cfg::CORNER_HOLD_MAX_MS - 2000);
  CHECK(t.c.outputs().gear == 2);
  t.step(2500);
  CHECK(t.c.outputs().gear >= 3);  // shifting resumed (ECO may go straight to 4th)

  // Gyro dead (or its bias not learned): the reading is ignored, no corner
  // hold, the shift map works as normal.
  Sim g;
  g.arm();
  g.in.tpsPct = 20;
  g.cruise(24.9);
  g.in.latValid = false;
  g.in.latG = 0.5f;  // garbage from a dead sensor
  g.step(50);
  CHECK(!g.c.outputs().cornerHold);
  g.cruise(37.3);
  g.step(1000);
  CHECK(g.c.outputs().gear >= 3);

  // The driver's own shift is always honoured.
  Sim u;
  u.arm();
  u.in.tpsPct = 20;
  u.cruise(24.9);
  u.in.latValid = true;
  u.in.latG = 0.4f;
  u.select(false, false, true);
  u.step(20);
  u.tap(u.in.up);
  CHECK(u.c.outputs().gear == 3);

  // No gyro data: no hold.
  Sim v;
  v.arm();
  v.in.tpsPct = 20;
  v.cruise(24.9);
  v.in.latValid = false;
  v.in.latG = 0.5f;
  v.cruise(37.3);
  v.step(1500);
  CHECK(v.c.outputs().gear >= 3);
}

static void test_brake_downshift() {
  std::printf("SPORT brake downshift: gear ready for the exit, held until throttle\n");
  Sim s;
  s.select(false, true, false);
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(62.1);
  s.step(1500);
  CHECK(s.c.outputs().gear == 4);
  s.in.tpsPct = 0;
  s.in.brake = true;
  s.rampTo(52.8, 9.3f);  // hard braking
  CHECK(s.c.outputs().gear == 3);
  s.rampTo(37.3, 9.3f);
  CHECK(s.c.outputs().gear == 2);
  CHECK(s.in.speedMph * cfg::RPM_PER_MPH[2] <= cfg::BRAKE_DOWNSHIFT_RPM[MODE_SPORT]);
  s.rampTo(31.1, 9.3f);
  CHECK(s.c.outputs().gear == 2);  // 1st only below the 4200 rpm / 28 mph line
  s.in.brake = false;              // corner entry: off the brake, off the throttle
  s.step(cfg::BRAKE_HOLD_MS - 500);
  CHECK(s.c.outputs().gear == 2);  // the 0 % map would have gone back to 3rd at 50
  s.in.tpsPct = 40;                // exit: back on the throttle
  s.step(100);
  CHECK(s.c.outputs().gear == 2);  // 40 % in SPORT holds 2nd at 31 mph

  // Hold also ends on its own if the driver just coasts.
  Sim t;
  t.select(false, true, false);
  t.arm();
  t.in.tpsPct = 20;
  t.cruise(62.1);
  t.step(1500);
  t.in.tpsPct = 0;
  t.in.brake = true;
  t.rampTo(37.3, 9.3f);
  CHECK(t.c.outputs().gear == 2);
  t.in.brake = false;
  t.step(cfg::BRAKE_HOLD_MS + 1500);
  CHECK(t.c.outputs().gear == 3);

  // NORMAL doesn't brake-downshift; SPORT doesn't do it mid-corner.
  Sim n;
  n.arm();
  n.in.tpsPct = 20;
  n.cruise(62.1);
  n.step(1500);
  n.in.tpsPct = 0;
  n.in.brake = true;
  n.rampTo(37.3, 9.3f);
  CHECK(n.c.outputs().gear == 4);
  Sim c;
  c.select(false, true, false);
  c.arm();
  c.in.tpsPct = 20;
  c.cruise(62.1);
  c.step(1500);
  c.in.latValid = true;
  c.in.latG = 0.4f;
  c.in.tpsPct = 0;
  c.in.brake = true;
  c.rampTo(43.5, 6.2f);
  CHECK(c.c.outputs().gear == 4);
}

static void test_coast_unlock() {
  std::printf("SPORT releases lockup on a closed throttle; ECO keeps it\n");
  for (int mode = 0; mode < 2; mode++) {
    Sim s;
    s.in.sport = mode == 1;
    s.in.eco = mode == 0;
    s.arm();
    s.in.tpsPct = 20;
    s.cruise(62.1);
    s.step(3000);
    CHECK(s.c.outputs().gear == 4);
    CHECK(s.c.outputs().sl);
    s.in.tpsPct = 0;
    s.step(50);
    CHECK(s.c.outputs().sl == (mode == 0));
    s.in.tpsPct = 3;  // inside the hysteresis band: stays as it is
    s.step(1500);
    CHECK(s.c.outputs().sl == (mode == 0));
    s.in.tpsPct = 15;
    s.step(1500);
    CHECK(s.c.outputs().sl);
  }
}

static void test_eco_throttle_smoothing() {
  std::printf("ECO: a quick pedal stab doesn't downshift; a sustained push or floor does\n");
  auto setup = [](Sim& s) {
    s.in.eco = true;
    s.arm();
    s.in.tpsPct = 15;
    s.cruise(43.5);
    s.step(3000);
  };
  Sim s;
  setup(s);
  CHECK(s.c.outputs().gear == 4);
  s.in.tpsPct = 60;  // 0.3 s stab
  s.step(300);
  s.in.tpsPct = 15;
  s.step(2000);
  CHECK(s.c.outputs().gear == 4);

  Sim t;
  setup(t);
  t.in.tpsPct = 60;  // held: the driver wants more
  t.step(3000);
  CHECK(t.c.outputs().gear == 3);

  Sim f;
  setup(f);
  f.in.tpsPct = 100;  // floored: kickdown is never smoothed
  f.step(30);
  CHECK(!f.c.outputs().sl);  // lockup lets go at once...
  f.step(cfg::LOCKUP_RELEASE_BEFORE_SHIFT_MS);
  CHECK(f.c.outputs().gear == 3);  // ...and 3rd follows 0.2 s later
}

static void test_sport_tipin_release() {
  std::printf("SPORT: a fast stab releases lockup at once; a slow squeeze doesn't\n");
  auto setup = [](Sim& s) {
    s.select(false, true, false);
    s.arm();
    s.in.tpsPct = 15;
    s.cruise(62.1);
    s.step(3000);
  };
  Sim s;
  setup(s);
  CHECK(s.c.outputs().gear == 4 && s.c.outputs().sl);
  s.in.tpsPct = 35;  // +20 % at once, still under SPORT's 45 % lockup limit
  s.step(120);
  CHECK(!s.c.outputs().sl);

  Sim t;
  setup(t);
  for (int i = 0; i < 200; i++) {  // +20 % over 2 s
    t.in.tpsPct += 0.1f;
    t.step(10);
  }
  CHECK(t.c.outputs().sl);
}

static void test_manual_is_sport() {
  std::printf("MANUAL = SPORT you shift yourself; AUTO goes back to the switch mode\n");
  Sim s;
  s.in.eco = true;  // ECO switch on
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(62.1);
  s.step(3000);
  CHECK(s.c.outputs().mode == MODE_ECO);
  CHECK(s.c.outputs().gear == 4 && s.c.outputs().sl);
  s.in.tpsPct = 0;  // ECO keeps lockup on the coast
  s.step(200);
  CHECK(s.c.outputs().sl);
  s.in.tpsPct = 20;
  s.step(1500);

  s.select(false, false, true);  // MANUAL switch on
  s.tap(s.in.down);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  CHECK(s.c.outputs().mode == MODE_SPORT);
  s.step(cfg::LOCKUP_RELEASE_BEFORE_SHIFT_MS);
  CHECK(s.c.outputs().gear == 3);
  s.step(3000);
  CHECK(s.c.outputs().sl);  // SPORT lockup rules in 3rd at 62 mph, 20 %
  s.in.tpsPct = 0;          // SPORT: coast unlock
  s.step(200);
  CHECK(!s.c.outputs().sl);
  s.in.tpsPct = 20;
  s.step(2000);
  CHECK(s.c.outputs().gear == 3);  // and it holds the gear you chose

  s.select(true, false, false);  // MANUAL switch off: back to AUTO, back to ECO
  s.step(50);
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(s.c.outputs().mode == MODE_ECO);
}

static void test_sport_lift_hold() {
  std::printf("SPORT lift-off hold: a quick lift after a hard push keeps the gear\n");
  auto run = [](bool sport, bool quickLift) {
    Sim s;
    s.select(!sport, sport, false);
    s.arm();
    s.in.tpsPct = 70;  // hard acceleration out of a corner, in 2nd
    s.rampTo(46.6, 5.0f);
    const uint8_t g = s.c.outputs().gear;
    if (quickLift) {
      s.in.tpsPct = 0;  // snap off the throttle for the next corner
    } else {
      for (int i = 0; i < 300; i++) {  // ease off over 3 s
        s.in.tpsPct -= 70.0f / 300.0f;
        s.step(10);
      }
    }
    s.step(2000);
    return std::make_pair(g, s.c.outputs().gear);
  };
  const auto sportQuick = run(true, true);
  CHECK(sportQuick.first == 2);
  CHECK(sportQuick.second == 2);  // held, ready for the next corner
  const auto sportSlow = run(true, false);
  CHECK(sportSlow.second > 2);    // a gentle lift is a cruise: upshift as usual
  const auto ecoQuick = run(false, true);
  CHECK(ecoQuick.second > ecoQuick.first);  // ECO upshifts on the lift as usual

  // The hold ends on its own after LIFT_HOLD_MS, and when the throttle returns.
  Sim t;
  t.select(false, true, false);
  t.arm();
  t.in.tpsPct = 70;
  t.rampTo(46.6, 5.0f);
  t.in.tpsPct = 0;
  t.step(100);
  CHECK(t.c.outputs().gearHold);
  t.step(cfg::LIFT_HOLD_MS + 1500);
  CHECK(!t.c.outputs().gearHold);
  CHECK(t.c.outputs().gear > 2);
}

static void test_eco_early_lockup() {
  std::printf("ECO: lockup from ~23 mph in 3rd, rpm floor in 4th, never lugs\n");
  Sim e;
  e.in.eco = true;
  e.arm();
  e.in.tpsPct = 15;
  e.cruise(24.9);
  e.step(3000);
  CHECK(e.c.outputs().gear == 3);
  CHECK(e.c.outputs().sl);  // ~1,340 rpm locked in 3rd
  Sim n;
  n.select(false, true, false);
  n.arm();
  n.in.tpsPct = 15;
  n.cruise(24.9);
  n.step(3000);
  CHECK(!n.c.outputs().sl);  // SPORT waits for 43 mph

  e.in.tpsPct = 35;  // more load at low rpm: let the converter help
  e.step(200);
  CHECK(!e.c.outputs().sl);
  e.in.tpsPct = 15;
  e.step(1500);
  CHECK(e.c.outputs().sl);

  // 4th: floor 1,250 rpm (~32 mph), released below 1,100 rpm (~28 mph).
  e.cruise(36);
  e.step(3000);
  CHECK(e.c.outputs().gear == 4);
  CHECK(e.c.outputs().sl);
  e.rampTo(29.2, 3.1f);
  CHECK(e.c.outputs().gear == 4);
  CHECK(e.c.outputs().sl);   // hysteresis: still above 1,100 rpm
  e.rampTo(26.7, 3.1f);
  CHECK(!e.c.outputs().sl);  // below the floor: unlocked, no lugging
  CHECK(e.in.speedMph * cfg::RPM_PER_MPH[4] < cfg::LU_MIN_RPM[MODE_ECO] - cfg::LU_MIN_RPM_HYST);
}

static void test_highway_lockup() {
  std::printf("highway: ECO/NORMAL stay locked in 4th at 90 mph under heavy throttle\n");
  Sim e;
  e.in.eco = true;
  e.arm();
  e.in.tpsPct = 20;
  e.cruise(90);
  e.step(3000);
  CHECK(e.c.outputs().gear == 4 && e.c.outputs().sl);
  e.in.tpsPct = 88;  // what it takes to hold 90 mph, or a grade
  e.step(3000);
  CHECK(e.c.outputs().gear == 4);
  CHECK(e.c.outputs().sl);  // ~3,550 rpm, locked: no slip, no extra heat
  e.in.tpsPct = 98;         // floored: release so the converter multiplies torque
  e.step(100);
  CHECK(!e.c.outputs().sl);

  // Below highway rpm the normal ECO limit applies.
  Sim l;
  l.in.eco = true;
  l.arm();
  l.in.tpsPct = 20;
  l.cruise(45);  // 4th, ~1,770 rpm
  l.step(3000);
  CHECK(l.c.outputs().gear == 4 && l.c.outputs().sl);
  l.in.tpsPct = 60;
  l.step(100);
  CHECK(!l.c.outputs().sl);

  // SPORT (and MANUAL) too: a 90 mph cruise must not slip and cook the ATF.
  Sim s;
  s.select(false, true, false);
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(90);
  s.step(3000);
  CHECK(s.c.outputs().gear == 4 && s.c.outputs().sl);
  for (int i = 0; i < 200; i++) {  // settle onto a heavier cruise throttle over 2 s
    s.in.tpsPct += 35.0f / 200.0f;
    s.step(10);
  }
  s.step(2000);
  CHECK(s.c.outputs().gear == 4);
  CHECK(s.c.outputs().sl);   // 55 % at 90 mph: locked, no slip
  s.in.tpsPct = 75;          // quick stab: SPORT still gets its instant release
  s.step(120);
  CHECK(!s.c.outputs().sl);
}

static void test_lockup_rules() {
  std::printf("lockup: gear/speed/brake/cold/hot/shift sequencing\n");
  Sim s;
  s.armManual();
  s.cruise(18.6); s.tap(s.in.up); s.step(800);
  s.cruise(34.2); s.tap(s.in.up); s.step(800);
  s.cruise(49.7); s.tap(s.in.up);
  s.in.tpsPct = 20;
  s.cruise(49.7);
  s.step(2000);
  CHECK(s.c.outputs().gear == 4);
  CHECK(s.c.outputs().sl);

  s.in.brake = true;
  CHECK(!s.step(10).sl);  // brake releases immediately
  s.in.brake = false;
  s.step(500);
  CHECK(!s.c.outputs().sl);  // anti-chatter delay
  s.step(1000);
  CHECK(s.c.outputs().sl);

  // Downshift with lockup on: SL releases first, gear changes after delay.
  s.tap(s.in.down);
  CHECK(!s.c.outputs().sl);
  const uint8_t g = s.c.outputs().gear;
  s.step(cfg::LOCKUP_RELEASE_BEFORE_SHIFT_MS + 50);
  CHECK(s.c.outputs().gear == 3);
  CHECK(g == 4 || g == 3);
  s.step(cfg::LOCKUP_SETTLE_AFTER_SHIFT_MS - 300);
  CHECK(!s.c.outputs().sl);  // still settling after the shift
  s.cruise(49.7);
  s.step(1500);
  CHECK(s.c.outputs().sl);

  s.rampTo(24.9);  // below every non-hot lockup threshold
  CHECK(!s.c.outputs().sl);
  CHECK(s.c.outputs().control == CTRL_MANUAL);

  // Cold ATF inhibits lockup.
  s.in.atfC = 20;
  s.cruise(49.7);
  s.step(3000);
  CHECK(!s.c.outputs().sl);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  s.in.atfC = 60;
  s.step(1500);
  CHECK(s.c.outputs().sl);
}

static void test_hot_atf_widens_lockup() {
  std::printf("directive 2 corrected: hot ATF widens lockup instead of cancelling it\n");
  auto lockedAt = [](float atf, float mph, float tps) {
    Sim s;
    s.in.atfC = atf;
    s.armManual();
    s.cruise(18.6); s.tap(s.in.up); s.step(800);
    s.cruise(34.2); s.tap(s.in.up); s.step(800);
    s.cruise(mph);
    s.in.tpsPct = tps;
    s.step(3000);
    return s.c.outputs().gear == 3 && s.c.outputs().sl;
  };
  CHECK(!lockedAt(80, 28, 20));   // NORMAL: 28 mph is below the lockup speed
  CHECK(lockedAt(115, 28, 20));   // hot: locked to stop converter heating
  CHECK(!lockedAt(80, 49.7, 70));   // NORMAL: heavy throttle unlocks
  CHECK(lockedAt(115, 49.7, 70));   // hot: stays locked
  Sim s;
  s.in.atfC = 130;
  s.step(20);
  CHECK(s.c.outputs().warnings & WARN_ATF_HOT);
  CHECK(s.c.outputs().warnings & WARN_ATF_CRITICAL);
}

static void test_vss_faults() {
  std::printf("VSS loss at speed and VSS stall both hand back to ECU (latched)\n");
  Sim s;
  s.armManual();
  s.cruise(18.6); s.tap(s.in.up); s.step(800);
  s.cruise(37.3); s.tap(s.in.up); s.step(800);
  CHECK(s.c.outputs().control != CTRL_STOCK);
  s.in.speedMph = 0;  // signal collapses
  s.step(200);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(s.c.outputs().faults & FAULT_VSS);
  CHECK(s.c.outputs().event == EV_FAULT);
  s.in.speedMph = 37.3;
  s.step(500);
  CHECK(s.c.outputs().control == CTRL_STOCK);  // latched
  s.setEcuGear(3);
  s.arm();  // driver acknowledges; re-entry syncs to the ECU's 3rd
  CHECK(s.c.outputs().control != CTRL_STOCK);
  CHECK(s.c.outputs().gear == 3);

  // Stall: ECU holding 2nd, engine pulling, VSS says 0 -> entry refused.
  Sim t;
  t.setEcuGear(2);
  t.autoRpm = false;
  t.in.engineRpm = 3500;
  t.in.tpsPct = 40;
  t.step(cfg::VSS_CHECK_MS + 200);
  CHECK(t.c.outputs().faults & FAULT_VSS);
  t.arm();
  CHECK(t.c.outputs().control == CTRL_STOCK);
  t.in.brake = true;  // brake-torquing against the converter is legitimate
  t.step(50);
  CHECK(!(t.c.outputs().faults & FAULT_VSS));
}

static void test_ratio_fault() {
  std::printf("locked-converter ratio mismatch (e.g. S1/S2 swapped) -> ECU\n");
  Sim s;
  s.armManual();
  s.cruise(18.6); s.tap(s.in.up); s.step(800);
  s.cruise(34.2); s.tap(s.in.up); s.step(800);
  s.cruise(49.7); s.tap(s.in.up);
  s.in.tpsPct = 20;
  s.cruise(49.7);
  s.step(2500);
  CHECK(s.c.outputs().sl && s.c.outputs().gear == 4);
  s.autoRpm = false;
  s.in.engineRpm = 80 * cfg::RPM_PER_MPH[3];  // car is really in 3rd
  s.step(cfg::RATIO_SETTLE_MS + cfg::RATIO_FAULT_MS + 200);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(s.c.outputs().faults & FAULT_RATIO);
}

static void test_supply_and_tps_faults() {
  std::printf("supply and TPS faults block entry and force STOCK\n");
  Sim s;
  s.in.batteryV = 9.0f;
  s.arm();
  s.step(cfg::SUPPLY_FAULT_MS + 100);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(s.c.outputs().faults & FAULT_SUPPLY);

  Sim t;
  t.arm();
  CHECK(t.c.outputs().control == CTRL_AUTO);
  t.in.tpsValid = false;
  t.step(cfg::TPS_FAULT_MS + 50);
  CHECK(t.c.outputs().control == CTRL_STOCK);
  CHECK(t.c.outputs().faults & FAULT_TPS);
  t.in.tpsValid = true;
  t.step(500);
  CHECK(t.c.outputs().control == CTRL_STOCK);  // latched until switch cycle
}

static void test_rev_limit_upshift() {
  std::printf("redline: MANUAL warns and leaves the gear to you; ECO/SPORT upshift\n");
  // MANUAL: the gear is yours, even at the redline. The screen says SHIFT UP.
  Sim s;
  s.armManual();
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  s.in.speedMph = 32.3;
  s.autoRpm = false;
  s.in.engineRpm = cfg::SHIFT_LIGHT_RPM - 100;
  s.step(500);
  CHECK(!(s.c.outputs().warnings & WARN_SHIFT_UP));
  s.in.engineRpm = cfg::REV_LIMIT_UPSHIFT_RPM + 200;
  s.step(cfg::REV_LIMIT_HOLD_MS + 1000);
  CHECK(s.c.outputs().gear == (cfg::MANUAL_REV_LIMIT_UPSHIFT ? 2 : 1));
  if (!cfg::MANUAL_REV_LIMIT_UPSHIFT) {
    CHECK(s.c.outputs().event != EV_REV_LIMIT_UPSHIFT);
    CHECK(s.c.outputs().warnings & WARN_SHIFT_UP);
    s.tap(s.in.up);  // the driver shifts: 2nd, rpm drops, the warning goes
    s.in.engineRpm = 4200;
    s.step(100);
    CHECK(s.c.outputs().gear == 2);
    CHECK(!(s.c.outputs().warnings & WARN_SHIFT_UP));
    // No tach: speed x ratio stands in for the warning.
    s.in.rpmValid = false;
    s.in.speedMph = cfg::SHIFT_LIGHT_RPM / cfg::RPM_PER_MPH[2] + 1;
    s.step(100);
    CHECK(s.c.outputs().warnings & WARN_SHIFT_UP);
    CHECK(s.c.outputs().gear == 2);
  }

  // ECO and SPORT are automatic: they upshift at the rev limit themselves.
  Sim a;
  a.arm();
  CHECK(a.c.outputs().control == CTRL_AUTO);
  a.autoRpm = false;
  a.in.speedMph = 10;
  a.in.engineRpm = cfg::REV_LIMIT_UPSHIFT_RPM + 200;
  a.step(cfg::REV_LIMIT_HOLD_MS + 50);
  CHECK(a.c.outputs().gear == 2);
  CHECK(a.c.outputs().event == EV_REV_LIMIT_UPSHIFT);
  CHECK(!(a.c.outputs().warnings & WARN_SHIFT_UP));
}

static void test_mode_switch_fault_and_stuck_button() {
  std::printf("mode switches: exactly one of ECO/SPORT/MANUAL, else factory ECU until fixed\n");
  Sim s;
  s.setEcuGear(3);
  s.in.ecuSL = false;
  s.cruise(43.5);
  s.arm();
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(s.c.outputs().mode == MODE_ECO);

  // Two on: the last valid mode carries on through the grace time...
  s.in.sport = true;  // ECO still on
  s.step(cfg::MODE_SWITCH_GRACE_MS - 100);
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(s.c.outputs().mode == MODE_ECO);
  CHECK(s.c.outputs().modeSelect == SEL_OK);
  // ...then the factory ECU takes over, with a message, not a latched fault.
  s.step(200);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(!s.c.outputs().relayCustom);
  CHECK(s.c.outputs().modeSelect == SEL_SEVERAL);
  CHECK(s.c.outputs().warnings & WARN_MODE_SWITCH);
  CHECK(s.c.outputs().event == EV_MODE_SELECT);
  CHECK(!s.c.outputs().faults);
  s.step(2000);
  CHECK(s.c.outputs().control == CTRL_STOCK);  // stays there while it's wrong

  // Fixed: back in control by itself (no ON/OFF cycle), in the new mode.
  s.setEcuGear(s.c.outputs().gear ? s.c.outputs().gear : 3);
  s.in.eco = false;
  s.step(100);
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(s.c.outputs().mode == MODE_SPORT);
  CHECK(s.c.outputs().modeSelect == SEL_OK);
  CHECK(!(s.c.outputs().warnings & WARN_MODE_SWITCH));

  // Moving from one switch to another (a moment with none on) is no handover.
  s.in.sport = false;
  s.step(400);
  s.in.eco = true;
  s.step(50);
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(s.c.outputs().mode == MODE_ECO);

  // None on, for longer than the grace time.
  s.in.eco = false;
  s.step(cfg::MODE_SWITCH_GRACE_MS + 100);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(s.c.outputs().modeSelect == SEL_NONE);

  // MANUAL with SPORT (or ECO) on is not allowed either.
  Sim m;
  m.select(false, true, true);
  m.arm();
  m.step(cfg::MODE_SWITCH_GRACE_MS + 100);
  CHECK(m.c.outputs().control == CTRL_STOCK);
  CHECK(m.c.outputs().modeSelect == SEL_SEVERAL);
  m.select(false, false, true);
  m.step(50);
  CHECK(m.c.outputs().control == CTRL_MANUAL);
  CHECK(m.c.outputs().mode == MODE_SPORT);  // MANUAL drives like SPORT

  // Nothing selected at key-on: arming doesn't engage.
  Sim n;
  n.select(false, false, false);
  n.arm();
  CHECK(n.c.outputs().control == CTRL_STOCK);

  // A shifter switch held closed for 10 s is reported.
  Sim b;
  b.in.down = true;
  b.step(cfg::STUCK_BUTTON_MS + 100);
  CHECK(b.c.outputs().warnings & WARN_BUTTON_STUCK);
}

static void test_entry_plausibility() {
  std::printf("take-over on the move: rpm must fit the sensed gear, else wait for a stop\n");
  // Sense inputs dead: they read "both off" (= 4th) while the ECU holds 2nd.
  Sim s;
  s.autoRpm = false;
  s.in.ecuS1 = false;
  s.in.ecuS2 = false;
  s.in.speedMph = 25;
  s.in.engineRpm = 25 * cfg::RPM_PER_MPH[2] * 1.1f;  // really in 2nd, a little slip
  s.arm();
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(s.c.outputs().event == EV_NEED_SYNC);
  // Healthy sense: 2nd, same rpm: takes over in 2nd.
  Sim h;
  h.autoRpm = false;
  h.setEcuGear(2);
  h.in.speedMph = 25;
  h.in.engineRpm = 25 * cfg::RPM_PER_MPH[2] * 1.1f;
  h.arm();
  CHECK(h.c.outputs().control == CTRL_AUTO);
  CHECK(h.c.outputs().gear == 2);
  // No tach on the move: can't cross-check, so wait for a stop.
  Sim n;
  n.autoRpm = false;
  n.setEcuGear(2);
  n.in.speedMph = 25;
  n.in.rpmValid = false;
  n.arm();
  CHECK(n.c.outputs().control == CTRL_STOCK);
  n.rampTo(0, 9.0f);  // brake to a stop
  n.step(100);
  CHECK(n.c.outputs().control == CTRL_AUTO);
  CHECK(n.c.outputs().gear == 1);
}

static void test_onoff_two_poles_and_relay() {
  std::printf("ON/OFF: both poles must agree; a stuck relay blocks take-over; lost tach warns\n");
  auto flip = [](Sim& s, bool on, bool coil) {
    s.in.customRequested = s.in.switchOn = on;
    s.in.coilPowered = coil;
  };
  // Healthy: both poles together.
  Sim s;
  s.in.coilSenseValid = true;
  flip(s, false, false);
  s.step(100);
  flip(s, true, true);
  s.step(100);
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(!s.c.outputs().faults);
  // Coil power lost while in control (blown fuse, broken wire): back to the ECU.
  s.in.coilPowered = false;
  s.step(cfg::ONOFF_FAULT_MS + 50);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(s.c.outputs().faults & FAULT_ONOFF);

  // The power pole stuck closed: OFF reads OFF but the coils still get power.
  // Nothing can take over until it's fixed, even after flipping ON again.
  Sim b;
  b.in.coilSenseValid = true;
  flip(b, false, true);
  b.step(cfg::ONOFF_FAULT_MS + 50);
  CHECK(b.c.outputs().faults & FAULT_ONOFF);
  flip(b, true, true);
  b.step(100);
  CHECK(b.c.outputs().control == CTRL_AUTO);  // agree again: fine
  Sim b2;
  b2.in.coilSenseValid = true;
  flip(b2, false, false);
  b2.step(100);
  flip(b2, true, false);  // read pole ON, power pole not: broken
  b2.step(cfg::ONOFF_FAULT_MS + 50);
  CHECK(b2.c.outputs().control == CTRL_STOCK);
  CHECK(b2.c.outputs().faults & FAULT_ONOFF);

  // A relay found stuck closed at key-on: never take over.
  Sim r;
  r.in.relayStuck = true;
  r.arm();
  CHECK(r.c.outputs().control == CTRL_STOCK);
  CHECK(r.c.outputs().faults & FAULT_RELAY);

  // Tach lost while driving: a warning; the over-rev guard still works on speed.
  Sim t;
  t.arm();
  t.cruise(40);
  CHECK(!(t.c.outputs().warnings & WARN_TACH));
  t.autoRpm = false;
  t.in.rpmValid = false;
  t.step(cfg::TACH_LOST_MS + 100);
  CHECK(t.c.outputs().warnings & WARN_TACH);
  CHECK(t.c.outputs().control == CTRL_AUTO);
}

static void test_lever_stuck_at_start() {
  std::printf("engine started with the lever reading D: lever input stuck\n");
  // Key on, engine off, then the starter: battery dips, rpm rises.
  auto start = [](Sim& s, bool leverReadsD) {
    s.autoRpm = false;
    s.in.leverInD = leverReadsD;
    s.in.engineRpm = 0;
    s.in.batteryV = 12.4f;
    s.step(cfg::ENGINE_OFF_MS + 500);
    s.in.engineRpm = 250;
    s.in.batteryV = 10.2f;
    s.step(600);
    s.in.engineRpm = 1100;
    s.in.batteryV = 14.2f;
    s.step(500);
    s.in.engineRpm = 750;
  };
  // Started in P (lever not D), then into D: normal.
  Sim ok;
  start(ok, false);
  ok.in.leverInD = true;
  ok.arm();
  CHECK(!(ok.c.outputs().faults & FAULT_LEVER));
  CHECK(ok.c.outputs().control == CTRL_AUTO);

  // Started while the lever input says D: impossible, so it's stuck.
  Sim bad;
  start(bad, true);
  CHECK(bad.c.outputs().faults & FAULT_LEVER);
  bad.arm();
  CHECK(bad.c.outputs().control == CTRL_STOCK);
  bad.arm();  // flipping ON/OFF again doesn't clear it: only a new key cycle
  CHECK(bad.c.outputs().control == CTRL_STOCK);
  CHECK(bad.c.outputs().faults & FAULT_LEVER);

  // Tach drops out for 2 s while idling in D: the battery is charging, no
  // starter dip, so it's not taken for a start.
  Sim idle;
  idle.arm();
  idle.autoRpm = false;
  idle.in.engineRpm = 0;
  idle.in.batteryV = 13.9f;
  idle.step(2000);
  idle.in.engineRpm = 750;
  idle.step(500);
  CHECK(!(idle.c.outputs().faults & FAULT_LEVER));

  // No stop seen first (the TCU came on mid-crank): no verdict either way.
  Sim late;
  late.autoRpm = false;
  late.in.engineRpm = 250;
  late.in.batteryV = 10.2f;
  late.step(300);
  late.in.engineRpm = 1100;
  late.in.batteryV = 14.2f;
  late.step(300);
  CHECK(!(late.c.outputs().faults & FAULT_LEVER));
}

static void test_debouncer() {
  std::printf("debouncer rejects bounce, accepts stable level\n");
  Debouncer d;
  uint32_t t = 0;
  const bool bounce[] = {true, false, true, false, true};
  for (bool b : bounce) {
    d.update(b, t, 25);
    t += 3;
  }
  CHECK(!d.state());
  for (int i = 0; i < 10; i++, t += 3) d.update(true, t, 25);
  CHECK(d.state());
  d.update(false, t, 25);
  t += 10;
  d.update(true, t, 25);  // 10 ms dropout = noise
  t += 30;
  d.update(true, t, 25);
  CHECK(d.state());
}

static void test_ds18b20_decode() {
  std::printf("DS18B20: CRC, temperature decode, fault patterns\n");
  // Maxim application note 27 example ROM: CRC of these 7 bytes is 0xA2.
  const uint8_t rom[7] = {0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00};
  CHECK(ds18b20::crc8(rom, 7) == 0xA2);

  auto pad = [](int16_t raw, uint8_t* sp) {
    const uint8_t base[9] = {0, 0, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10, 0};
    for (int i = 0; i < 9; i++) sp[i] = base[i];
    sp[0] = (uint8_t)(raw & 0xFF);
    sp[1] = (uint8_t)((uint16_t)raw >> 8);
    sp[8] = ds18b20::crc8(sp, 8);
  };
  struct Case { int16_t raw; float c; };
  const Case cases[] = {{0x07D0, 125.0f}, {0x0191, 25.0625f}, {0x06E0, 110.0f}, {(int16_t)0xFF5E, -10.125f},
                        {(int16_t)0xFC90, -55.0f}};
  uint8_t sp[9];
  float c = 0;
  for (const Case& k : cases) {
    pad(k.raw, sp);
    CHECK(ds18b20::decode(sp, c) == ds18b20::OK);
    CHECK(std::fabs(c - k.c) < 1e-4f);
  }
  pad(0x0550, sp);  // 85.0 C: also the power-on reset value
  CHECK(ds18b20::decode(sp, c) == ds18b20::POWER_ON_VALUE);
  CHECK(c == 85.0f);
  pad(0x06E0, sp);
  sp[0] ^= 0x01;  // a flipped bit on a noisy line
  CHECK(ds18b20::decode(sp, c) == ds18b20::BAD_CRC);
  uint8_t ones[9], zeros[9];
  for (int i = 0; i < 9; i++) {
    ones[i] = 0xFF;
    zeros[i] = 0x00;
  }
  CHECK(ds18b20::decode(ones, c) == ds18b20::NO_DEVICE);   // unplugged
  CHECK(ds18b20::decode(zeros, c) == ds18b20::NO_DEVICE);  // shorted (CRC of zeros is 0)
}

static void test_maps_are_sane() {
  std::printf("shift maps: hysteresis, monotonic, WOT points below rev limit\n");
  for (int m = 0; m < 2; m++) {
    for (uint8_t g = 1; g <= 3; g++) {
      for (int tps = 0; tps <= 100; tps += 5) {
        const float up = upshiftMph((DriveMode)m, g, tps);
        const float down = downshiftMph((DriveMode)m, g + 1, tps);
        CHECK(down < up);
        CHECK(up * cfg::RPM_PER_MPH[g] < cfg::REV_LIMIT_UPSHIFT_RPM);
        // Auto upshift must clear the MANUAL forced-downshift margin.
        CHECK(up >= cfg::MANUAL_MIN_MPH[g + 1] + cfg::UPSHIFT_MARGIN_MPH);
        if (tps > 0) CHECK(up >= upshiftMph((DriveMode)m, g, tps - 5));
      }
    }
  }
}


// ---------------------------------------------------------------------------
// Tests added after mutation testing (tools/mutate.py) showed bugs planted in
// these paths went unnoticed. Each says what it pins down.

// Enter CUSTOM on the move, the ECU holding `gear` (and lockup if `sl`).
static void enterMoving(Sim& s, uint8_t gear, float mph, bool sl) {
  s.setEcuGear(gear);
  s.in.ecuSL = sl;
  s.in.speedMph = mph;
  s.in.customRequested = false;
  s.step(cfg::ARM_OFF_MIN_MS + 50);
  s.in.customRequested = true;
  s.step(10);
}

static void test_arming_glitch_after_drive() {
  std::printf("arming: an OFF glitch while driving disarms; ON again needs a real OFF\n");
  Sim s;
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(30);
  s.step(60000);  // a minute of driving: the OFF that armed it is long ago
  CHECK(s.c.outputs().control == CTRL_AUTO);
  s.in.customRequested = false;  // 30 ms OFF (a bouncing contact, a bad crimp)
  s.step(30);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(!s.c.outputs().armed);
  s.in.customRequested = true;
  s.step(1000);
  CHECK(s.c.outputs().control == CTRL_STOCK);  // not re-armed by the glitch
  CHECK(!s.c.outputs().armed);
  s.in.customRequested = false;  // a real flip: OFF for longer than ARM_OFF_MIN_MS
  s.step(cfg::ARM_OFF_MIN_MS + 50);
  s.in.customRequested = true;
  s.step(20);
  CHECK(s.c.outputs().armed);
}

static void test_stock_outputs() {
  std::printf("STOCK: TCU drivers off, the ECU's gear and lockup reported\n");
  Sim s;
  enterMoving(s, 3, 43.5f, true);
  CHECK(s.c.outputs().control != CTRL_STOCK);
  CHECK(s.c.outputs().s2 && s.c.outputs().sl);
  s.in.customRequested = false;
  const Outputs& o = s.step(10);
  CHECK(o.control == CTRL_STOCK);
  CHECK(!o.relayCustom);
  CHECK(!o.s1 && !o.s2 && !o.sl);
  CHECK(o.gear == 3);    // what the ECU is doing (sensed)
  CHECK(o.lockup);       // the ECU's lockup, for the log
  s.in.ecuSL = false;
  CHECK(!s.step(10).lockup);
}

static void test_entry_never_overrevs() {
  std::printf("entry on the move: a sensed gear that would over-rev is refused, even if rpm agrees\n");
  Sim s;
  s.autoRpm = false;
  const float mph = 50.0f;
  s.in.engineRpm = mph * cfg::RPM_PER_MPH[1];  // the sense says 1st and the rpm "agrees"
  enterMoving(s, 1, mph, false);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(s.c.outputs().event == EV_NEED_SYNC);
}

static void test_lockup_released_before_shift() {
  std::printf("a shift with lockup on: release first, shift 200 ms later\n");
  Sim s;
  s.select(false, false, true);
  enterMoving(s, 3, 43.5f, true);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  CHECK(s.c.outputs().sl && s.c.outputs().gear == 3);
  s.in.up = true;
  s.step(20);
  s.in.up = false;
  s.step(20);
  CHECK(!s.c.outputs().sl);          // converter clutch let go at once...
  CHECK(s.c.outputs().gear == 3);    // ...the gear waits for it
  s.step(cfg::LOCKUP_RELEASE_BEFORE_SHIFT_MS);
  CHECK(s.c.outputs().gear == 4);
}

static void test_first_shift_right_after_takeover() {
  std::printf("the first shift after a take-over isn't held up by the shift interval\n");
  Sim s;
  s.select(false, false, true);
  enterMoving(s, 3, 43.5f, false);
  CHECK(s.c.outputs().gear == 3);
  s.tap(s.in.up);
  CHECK(s.c.outputs().gear == 4);
  CHECK(s.c.outputs().event == EV_SHIFT);
}

static void test_events_repeat() {
  std::printf("the same message twice in a row is shown twice (event counter)\n");
  // Two refused downshifts: each one must reach the screen.
  Sim s;
  s.select(false, false, true);
  enterMoving(s, 3, 43.5f, false);
  s.step(800);
  uint8_t seq = s.c.outputs().eventSeq;
  s.in.down = true; s.step(30); s.in.down = false; s.step(30);  // 3 -> 2 at 43.5 mph: fine
  CHECK(s.c.outputs().gear == 2);
  s.step(800);
  seq = s.c.outputs().eventSeq;
  s.tap(s.in.down);  // 2 -> 1 at 43.5 mph: refused
  CHECK(s.c.outputs().event == EV_DENY_OVERREV);
  CHECK(s.c.outputs().eventSeq == (uint8_t)(seq + 1));
  s.step(800);
  s.tap(s.in.down);  // again: refused again, and said again
  CHECK(s.c.outputs().eventSeq == (uint8_t)(seq + 2));
  // Shifter clicked twice with MANUAL off.
  Sim a;
  enterMoving(a, 3, 43.5f, false);
  a.tap(a.in.up);
  CHECK(a.c.outputs().event == EV_DENY_NOT_MANUAL);
  const uint8_t q = a.c.outputs().eventSeq;
  a.tap(a.in.up);
  CHECK(a.c.outputs().eventSeq == (uint8_t)(q + 1));
  // Waiting for a stop: one NEED_SYNC, not a new one every 10 ms.
  Sim w;
  w.in.ecuSenseValid = false;
  w.in.speedMph = 37.3f;
  w.arm();
  CHECK(w.c.outputs().event == EV_NEED_SYNC);
  const uint8_t n = w.c.outputs().eventSeq;
  w.step(1000);
  CHECK(w.c.outputs().eventSeq == n);
  // Two automatic upshifts at the redline in a row (ECO): both announced.
  Sim r;
  r.arm();
  r.autoRpm = false;
  r.in.speedMph = 10;
  r.in.engineRpm = cfg::REV_LIMIT_UPSHIFT_RPM + 200;
  r.step(cfg::REV_LIMIT_HOLD_MS + 50);
  CHECK(r.c.outputs().gear == 2 && r.c.outputs().event == EV_REV_LIMIT_UPSHIFT);
  const uint8_t u = r.c.outputs().eventSeq;
  r.in.speedMph = 30;
  r.step(cfg::MIN_SHIFT_INTERVAL_MS + cfg::REV_LIMIT_HOLD_MS + 50);
  CHECK(r.c.outputs().gear == 3);
  CHECK(r.c.outputs().eventSeq == (uint8_t)(u + 1));
}

static void test_stall_in_d_is_not_a_stuck_lever() {
  std::printf("engine stalls in D and catches again: no LEVER SENSOR fault\n");
  Sim s;
  s.autoRpm = false;
  // A normal start in P: the battery dips under the starter.
  s.in.leverInD = false;
  s.in.engineRpm = 0;
  s.in.batteryV = 12.4f;
  s.step(cfg::ENGINE_OFF_MS + 500);
  s.in.engineRpm = 250;
  s.in.batteryV = 10.2f;
  s.step(600);
  s.in.engineRpm = 800;
  s.in.batteryV = 14.2f;
  s.step(500);
  // Into D; the engine stalls at a light...
  s.in.leverInD = true;
  s.step(500);
  s.in.engineRpm = 0;
  s.in.batteryV = 12.6f;
  s.step(cfg::ENGINE_OFF_MS + 500);
  // ...and catches again on its own (no starter, no dip).
  s.in.engineRpm = 800;
  s.in.batteryV = 14.0f;
  s.step(500);
  CHECK(!(s.c.outputs().faults & FAULT_LEVER));
}

static void test_millis_rollover() {
  std::printf("49.7 days of power: the millisecond clock wraps mid-drive, nothing notices\n");
  Sim s;
  s.in.nowMs = 0xFFFFFFFFu - 2000u;  // 2 s before the wrap
  s.c = Controller();
  s.c.begin(s.in.nowMs, false);
  s.step(500);
  CHECK(s.c.outputs().warnings == 0);
  CHECK(s.c.outputs().faults == 0);
  s.arm();
  CHECK(s.c.outputs().control == CTRL_AUTO);
  s.in.tpsPct = 25;
  s.cruise(45);  // takes us through the wrap
  CHECK(s.in.nowMs < 100000u);
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(s.c.outputs().gear == 4);
  CHECK(s.c.outputs().faults == 0);
  CHECK(!(s.c.outputs().warnings & WARN_BUTTON_STUCK));
}

static void test_fault_thresholds_exact() {
  std::printf("fault limits: exactly at the limit is still fine, past it is a fault\n");
  struct { float v; bool fault; } supply[] = {
      {cfg::SUPPLY_MIN_V, false}, {cfg::SUPPLY_MIN_V - 0.05f, true},
      {cfg::SUPPLY_MAX_V, false}, {cfg::SUPPLY_MAX_V + 0.05f, true}};
  for (auto& k : supply) {
    Sim s;
    s.in.batteryV = k.v;
    s.step(cfg::SUPPLY_FAULT_MS + 100);
    CHECK(((s.c.outputs().faults & FAULT_SUPPLY) != 0) == k.fault);
  }
  // Solenoid current, commanded on (1st: S1 on), and off (S2).
  struct { float on, off; bool fault; } amps[] = {
      {cfg::SOL_MIN_A, 0, false}, {cfg::SOL_MIN_A - 0.05f, 0, true},
      {cfg::SOL_OVER_A, 0, false}, {cfg::SOL_OVER_A + 0.05f, 0, true},
      {0.95f, cfg::SOL_OFF_MAX_A, false}, {0.95f, cfg::SOL_OFF_MAX_A + 0.05f, true}};
  for (auto& k : amps) {
    Sim s;
    s.in.solSenseValid = true;
    s.arm();
    CHECK(s.c.outputs().gear == 1 && s.c.outputs().s1 && !s.c.outputs().s2);
    s.in.solAmps[0] = k.on;
    s.in.solAmps[1] = k.off;
    s.in.solAmps[2] = 0;
    s.step(cfg::SOL_FAULT_MS + 100);
    CHECK(((s.c.outputs().faults & FAULT_SOLENOID) != 0) == k.fault);
  }
}


static void test_stuck_button_timing() {
  std::printf("stuck shifter switch: timed from the press, either one, after any drive\n");
  for (int which = 0; which < 2; which++) {
    Sim s;
    s.step(30000);  // half a minute after key-on
    bool& b = which ? s.in.down : s.in.up;
    b = true;
    s.step(500);
    CHECK(!(s.c.outputs().warnings & WARN_BUTTON_STUCK));  // a press, not a stuck switch
    s.step(cfg::STUCK_BUTTON_MS - 1000);
    CHECK(!(s.c.outputs().warnings & WARN_BUTTON_STUCK));
    s.step(1000);
    CHECK(s.c.outputs().warnings & WARN_BUTTON_STUCK);
    b = false;
    s.step(50);
    CHECK(!(s.c.outputs().warnings & WARN_BUTTON_STUCK));
  }
}

static void test_map_edges() {
  std::printf("shift maps at their edges: full throttle, past it, and gears with no shift\n");
  for (int m = 0; m < 2; m++) {
    for (int g = 1; g <= 3; g++) {
      const float wot = cfg::UPSHIFT_MPH[m][g - 1][cfg::TPS_POINTS - 1];
      CHECK(upshiftMph((DriveMode)m, g, 100.0f) == wot);
      CHECK(upshiftMph((DriveMode)m, g, 150.0f) == wot);
      CHECK(upshiftMph((DriveMode)m, g, 0.0f) == cfg::UPSHIFT_MPH[m][g - 1][0]);
    }
    CHECK(upshiftMph((DriveMode)m, 4, 50.0f) > 1e8f);   // no 5th
    CHECK(upshiftMph((DriveMode)m, 0, 50.0f) > 1e8f);
    CHECK(downshiftMph((DriveMode)m, 1, 50.0f) < 0.0f);  // nothing below 1st
    CHECK(downshiftMph((DriveMode)m, 5, 50.0f) < 0.0f);
  }
}

static void test_atf_probe_lost_while_hot() {
  std::printf("ATF probe lost while hot: the hot state clears, the probe warning shows\n");
  Sim s;
  s.arm();
  s.in.atfC = cfg::ATF_HOT_C + 5;
  s.step(100);
  CHECK(s.c.outputs().warnings & WARN_ATF_HOT);
  s.in.atfValid = false;
  s.in.atfC = 0;
  s.step(100);
  CHECK(!(s.c.outputs().warnings & WARN_ATF_HOT));
  CHECK(s.c.outputs().warnings & WARN_ATF_SENSOR);
  s.in.atfValid = true;  // back, and cool: no stale hot state
  s.in.atfC = 80;
  s.step(100);
  CHECK(!(s.c.outputs().warnings & WARN_ATF_HOT));
}

static void test_reentry_starts_fresh() {
  std::printf("take-over again after a hand-back: no leftover gear hold\n");
  Sim s;
  s.select(false, true, false);
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(62.1);
  s.step(1500);
  s.in.tpsPct = 0;
  s.in.brake = true;
  s.rampTo(37.3, 9.3f);
  s.in.brake = false;
  s.step(200);
  CHECK(s.c.outputs().gearHold);  // SPORT holding 2nd after braking
  s.in.customRequested = false;   // ON/OFF off...
  s.step(cfg::ARM_OFF_MIN_MS + 50);
  s.setEcuGear(2);
  s.in.customRequested = true;    // ...and on again, rolling in 2nd
  s.step(20);
  CHECK(s.c.outputs().control == CTRL_AUTO);
  CHECK(!s.c.outputs().gearHold);
}

static void test_corner_hold_ends_when_gyro_lost() {
  std::printf("corner hold ends at once if the gyro reading is lost mid-corner\n");
  Sim s;
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(24.9);
  s.in.latValid = true;
  s.in.latG = 0.4f;
  s.step(50);
  CHECK(s.c.outputs().cornerHold);
  s.in.latValid = false;
  s.step(20);
  CHECK(!s.c.outputs().cornerHold);
  s.in.latValid = true;  // back, straight: no stale corner
  s.in.latG = 0.0f;
  s.step(20);
  CHECK(!s.c.outputs().cornerHold);
}

static void test_brake_downshift_rpm_limit() {
  std::printf("SPORT brake downshift never lands above its rpm limit\n");
  Sim s;
  s.select(false, true, false);
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(62.1);
  s.step(1500);
  s.in.tpsPct = 0;
  s.in.brake = true;
  uint8_t was = s.c.outputs().gear;
  bool sawDownshift = false;
  for (float v = 62.1f; v > 30.0f; v -= 0.093f) {  // 9.3 mph/s
    s.in.speedMph = v;
    s.step(10);
    const uint8_t g = s.c.outputs().gear;
    if (g < was) {
      sawDownshift = true;
      CHECK(v * cfg::RPM_PER_MPH[g] <= cfg::BRAKE_DOWNSHIFT_RPM[MODE_SPORT] + 1.0f);
    }
    was = g;
  }
  CHECK(sawDownshift);
}


// ---------------------------------------------------------------------------
// Third batch from the mutation survivors.

static void test_eco_stab_never_downshifts() {
  std::printf("ECO: a quick pedal stab never downshifts, not even for a moment\n");
  Sim s;
  s.arm();
  s.in.tpsPct = 15;
  s.cruise(43.5);
  s.step(3000);
  CHECK(s.c.outputs().gear == 4);
  s.in.tpsPct = 60;
  for (int i = 0; i < 30; i++) { s.step(10); CHECK(s.c.outputs().gear == 4); }
  s.in.tpsPct = 15;
  for (int i = 0; i < 200; i++) { s.step(10); CHECK(s.c.outputs().gear == 4); }
}

static void test_cold_state_cleared_when_probe_lost() {
  std::printf("cold start, then the ATF probe is lost: lockup works by the normal rules\n");
  Sim s;
  s.in.atfC = 20;  // cold: no lockup until it warms up
  s.arm();
  s.in.tpsPct = 15;
  s.cruise(55.9);
  s.step(5000);
  CHECK(s.c.outputs().gear == 4);
  CHECK(!s.c.outputs().sl);
  s.in.atfValid = false;  // probe gone: temperature unknown, not "still cold"
  s.step(5000);
  CHECK(s.c.outputs().sl);
}

static void test_weak_battery_idle_is_not_a_start() {
  std::printf("idling in D on a weak battery (under 11.5 V): not taken for an engine start\n");
  Sim s;
  s.arm();
  s.autoRpm = false;
  s.in.engineRpm = 700;
  s.in.batteryV = 11.2f;  // lights on, tired alternator, stopped at a light
  s.step(5000);
  s.in.engineRpm = 900;
  s.step(1000);
  CHECK(!(s.c.outputs().faults & FAULT_LEVER));
  CHECK(s.c.outputs().control == CTRL_AUTO);
}

static void test_brake_downshift_needs_real_braking() {
  std::printf("SPORT brake downshift: only with the brake, and only braking hard\n");
  auto at62 = [](Sim& s) {
    s.select(false, true, false);
    s.arm();
    s.in.tpsPct = 20;
    s.cruise(62.1);
    s.step(1500);
    s.in.tpsPct = 0;
  };
  Sim g;  // gentle braking: 1 mph/s
  at62(g);
  g.in.brake = true;
  g.rampTo(53, 1.0f);
  CHECK(g.c.outputs().gear == 4);
  Sim e;  // slowing hard on a climb, no brake
  at62(e);
  e.rampTo(53, 6.0f);
  CHECK(e.c.outputs().gear == 4);
}

static void test_lift_hold_ends_on_throttle() {
  std::printf("SPORT lift-off hold ends as soon as the throttle comes back\n");
  Sim t;
  t.select(false, true, false);
  t.arm();
  t.in.tpsPct = 70;
  t.rampTo(46.6, 5.0f);
  t.in.tpsPct = 0;
  t.step(100);
  CHECK(t.c.outputs().gearHold);
  t.in.tpsPct = 25;
  t.step(20);
  CHECK(!t.c.outputs().gearHold);
}

static void test_automatic_refusals_are_silent() {
  std::printf("a kickdown the over-rev guard refuses doesn't put NO SHIFT on the screen\n");
  Sim s;
  s.arm();
  s.in.tpsPct = 15;
  s.cruise(40);
  s.step(2000);
  s.in.tpsPct = 100;  // floored at 40 mph: 2nd fits, 1st would over-rev
  for (int i = 0; i < 300; i++) {
    s.step(10);
    CHECK(s.c.outputs().event != EV_DENY_OVERREV && s.c.outputs().event != EV_DENY_BUSY &&
          s.c.outputs().event != EV_DENY_LIMIT);
  }
  CHECK(s.c.outputs().gear == 2);
}

static void test_stock_gear_only_in_d() {
  std::printf("STOCK: the ECU's gear is only shown with the lever in D\n");
  Sim s;
  s.in.leverInD = false;  // P, with the ECU's solenoids saying "1st"
  s.step(100);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(s.c.outputs().gear == 0);
  s.in.leverInD = true;
  s.step(100);
  CHECK(s.c.outputs().gear == 1);
}

static void test_second_corner_waits_again() {
  std::printf("corner hold: after a second corner, straight again waits the full time again\n");
  Sim s;
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(24.9);
  s.in.latValid = true;
  s.in.latG = 0.35f;
  s.step(500);
  s.in.latG = 0.0f;  // short straight
  s.step(cfg::CORNER_EXIT_MS + 200);
  CHECK(!s.c.outputs().cornerHold);
  s.in.latG = -0.35f;  // the next corner
  s.step(500);
  CHECK(s.c.outputs().cornerHold);
  s.in.latG = 0.0f;
  s.step(cfg::CORNER_EXIT_MS - 200);
  CHECK(s.c.outputs().cornerHold);  // not straight long enough yet
  s.step(400);
  CHECK(!s.c.outputs().cornerHold);
}

static void test_mode_grace_at_key_on() {
  std::printf("key-on with no mode switch on: a second's grace, then MODE ERROR\n");
  Sim s;
  s.select(false, false, false);
  s.step(cfg::MODE_SWITCH_GRACE_MS - 200);
  CHECK(s.c.outputs().modeSelect == SEL_OK);
  s.step(400);
  CHECK(s.c.outputs().modeSelect == SEL_NONE);
}

static void test_manual_auto_downshifts_announced() {
  std::printf("MANUAL slowing down: every automatic downshift is announced again\n");
  Sim s;
  s.armManual();
  s.cruise(18.6); s.tap(s.in.up); s.step(800);
  s.cruise(37.3); s.tap(s.in.up); s.step(800);
  s.cruise(55.9); s.tap(s.in.up); s.step(800);
  CHECK(s.c.outputs().gear == 4);
  int announced = 0;
  uint8_t seq = s.c.outputs().eventSeq;
  s.in.brake = true;
  for (float v = 56; v > 0; v -= 0.08f) {
    s.in.speedMph = v;
    s.step(10);
    if (s.c.outputs().eventSeq != seq) {
      seq = s.c.outputs().eventSeq;
      if (s.c.outputs().event == EV_AUTO_DOWNSHIFT) announced++;
    }
  }
  CHECK(announced == 3);  // 4->3, 3->2, 2->1
}

static void test_quick_reentry_shift() {
  std::printf("ON/OFF off and on again quickly: the first shift isn't held up by the last one\n");
  Sim s;
  s.select(false, false, true);
  enterMoving(s, 3, 43.5f, false);
  s.tap(s.in.up);
  CHECK(s.c.outputs().gear == 4);
  s.in.customRequested = false;  // 150 ms off, on again
  s.step(150);
  s.setEcuGear(3);
  s.in.customRequested = true;
  s.step(20);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  CHECK(s.c.outputs().gear == 3);
  s.tap(s.in.up);  // within 700 ms of the earlier shift, but a new take-over
  CHECK(s.c.outputs().gear == 4);
}

int main() {
  test_solenoid_table();
  test_boot_requires_switch_cycle();
  test_boot_glitch_does_not_arm();
  test_watchdog_flag();
  test_entry_sync();
  test_lever_out_of_d();
  test_no_autorepeat_and_interval();
  test_downshift_overrev_guard();
  test_upshift_too_slow();
  test_manual_auto_downshift_to_stop();
  test_manual_switch();
  test_auto_schedule();
  test_kickdown_is_immediate();
  test_corner_hold();
  test_brake_downshift();
  test_coast_unlock();
  test_eco_throttle_smoothing();
  test_sport_tipin_release();
  test_manual_is_sport();
  test_sport_lift_hold();
  test_eco_early_lockup();
  test_highway_lockup();
  test_lockup_rules();
  test_hot_atf_widens_lockup();
  test_vss_faults();
  test_ratio_fault();
  test_supply_and_tps_faults();
  test_rev_limit_upshift();
  test_mode_switch_fault_and_stuck_button();
  test_entry_plausibility();
  test_onoff_two_poles_and_relay();
  test_lever_stuck_at_start();
  test_debouncer();
  test_ds18b20_decode();
  test_maps_are_sane();
  test_arming_glitch_after_drive();
  test_stock_outputs();
  test_entry_never_overrevs();
  test_lockup_released_before_shift();
  test_first_shift_right_after_takeover();
  test_events_repeat();
  test_stall_in_d_is_not_a_stuck_lever();
  test_millis_rollover();
  test_fault_thresholds_exact();
  test_stuck_button_timing();
  test_map_edges();
  test_atf_probe_lost_while_hot();
  test_reentry_starts_fresh();
  test_corner_hold_ends_when_gyro_lost();
  test_brake_downshift_rpm_limit();
  test_eco_stab_never_downshifts();
  test_cold_state_cleared_when_probe_lost();
  test_weak_battery_idle_is_not_a_start();
  test_brake_downshift_needs_real_braking();
  test_lift_hold_ends_on_throttle();
  test_automatic_refusals_are_silent();
  test_stock_gear_only_in_d();
  test_second_corner_waits_again();
  test_mode_grace_at_key_on();
  test_manual_auto_downshifts_announced();
  test_quick_reentry_shift();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
