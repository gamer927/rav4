// Host unit tests for firmware/tcu/tcu_logic.cpp. Run: make test
#include <cmath>
#include <cstdio>

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
        const float rpm = in.speedKmh * cfg::RPM_PER_KMH[g];
        in.engineRpm = rpm < 750 ? 750 : rpm;
      }
      o = &c.step(in);
    }
    return *o;
  }
  // Change speed at a physically plausible rate (default 20 km/h per second).
  const Outputs& rampTo(float kmh, float kmhPerS = 20.0f) {
    const float stepKmh = kmhPerS / 100.0f;
    while (in.speedKmh != kmh) {
      if (in.speedKmh < kmh) in.speedKmh = in.speedKmh + stepKmh > kmh ? kmh : in.speedKmh + stepKmh;
      else in.speedKmh = in.speedKmh - stepKmh < kmh ? kmh : in.speedKmh - stepKmh;
      step(10);
    }
    return c.outputs();
  }
  // Flip the master switch STOCK -> CUSTOM.
  const Outputs& arm() {
    in.customRequested = false;
    step(50);
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
  void cruise(float kmh) { rampTo(kmh); }
  // Arm, then drop into MANUAL at a standstill (a refused [-] in 1st).
  const Outputs& armManual() {
    arm();
    return tap(in.down);
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
  s.cruise(70);
  s.arm();
  CHECK(s.c.outputs().control == CTRL_AUTO || s.c.outputs().control == CTRL_MANUAL);
  // AUTO may legitimately upshift later, but at entry the gear must match the ECU.
  Sim s2;
  s2.setEcuGear(3);
  s2.in.ecuSL = true;
  s2.in.speedKmh = 70;
  s2.in.customRequested = false;
  s2.step(50);
  s2.in.customRequested = true;
  const Outputs& o = s2.step(10);
  CHECK(o.control != CTRL_STOCK);
  CHECK(o.gear == 3);
  CHECK(!o.s1 && o.s2);
  CHECK(o.sl);  // lockup carried over bumplessly

  Sim s3;
  s3.in.ecuSenseValid = false;
  s3.in.speedKmh = 60;
  s3.arm();
  CHECK(s3.c.outputs().control == CTRL_STOCK);
  CHECK(s3.c.outputs().event == EV_NEED_SYNC);
  s3.in.speedKmh = 0;  // car stopped: now it can engage in 1st
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
  s.cruise(40);
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
  s.cruise(30);
  s.tap(s.in.up); s.step(800);
  s.cruise(60);
  s.tap(s.in.up); s.step(800);
  s.cruise(100);
  s.tap(s.in.up); s.step(800);
  CHECK(s.c.outputs().gear == 4);
  // Just above the speed where 3rd would exceed the downshift rpm limit.
  s.cruise(cfg::MAX_DOWNSHIFT_RPM / cfg::RPM_PER_KMH[3] + 5);
  s.tap(s.in.down);
  CHECK(s.c.outputs().gear == 4);
  CHECK(s.c.outputs().event == EV_DENY_OVERREV);
  s.step(800);
  s.cruise(110);  // 3rd well under the limit
  s.tap(s.in.down);
  s.step(300);
  CHECK(s.c.outputs().gear == 3);
  s.step(800);
  s.cruise(80);  // 2nd under the limit
  s.tap(s.in.down);
  s.step(300);
  CHECK(s.c.outputs().gear == 2);
  s.step(800);
  s.cruise(80);  // 1st would be far over the limit
  s.tap(s.in.down);
  CHECK(s.c.outputs().gear == 2);
  CHECK(s.c.outputs().event == EV_DENY_OVERREV);
  s.step(800);
  s.cruise(cfg::FIRST_GEAR_MAX_KMH + 2);  // under rpm limit but over hard cap
  s.tap(s.in.down);
  CHECK(s.c.outputs().gear == 2);
  s.step(800);
  s.cruise(30);
  s.tap(s.in.down);
  CHECK(s.c.outputs().gear == 1);
}

static void test_upshift_too_slow() {
  std::printf("upshift into forced-downshift zone is refused\n");
  Sim s;
  s.arm();
  s.cruise(5);
  s.tap(s.in.up);
  CHECK(s.c.outputs().gear == 1);
  CHECK(s.c.outputs().event == EV_DENY_TOO_SLOW);
}

static void test_manual_auto_downshift_to_stop() {
  std::printf("MANUAL: forced downshifts while stopping, ends in 1st\n");
  Sim s;
  s.armManual();
  float v = 30;
  s.cruise(v); s.tap(s.in.up); s.step(800);
  s.cruise(60); s.tap(s.in.up); s.step(800);
  s.cruise(90); s.tap(s.in.up); s.step(800);
  CHECK(s.c.outputs().gear == 4);
  s.in.brake = true;
  for (v = 90; v > 0; v -= 0.25f) {  // ~25 km/h/s braking
    s.in.speedKmh = v;
    s.step(10);
    CHECK(!s.c.outputs().sl);
  }
  s.in.speedKmh = 0;
  s.step(3000);
  CHECK(s.c.outputs().gear == 1);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
}

static void test_long_press_resumes_auto() {
  std::printf("long-press [+] returns MANUAL -> AUTO\n");
  Sim s;
  s.arm();
  s.cruise(40);
  s.tap(s.in.up);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  s.step(800);
  s.in.up = true;
  s.step(cfg::LONG_PRESS_MS + 100);
  s.in.up = false;
  s.step(20);
  CHECK(s.c.outputs().control == CTRL_AUTO);
}

static void test_auto_schedule() {
  std::printf("AUTO: upshifts with speed; ECO earlier than SPORT; kickdown\n");
  auto upAt = [](bool eco, bool sport) {
    Sim s;
    s.in.eco = eco;
    s.in.sport = sport;
    s.arm();
    s.in.tpsPct = 25;
    for (float v = 0; v < 150; v += 0.5f) {
      s.in.speedKmh = v;
      s.step(50);
      if (s.c.outputs().gear == 2) return v;
    }
    return -1.0f;
  };
  const float eco = upAt(true, false), norm = upAt(false, false), sport = upAt(false, true);
  std::printf("  1->2 at 25%%: eco %.1f  normal %.1f  sport %.1f km/h\n", eco, norm, sport);
  CHECK(eco > 0 && norm > 0 && sport > 0);
  CHECK(eco < norm && norm < sport);

  Sim s;
  s.arm();
  s.in.tpsPct = 20;
  for (float v = 0; v <= 100; v += 0.5f) {
    s.in.speedKmh = v;
    s.step(50);
  }
  s.step(3000);
  CHECK(s.c.outputs().gear == 4);
  s.in.tpsPct = 100;  // kickdown at 100 km/h -> 3rd, but not on to 2nd
  s.step(3000);
  CHECK(s.c.outputs().gear == 3);
}

static void test_kickdown_is_immediate() {
  std::printf("kickdown right after a shift goes 4->3->2 without the dwell\n");
  Sim s;
  s.in.sport = true;
  s.cruise(65);
  s.setEcuGear(4);
  s.arm();
  CHECK(s.c.outputs().gear == 4);
  s.step(300);  // only 0.3 s in 4th: a part-throttle downshift would still wait
  s.in.tpsPct = 100;
  s.step(20);
  CHECK(s.c.outputs().gear == 3);
  s.step(cfg::MIN_SHIFT_INTERVAL_MS);
  CHECK(s.c.outputs().gear == 2);  // 2nd at 65 km/h is under the rpm limit
  s.step(2000);
  CHECK(s.c.outputs().gear == 2);  // and it doesn't go on to 1st (45 km/h cap)

  // Part throttle in the same spot still respects the dwell.
  Sim t;
  t.in.sport = true;
  t.cruise(65);
  t.setEcuGear(4);
  t.arm();
  t.step(300);
  t.in.tpsPct = 70;
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
  s.cruise(40);
  CHECK(s.c.outputs().gear == 2);
  s.in.latValid = true;
  s.in.latG = -0.35f;  // turning left
  s.step(50);
  CHECK(s.c.outputs().cornerHold);
  s.cruise(60);  // past the 2->3 point, but mid-corner
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
  t.cruise(40);
  t.in.latValid = true;
  t.in.latG = 0.3f;
  t.step(20);
  t.cruise(60);
  t.step(cfg::CORNER_HOLD_MAX_MS - 2000);
  CHECK(t.c.outputs().gear == 2);
  t.step(2500);
  CHECK(t.c.outputs().gear == 3);

  // The driver's own shift is always honoured.
  Sim u;
  u.arm();
  u.in.tpsPct = 20;
  u.cruise(40);
  u.in.latValid = true;
  u.in.latG = 0.4f;
  u.step(20);
  u.tap(u.in.up);
  CHECK(u.c.outputs().gear == 3);

  // No gyro data: no hold.
  Sim v;
  v.arm();
  v.in.tpsPct = 20;
  v.cruise(40);
  v.in.latValid = false;
  v.in.latG = 0.5f;
  v.cruise(60);
  v.step(1500);
  CHECK(v.c.outputs().gear == 3);
}

static void test_brake_downshift() {
  std::printf("SPORT brake downshift: gear ready for the exit, held until throttle\n");
  Sim s;
  s.in.sport = true;
  s.arm();
  s.in.tpsPct = 20;
  s.cruise(100);
  s.step(1500);
  CHECK(s.c.outputs().gear == 4);
  s.in.tpsPct = 0;
  s.in.brake = true;
  s.rampTo(85, 15.0f);  // hard braking
  CHECK(s.c.outputs().gear == 3);
  s.rampTo(60, 15.0f);
  CHECK(s.c.outputs().gear == 2);
  CHECK(s.in.speedKmh * cfg::RPM_PER_KMH[2] <= cfg::BRAKE_DOWNSHIFT_RPM[2]);
  s.rampTo(50, 15.0f);
  CHECK(s.c.outputs().gear == 2);  // 1st only below the 4200 rpm / 45 km/h line
  s.in.brake = false;              // corner entry: off the brake, off the throttle
  s.step(cfg::BRAKE_HOLD_MS - 500);
  CHECK(s.c.outputs().gear == 2);  // the 0 % map would have gone back to 3rd at 50
  s.in.tpsPct = 40;                // exit: back on the throttle
  s.step(100);
  CHECK(s.c.outputs().gear == 2);  // 40 % in SPORT holds 2nd at 50 km/h

  // Hold also ends on its own if the driver just coasts.
  Sim t;
  t.in.sport = true;
  t.arm();
  t.in.tpsPct = 20;
  t.cruise(100);
  t.step(1500);
  t.in.tpsPct = 0;
  t.in.brake = true;
  t.rampTo(60, 15.0f);
  CHECK(t.c.outputs().gear == 2);
  t.in.brake = false;
  t.step(cfg::BRAKE_HOLD_MS + 1500);
  CHECK(t.c.outputs().gear == 3);

  // NORMAL doesn't brake-downshift; SPORT doesn't do it mid-corner.
  Sim n;
  n.arm();
  n.in.tpsPct = 20;
  n.cruise(100);
  n.step(1500);
  n.in.tpsPct = 0;
  n.in.brake = true;
  n.rampTo(60, 15.0f);
  CHECK(n.c.outputs().gear == 4);
  Sim c;
  c.in.sport = true;
  c.arm();
  c.in.tpsPct = 20;
  c.cruise(100);
  c.step(1500);
  c.in.latValid = true;
  c.in.latG = 0.4f;
  c.in.tpsPct = 0;
  c.in.brake = true;
  c.rampTo(70, 10.0f);
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
    s.cruise(100);
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

static void test_lockup_rules() {
  std::printf("lockup: gear/speed/brake/cold/hot/shift sequencing\n");
  Sim s;
  s.armManual();
  s.cruise(30); s.tap(s.in.up); s.step(800);
  s.cruise(55); s.tap(s.in.up); s.step(800);
  s.cruise(80); s.tap(s.in.up);
  s.in.tpsPct = 20;
  s.cruise(80);
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
  s.cruise(80);
  s.step(1500);
  CHECK(s.c.outputs().sl);

  s.rampTo(40);  // below every non-hot lockup threshold
  CHECK(!s.c.outputs().sl);
  CHECK(s.c.outputs().control == CTRL_MANUAL);

  // Cold ATF inhibits lockup.
  s.in.atfC = 20;
  s.cruise(80);
  s.step(3000);
  CHECK(!s.c.outputs().sl);
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  s.in.atfC = 60;
  s.step(1500);
  CHECK(s.c.outputs().sl);
}

static void test_hot_atf_widens_lockup() {
  std::printf("directive 2 corrected: hot ATF widens lockup instead of cancelling it\n");
  auto lockedAt = [](float atf, float kmh, float tps) {
    Sim s;
    s.in.atfC = atf;
    s.armManual();
    s.cruise(30); s.tap(s.in.up); s.step(800);
    s.cruise(55); s.tap(s.in.up); s.step(800);
    s.cruise(kmh);
    s.in.tpsPct = tps;
    s.step(3000);
    return s.c.outputs().gear == 3 && s.c.outputs().sl;
  };
  CHECK(!lockedAt(80, 45, 20));   // NORMAL: 45 km/h is below the lockup speed
  CHECK(lockedAt(115, 45, 20));   // hot: locked to stop converter heating
  CHECK(!lockedAt(80, 80, 70));   // NORMAL: heavy throttle unlocks
  CHECK(lockedAt(115, 80, 70));   // hot: stays locked
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
  s.cruise(30); s.tap(s.in.up); s.step(800);
  s.cruise(60); s.tap(s.in.up); s.step(800);
  CHECK(s.c.outputs().control != CTRL_STOCK);
  s.in.speedKmh = 0;  // signal collapses
  s.step(200);
  CHECK(s.c.outputs().control == CTRL_STOCK);
  CHECK(s.c.outputs().faults & FAULT_VSS);
  CHECK(s.c.outputs().event == EV_FAULT);
  s.in.speedKmh = 60;
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
  s.cruise(30); s.tap(s.in.up); s.step(800);
  s.cruise(55); s.tap(s.in.up); s.step(800);
  s.cruise(80); s.tap(s.in.up);
  s.in.tpsPct = 20;
  s.cruise(80);
  s.step(2500);
  CHECK(s.c.outputs().sl && s.c.outputs().gear == 4);
  s.autoRpm = false;
  s.in.engineRpm = 80 * cfg::RPM_PER_KMH[3];  // car is really in 3rd
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
  std::printf("MANUAL: forced upshift at the rev limit\n");
  Sim s;
  s.arm();
  s.tap(s.in.down);  // enter MANUAL (refused shift, already 1st)
  CHECK(s.c.outputs().control == CTRL_MANUAL);
  s.in.speedKmh = 52;
  s.autoRpm = false;
  s.in.engineRpm = cfg::REV_LIMIT_UPSHIFT_RPM + 200;
  s.step(cfg::REV_LIMIT_HOLD_MS + 50);
  CHECK(s.c.outputs().gear == 2);
  CHECK(s.c.outputs().event == EV_REV_LIMIT_UPSHIFT);
}

static void test_mode_switch_fault_and_stuck_button() {
  std::printf("ECO+SPORT together -> NORMAL + warning; stuck button warning\n");
  Sim s;
  s.in.eco = s.in.sport = true;
  CHECK(s.step(20).mode == MODE_NORMAL);
  CHECK(s.c.outputs().warnings & WARN_MODE_SWITCH);
  s.in.eco = s.in.sport = false;
  s.in.down = true;
  s.step(cfg::STUCK_BUTTON_MS + 100);
  CHECK(s.c.outputs().warnings & WARN_BUTTON_STUCK);
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
  for (int m = 0; m < 3; m++) {
    for (uint8_t g = 1; g <= 3; g++) {
      for (int tps = 0; tps <= 100; tps += 5) {
        const float up = upshiftKmh((DriveMode)m, g, tps);
        const float down = downshiftKmh((DriveMode)m, g + 1, tps);
        CHECK(down < up);
        CHECK(up * cfg::RPM_PER_KMH[g] < cfg::REV_LIMIT_UPSHIFT_RPM);
        // Auto upshift must clear the MANUAL forced-downshift margin.
        CHECK(up >= cfg::MANUAL_MIN_KMH[g + 1] + cfg::UPSHIFT_MARGIN_KMH);
        if (tps > 0) CHECK(up >= upshiftKmh((DriveMode)m, g, tps - 5));
      }
    }
  }
}

int main() {
  test_solenoid_table();
  test_boot_requires_switch_cycle();
  test_watchdog_flag();
  test_entry_sync();
  test_lever_out_of_d();
  test_no_autorepeat_and_interval();
  test_downshift_overrev_guard();
  test_upshift_too_slow();
  test_manual_auto_downshift_to_stop();
  test_long_press_resumes_auto();
  test_auto_schedule();
  test_kickdown_is_immediate();
  test_corner_hold();
  test_brake_downshift();
  test_coast_unlock();
  test_lockup_rules();
  test_hot_atf_widens_lockup();
  test_vss_faults();
  test_ratio_fault();
  test_supply_and_tps_faults();
  test_rev_limit_upshift();
  test_mode_switch_fault_and_stuck_button();
  test_debouncer();
  test_ds18b20_decode();
  test_maps_are_sane();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
