// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Host unit tests for the built-in test (firmware/tcu/self_test.cpp) and the
// continuous solenoid-current check in the controller. Run: make test
#include <cstdio>

#include "../firmware/tcu/config.h"
#include "../firmware/tcu/self_test.h"
#include "../firmware/tcu/tcu_logic.h"

using namespace tcu;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    g_checks++;                                                     \
    if (!(cond)) {                                                  \
      g_failures++;                                                 \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

// A parked car, engine off, ignition on, with a simulated electrical harness:
// a solenoid draws current only when its driver is on AND the relay routes the
// driver to it (unless the harness is faulted).
struct Bench {
  Inputs in;
  SelfTest t;
  bool solenoidOpen[3] = {false, false, false};
  bool relayWelded = false;
  bool senseWired = true;
  // The ON/OFF switch: pole 1 is read (switchOn), pole 2 powers the relay coils.
  bool switchOn = true;
  bool coilFuseBlown = false;   // pole 2 open: no coil power even when ON
  bool cutBroken = false;       // pole 2 shorted: coil power even when OFF

  Bench() {
    in.nowMs = 5000;
    in.rpmValid = true;
    in.tpsValid = true;
    in.tpsPct = 0;
    in.atfValid = true;
    in.atfC = 20;
    in.batteryV = 12.5f;  // ignition on, engine off: not charging
    in.imuPresent = true;
    in.switchOn = true;
    in.coilSenseValid = true;
    in.coilPowered = true;
    in.solSenseValid = true;
  }
  // Real parts take time: a G2RL relay's contact closes up to 15 ms after
  // its coil is powered, and a solenoid's current takes about 20 ms to come
  // up (its inductance). A check made too early sees no current.
  static constexpr uint32_t RELAY_OPERATE_MS = 15, SOLENOID_RISE_MS = 20;
  uint32_t coilOnSince = 0, driveSince[3] = {0, 0, 0};
  bool coilWasOn = false, driveWas[3] = {false, false, false};
  void physics() {
    const TestOutputs& o = t.outputs();
    const bool drive[3] = {o.s1, o.s2, o.sl};
    in.switchOn = switchOn;
    in.coilPowered = (switchOn || cutBroken) && !coilFuseBlown;
    const bool coil = o.relay && in.coilPowered;
    if (coil && !coilWasOn) coilOnSince = in.nowMs;
    coilWasOn = coil;
    const bool routed = (coil && in.nowMs - coilOnSince >= RELAY_OPERATE_MS) || relayWelded;
    for (int i = 0; i < 3; i++) {
      if (drive[i] && !driveWas[i]) driveSince[i] = in.nowMs;
      driveWas[i] = drive[i];
      const bool flowing = drive[i] && routed && !solenoidOpen[i];
      const uint32_t since = in.nowMs - (driveSince[i] > coilOnSince || relayWelded ? driveSince[i] : coilOnSince);
      in.solAmps[i] = flowing && since >= SOLENOID_RISE_MS ? 0.95f : flowing ? 0.3f : 0.0f;
    }
    in.solSenseValid = senseWired;
  }
  void step(uint32_t ms, bool edge = false) {
    for (uint32_t x = 0; x < ms; x += 10) {
      in.nowMs += 10;
      physics();
      t.step(in, edge && x == 0);
    }
  }
  // Run IBIT, answering each prompt correctly, up to the watchdog step.
  void runAll() {
    if (!t.active()) CHECK(t.start(in));
    while (t.active() && t.phase() != PH_PROMPT) step(10);
    for (int p = 0; p < 6 && t.phase() == PH_PROMPT; p++) {
      const TestPrompt pr = t.prompt();
      in.up = pr == PR_UP;
      in.down = pr == PR_DOWN;
      in.eco = pr == PR_ECO;
      in.sport = pr == PR_SPORT;
      in.manual = pr == PR_MANUAL;
      in.brake = pr == PR_BRAKE;
      step(20);
    }
    in.up = in.down = in.eco = in.sport = in.manual = in.brake = false;
    // Last prompt: turn ON/OFF OFF (the hardware cut), then on again after.
    while (t.active() && t.phase() == PH_CUT_ARM) step(10);
    if (t.prompt() == PR_ONOFF_OFF) {
      switchOn = false;
      step(cfg::IBIT_RELAY_SETTLE_MS + 50);
    }
  }
};

static void test_pbit() {
  std::printf("PBIT: passive checks at power-up\n");
  Bench b;
  b.t.runPowerOn(b.in, false);
  CHECK(b.t.result(T_SUPPLY) == TR_PASS);
  CHECK(b.t.result(T_TPS) == TR_PASS);
  CHECK(b.t.result(T_SWITCHES) == TR_PASS);
  CHECK(b.t.result(T_LAST_RESET) == TR_PASS);
  CHECK(b.t.result(T_CALIBRATED) == (cfg::CALIBRATED ? TR_PASS : TR_CHECK));
  CHECK(b.t.active());   // the key-on relay-release check runs next...
  CHECK(!b.t.ibitActive());
  CHECK(b.t.outputs().s1 && b.t.outputs().s2 && b.t.outputs().sl && !b.t.outputs().relay);
  b.step(cfg::IBIT_SOLENOID_ON_MS + 20);
  CHECK(!b.t.active());  // ...for 0.4 s, then everything is off again
  CHECK(!b.t.ibitDone());
  CHECK(!b.t.outputs().s1 && !b.t.outputs().s2 && !b.t.outputs().sl);
  CHECK(b.t.result(T_RELAY_RELEASE) == TR_PASS);
  CHECK(b.t.result(T_ONOFF_CUT) == TR_PASS);

  Bench c;
  c.in.up = true;           // button stuck at boot
  c.in.atfValid = false;    // sender unplugged
  c.t.runPowerOn(c.in, true);
  CHECK(c.t.result(T_SWITCHES) == TR_FAIL);
  CHECK(c.t.result(T_ATF) == TR_FAIL);
  CHECK(c.t.result(T_LAST_RESET) == TR_FAIL);

  // A welded relay is found at every key-on, not only by IBIT.
  Bench w;
  w.relayWelded = true;
  w.t.runPowerOn(w.in, false);
  w.step(cfg::IBIT_SOLENOID_ON_MS + 20);
  CHECK(w.t.result(T_RELAY_RELEASE) == TR_FAIL);
  // Lever in D at key-on: the relay check waits for another day.
  Bench d;
  d.in.leverInD = true;
  d.t.runPowerOn(d.in, false);
  CHECK(!d.t.active());
  CHECK(d.t.result(T_RELAY_RELEASE) == TR_SKIP);
  // The ON/OFF switch's two poles disagree at key-on.
  Bench s;
  s.switchOn = false;
  s.cutBroken = true;
  s.physics();
  s.t.runPowerOn(s.in, false);
  CHECK(s.t.result(T_ONOFF_CUT) == TR_FAIL);
}

static void test_interlocks() {
  std::printf("IBIT interlocks (weight-on-wheels style)\n");
  struct Case { const char* what; void (*apply)(Inputs&); TestInhibit expect; };
  const Case cases[] = {
      {"ON/OFF off (no relay power)", [](Inputs& i) { i.switchOn = false; i.coilPowered = false; }, INH_MASTER},
      {"ON/OFF on, coil power missing", [](Inputs& i) { i.coilPowered = false; }, INH_NO_COIL},
      {"lever in D", [](Inputs& i) { i.leverInD = true; }, INH_LEVER_D},
      {"moving", [](Inputs& i) { i.speedMph = 5; }, INH_MOVING},
      {"engine rpm", [](Inputs& i) { i.engineRpm = 750; }, INH_ENGINE},
      {"charging voltage (no tach)", [](Inputs& i) { i.rpmValid = false; i.batteryV = 14.1f; }, INH_ENGINE},
      {"low battery", [](Inputs& i) { i.batteryV = 10.2f; }, INH_SUPPLY},
  };
  for (const Case& k : cases) {
    Bench b;
    k.apply(b.in);
    CHECK(!b.t.start(b.in));
    CHECK(b.t.inhibit() == k.expect);
    CHECK(!b.t.outputs().relay && !b.t.outputs().s1);
  }
}

static void test_full_ibit_pass() {
  std::printf("IBIT: healthy harness passes every check\n");
  Bench b;
  b.t.runPowerOn(b.in, false);
  CHECK(!b.t.start(b.in));  // not while the key-on relay check is still running
  b.step(cfg::IBIT_SOLENOID_ON_MS + 20);
  CHECK(b.t.start(b.in));
  CHECK(b.t.outputs().lamps || b.t.phase() == PH_LAMPS);
  b.step(20);
  CHECK(b.t.outputs().lamps);
  b.runAll();
  CHECK(b.t.phase() == PH_WATCHDOG);
  CHECK(b.t.outputs().triggerWatchdog);
  CHECK(!b.t.outputs().relay && !b.t.outputs().s1 && !b.t.outputs().s2 && !b.t.outputs().sl);
  for (uint8_t i = T_SOL_S1; i <= T_SOL_SL; i++) {
    CHECK(b.t.result((TestId)i) == TR_PASS);
    CHECK(b.t.value((TestId)i) > 0.9f);
  }
  CHECK(b.t.result(T_RELAY_RELEASE) == TR_PASS);
  CHECK(b.t.result(T_LAMPS) == TR_CHECK);
  for (uint8_t i = T_BTN_UP; i <= T_BRAKE; i++) CHECK(b.t.result((TestId)i) == TR_PASS);
  CHECK(b.t.result(T_ONOFF_CUT) == TR_PASS);

  // Board resets via the watchdog; HAL restores the saved table.
  SelfTest after;
  after.restoreAfterWatchdog(b.t.results(), b.t.values(), true);
  after.runPowerOn(b.in, false);
  CHECK(after.ibitDone());
  CHECK(after.result(T_WATCHDOG) == TR_PASS);
  CHECK(after.result(T_SOL_S2) == TR_PASS);
  const TestSummary s = after.summary();
  CHECK(s.fail == 0);                              // a healthy build: ALL OK
  CHECK(s.check == (cfg::CALIBRATED ? 1 : 2));     // the screen flash, and "not calibrated yet"
}

static void test_one_solenoid_at_a_time() {
  std::printf("IBIT: one solenoid energised at a time, relay held during the pulses\n");
  Bench b;
  CHECK(b.t.start(b.in));
  int maxOn = 0;
  bool driverWithoutRelay = false;
  while (b.t.active() && b.t.phase() != PH_RELEASE_CHECK) {
    b.step(10);
    const TestOutputs& o = b.t.outputs();
    const int on = o.s1 + o.s2 + o.sl;
    maxOn = on > maxOn ? on : maxOn;
    if (on && !o.relay) driverWithoutRelay = true;
  }
  CHECK(maxOn == 1);
  CHECK(!driverWithoutRelay);
}

static void test_detects_faults() {
  std::printf("IBIT: open solenoid, welded relay, unsensed drivers\n");
  Bench b;
  b.solenoidOpen[1] = true;  // S2 wire broken
  b.runAll();
  CHECK(b.t.result(T_SOL_S1) == TR_PASS);
  CHECK(b.t.result(T_SOL_S2) == TR_FAIL);
  CHECK(b.t.value(T_SOL_S2) < 0.1f);

  Bench w;
  w.relayWelded = true;
  w.runAll();
  CHECK(w.t.result(T_RELAY_RELEASE) == TR_FAIL);

  Bench n;
  n.senseWired = false;
  n.runAll();
  CHECK(n.t.result(T_SOL_SL) == TR_CHECK);
  CHECK(n.t.result(T_RELAY_RELEASE) == TR_SKIP);

  // The hardware cut: ON/OFF's power pole shorted, so OFF doesn't drop the relays.
  Bench c;
  c.cutBroken = true;
  c.runAll();
  CHECK(c.t.result(T_ONOFF_CUT) == TR_FAIL);
  CHECK(c.t.value(T_ONOFF_CUT) > 0.9f);  // the solenoid kept drawing current
  CHECK(!c.t.outputs().relay && !c.t.outputs().s1);

  // ON/OFF turned OFF in the middle of the solenoid tests: abort, all off.
  Bench o;
  CHECK(o.t.start(o.in));
  while (o.t.phase() != PH_SOL_ON) o.step(10);
  o.switchOn = false;
  o.step(20);
  CHECK(o.t.aborted());
  CHECK(o.t.inhibit() == INH_MASTER);
  CHECK(!o.t.outputs().relay && !o.t.outputs().s1 && !o.t.outputs().s2 && !o.t.outputs().sl);

}

static void test_prompts_timeout_and_skip() {
  std::printf("IBIT: prompt timeout fails, TEST press skips\n");
  Bench b;
  CHECK(b.t.start(b.in));
  while (b.t.active() && b.t.phase() != PH_PROMPT) b.step(10);
  CHECK(b.t.prompt() == PR_UP);
  b.step(cfg::IBIT_PROMPT_TIMEOUT_MS + 20);
  CHECK(b.t.result(T_BTN_UP) == TR_FAIL);
  CHECK(b.t.prompt() == PR_DOWN);
  b.step(30, true);  // operator presses TEST
  CHECK(b.t.result(T_BTN_DOWN) == TR_SKIP);
  CHECK(b.t.prompt() == PR_ECO);
  b.in.eco = b.in.sport = true;
  b.step(100);
  CHECK(b.t.result(T_SW_ECO) == TR_NONE);  // ECO+SPORT both on doesn't pass "ECO on"
  b.in.sport = false;
  b.step(20);
  CHECK(b.t.result(T_SW_ECO) == TR_PASS);
  CHECK(b.t.prompt() == PR_SPORT);
  b.in.eco = false;
  b.in.sport = b.in.manual = true;  // SPORT and MANUAL on the same core
  b.step(100);
  CHECK(b.t.result(T_SW_SPORT) == TR_NONE);
  b.in.manual = false;
  b.step(20);
  CHECK(b.t.prompt() == PR_MANUAL);
  b.in.sport = false;
  b.in.manual = true;
  b.step(20);
  CHECK(b.t.result(T_SW_MANUAL) == TR_PASS);
  CHECK(b.t.prompt() == PR_BRAKE);
}

static void test_abort() {
  std::printf("IBIT aborts and de-energises everything if the engine starts\n");
  Bench b;
  CHECK(b.t.start(b.in));
  while (b.t.phase() != PH_SOL_ON) b.step(10);
  CHECK(b.t.outputs().relay);
  b.in.engineRpm = 400;  // cranking
  b.step(10);
  CHECK(!b.t.active());
  CHECK(b.t.aborted());
  CHECK(b.t.inhibit() == INH_ENGINE);
  CHECK(!b.t.outputs().relay && !b.t.outputs().s1 && !b.t.outputs().s2 && !b.t.outputs().sl);
}

static void test_cbit_solenoid_fault() {
  std::printf("CBIT: solenoid current mismatch while driving hands back to the ECU\n");
  Controller c;
  Inputs in;
  in.nowMs = 1000;
  in.leverInD = true;
  in.ecuSenseValid = true;
  in.ecuS1 = true;
  in.rpmValid = true;
  in.engineRpm = 750;
  in.tpsValid = true;
  in.atfValid = true;
  in.atfC = 80;
  in.batteryV = 14.2f;
  in.solSenseValid = true;
  in.eco = true;  // one mode switch on
  c.begin(in.nowMs, false);
  bool s1Broken = false;
  auto step = [&](uint32_t ms) {
    for (uint32_t x = 0; x < ms; x += 10) {
      in.nowMs += 10;
      const Outputs& o = c.outputs();
      in.solAmps[0] = o.relayCustom && o.s1 && !s1Broken ? 0.95f : 0.0f;
      in.solAmps[1] = o.relayCustom && o.s2 ? 0.95f : 0.0f;
      in.solAmps[2] = o.relayCustom && o.sl ? 0.95f : 0.0f;
      c.step(in);
    }
  };
  step(cfg::ARM_OFF_MIN_MS + 50);
  in.customRequested = true;
  step(1000);
  CHECK(c.outputs().control == CTRL_AUTO);
  CHECK(c.outputs().s1);  // 1st gear
  CHECK(!(c.outputs().faults & FAULT_SOLENOID));
  s1Broken = true;
  step(cfg::SOL_FAULT_MS + 50);
  CHECK(c.outputs().control == CTRL_STOCK);
  CHECK(c.outputs().faults & FAULT_SOLENOID);
}

static void test_cbit_solenoid_short() {
  std::printf("CBIT: a shorted solenoid wire (over-current) hands back to the ECU\n");
  Controller c;
  Inputs in;
  in.nowMs = 1000;
  in.leverInD = true;
  in.ecuSenseValid = true;
  in.ecuS1 = true;
  in.rpmValid = true;
  in.engineRpm = 750;
  in.tpsValid = true;
  in.atfValid = true;
  in.atfC = 80;
  in.batteryV = 14.2f;
  in.solSenseValid = true;
  in.eco = true;  // one mode switch on
  c.begin(in.nowMs, false);
  float s1On = 0.95f;
  float s2Off = 0.0f;  // current with S2 commanded off: a shorted driver
  auto step = [&](uint32_t ms) {
    for (uint32_t x = 0; x < ms; x += 10) {
      in.nowMs += 10;
      const Outputs& o = c.outputs();
      in.solAmps[0] = o.relayCustom && o.s1 ? s1On : 0.0f;
      in.solAmps[1] = o.relayCustom ? (o.s2 ? 0.95f : s2Off) : 0.0f;
      in.solAmps[2] = o.relayCustom && o.sl ? 0.95f : 0.0f;
      c.step(in);
    }
  };
  step(cfg::ARM_OFF_MIN_MS + 50);
  in.customRequested = true;
  step(1000);
  CHECK(c.outputs().control == CTRL_AUTO);
  CHECK(c.outputs().s1);
  s1On = 1.55f;  // a cold solenoid at charging voltage is still fine
  step(cfg::SOL_FAULT_MS + 50);
  CHECK(c.outputs().control == CTRL_AUTO);
  CHECK(!(c.outputs().faults & FAULT_SOLENOID));
  s1On = 4.0f;   // shorted
  step(cfg::SOL_FAULT_MS + 50);
  CHECK(c.outputs().control == CTRL_STOCK);
  CHECK(c.outputs().faults & FAULT_SOLENOID);

  // S2's driver MOSFET fails shorted: current flows while it's commanded off.
  s1On = 0.95f;
  in.customRequested = false;  // acknowledge, then take over again
  step(cfg::ARM_OFF_MIN_MS + 50);
  in.customRequested = true;
  step(500);
  CHECK(c.outputs().control == CTRL_AUTO);
  CHECK(!c.outputs().s2);  // 1st: S2 off
  s2Off = 0.9f;
  step(cfg::SOL_FAULT_MS + 50);
  CHECK(c.outputs().control == CTRL_STOCK);
  CHECK(c.outputs().faults & FAULT_SOLENOID);
}


// ---------------------------------------------------------------------------
// Tests added after mutation testing (tools/mutate.py) showed bugs planted in
// these paths went unnoticed. Each says what it pins down.

// Runs IBIT up to the ON/OFF prompt, answering every earlier prompt.
static void toCutPrompt(Bench& b) {
  if (!b.t.active()) CHECK(b.t.start(b.in));
  while (b.t.active() && b.t.phase() != PH_PROMPT) b.step(10);
  for (int p = 0; p < 6 && b.t.phase() == PH_PROMPT; p++) {
    const TestPrompt pr = b.t.prompt();
    b.in.up = pr == PR_UP;
    b.in.down = pr == PR_DOWN;
    b.in.eco = pr == PR_ECO;
    b.in.sport = pr == PR_SPORT;
    b.in.manual = pr == PR_MANUAL;
    b.in.brake = pr == PR_BRAKE;
    b.step(20);
  }
  b.in.up = b.in.down = b.in.eco = b.in.sport = b.in.manual = b.in.brake = false;
  while (b.t.active() && b.t.phase() == PH_CUT_ARM) b.step(10);
}

static void test_cut_failure_paths() {
  std::printf("IBIT ON/OFF CUT: every way the hardware cut can fail is a FAIL\n");
  // A welded relay: turning ON/OFF off drops the coils, but the contact
  // stays closed and the solenoid keeps drawing current.
  Bench w;
  w.relayWelded = true;
  w.runAll();
  CHECK(w.t.result(T_ONOFF_CUT) == TR_FAIL);
  CHECK(w.t.value(T_ONOFF_CUT) > 0.9f);
  // No current sensors wired: the coil sense alone must still catch a power
  // pole that doesn't cut...
  Bench n;
  n.senseWired = false;
  n.cutBroken = true;
  n.runAll();
  CHECK(n.t.result(T_ONOFF_CUT) == TR_FAIL);
  // ...and pass a good one.
  Bench g;
  g.senseWired = false;
  g.runAll();
  CHECK(g.t.result(T_ONOFF_CUT) == TR_PASS);
  // No current to cut (S1's wire broken): it can't prove anything, FAIL at once.
  Bench o;
  o.solenoidOpen[0] = true;
  toCutPrompt(o);
  CHECK(o.t.result(T_ONOFF_CUT) == TR_FAIL);
  CHECK(o.t.value(T_ONOFF_CUT) < 0.1f);
  CHECK(o.t.phase() == PH_WATCHDOG);
  CHECK(!o.t.outputs().relay && !o.t.outputs().s1);
  // Nobody turns ON/OFF off: FAIL after the prompt's 8 s, everything off.
  Bench t;
  toCutPrompt(t);
  CHECK(t.t.prompt() == PR_ONOFF_OFF);
  CHECK(t.t.outputs().relay && t.t.outputs().s1);  // current flowing, waiting for the cut
  t.step(cfg::IBIT_PROMPT_TIMEOUT_MS - 100);
  CHECK(t.t.result(T_ONOFF_CUT) == TR_NONE);
  t.step(200);
  CHECK(t.t.result(T_ONOFF_CUT) == TR_FAIL);
  CHECK(t.t.phase() == PH_WATCHDOG);
  CHECK(!t.t.outputs().relay && !t.t.outputs().s1);
  // TEST pressed at the prompt: SKIP, and on to the last step.
  Bench k;
  toCutPrompt(k);
  k.step(20, true);
  CHECK(k.t.result(T_ONOFF_CUT) == TR_SKIP);
  CHECK(k.t.phase() == PH_WATCHDOG);
  // Turned off, the verdict waits for the relay to drop out.
  Bench d;
  toCutPrompt(d);
  d.switchOn = false;
  d.step(cfg::IBIT_RELAY_SETTLE_MS - 20);
  CHECK(d.t.result(T_ONOFF_CUT) == TR_NONE);
  d.step(40);
  CHECK(d.t.result(T_ONOFF_CUT) == TR_PASS);
  CHECK(d.t.currentTest() == T_WATCHDOG);
}

static void test_outputs_between_steps() {
  std::printf("IBIT: nothing left on between steps; the screen flash ends\n");
  Bench b;
  CHECK(b.t.start(b.in));
  while (b.t.phase() == PH_LAMPS) b.step(10);
  CHECK(!b.t.outputs().lamps);  // the flash is over once the relay step starts
  int solPhases = 0;
  TestPhase was = b.t.phase();
  while (b.t.active() && b.t.phase() != PH_PROMPT) {
    b.step(10);
    if (b.t.phase() == PH_SOL_ON && was != PH_SOL_ON) solPhases++;
    was = b.t.phase();
  }
  CHECK(solPhases == 3);  // S1, S2, SL: no fourth
  // The prompts only read switches: relay and drivers off throughout.
  while (b.t.phase() == PH_PROMPT) {
    const TestOutputs& o = b.t.outputs();
    CHECK(!o.relay && !o.s1 && !o.s2 && !o.sl && !o.lamps);
    b.step(cfg::IBIT_PROMPT_TIMEOUT_MS / 2, true);
  }
}

static void test_rerun_and_restore() {
  std::printf("IBIT: a second run starts clean; the watchdog restore keeps values\n");
  Bench b;
  b.solenoidOpen[1] = true;
  CHECK(b.t.start(b.in));
  while (b.t.phase() != PH_SOL_GAP || b.t.currentTest() != T_SOL_S2) b.step(10);
  CHECK(b.t.result(T_SOL_S2) == TR_FAIL);
  b.in.engineRpm = 500;  // engine started: aborted
  b.step(10);
  CHECK(b.t.aborted());
  b.in.engineRpm = 0;
  b.solenoidOpen[1] = false;  // wire fixed
  CHECK(b.t.start(b.in));
  CHECK(!b.t.aborted());
  CHECK(b.t.result(T_SOL_S2) == TR_NONE);  // the old FAIL is gone
  CHECK(b.t.result(T_ONOFF_CUT) == TR_NONE);
  b.runAll();
  CHECK(b.t.result(T_SOL_S2) == TR_PASS);

  SelfTest after;
  after.restoreAfterWatchdog(b.t.results(), b.t.values(), true);
  for (uint8_t i = 0; i < T_COUNT; i++) {
    if (i != T_WATCHDOG) CHECK(after.result((TestId)i) == b.t.result((TestId)i));
  }
  CHECK(after.value(T_SOL_S2) > 0.9f);  // the amps on the table survive the reset
  CHECK(!after.aborted());
  CHECK(after.inhibit() == INH_NONE);
  CHECK(!after.outputs().relay && !after.outputs().s1);
  // Garbage in the saved table (RAM after a power cut) becomes "not run".
  uint8_t junk[T_COUNT];
  float vals[T_COUNT] = {};
  for (uint8_t i = 0; i < T_COUNT; i++) junk[i] = i % 2 ? TR_SKIP : 200;
  SelfTest j;
  j.restoreAfterWatchdog(junk, vals, false);
  CHECK(j.result(T_SUPPLY) == TR_NONE);
  CHECK(j.result(T_TPS) == TR_SKIP);
  CHECK(j.result(T_WATCHDOG) == TR_FAIL);
}

static void test_prompt_countdown() {
  std::printf("IBIT: the prompt's countdown on the screen\n");
  Bench b;
  CHECK(b.t.start(b.in));
  CHECK(b.t.promptRemainingMs(b.in.nowMs) == 0);  // not a prompt
  while (b.t.phase() != PH_PROMPT) b.step(10);
  const uint32_t t0 = b.in.nowMs;
  CHECK(b.t.promptRemainingMs(t0) == cfg::IBIT_PROMPT_TIMEOUT_MS);
  CHECK(b.t.promptRemainingMs(t0 + 3000) == cfg::IBIT_PROMPT_TIMEOUT_MS - 3000);
  CHECK(b.t.promptRemainingMs(t0 + cfg::IBIT_PROMPT_TIMEOUT_MS + 500) == 0);
  toCutPrompt(b);
  const uint32_t t1 = b.in.nowMs;
  CHECK(b.t.promptRemainingMs(t1) > cfg::IBIT_PROMPT_TIMEOUT_MS - 100);
}

static void test_pbit_details() {
  std::printf("PBIT: gyro, moving off during the key-on relay check\n");
  Bench b;
  CHECK(!b.t.hasRun());
  b.in.imuPresent = false;
  b.t.runPowerOn(b.in, false);
  CHECK(b.t.hasRun());
  CHECK(b.t.result(T_IMU) == (cfg::IMU_INSTALLED ? TR_FAIL : TR_SKIP));
  Bench g;
  g.t.runPowerOn(g.in, false);
  CHECK(g.t.result(T_IMU) == (cfg::IMU_INSTALLED ? TR_PASS : TR_SKIP));
  // Lever into D during the 0.4 s check: drop it, drivers off at once.
  Bench m;
  m.t.runPowerOn(m.in, false);
  CHECK(m.t.outputs().s1);
  m.step(100);
  m.in.leverInD = true;
  m.step(10);
  CHECK(!m.t.active());
  CHECK(m.t.result(T_RELAY_RELEASE) == TR_SKIP);
  CHECK(!m.t.outputs().s1 && !m.t.outputs().s2 && !m.t.outputs().sl);
  // Rolling: the same.
  Bench r;
  r.t.runPowerOn(r.in, false);
  r.in.speedMph = 2;
  r.step(10);
  CHECK(!r.t.active());
  CHECK(r.t.result(T_RELAY_RELEASE) == TR_SKIP);
  CHECK(!r.t.outputs().s1);
}

int main() {
  test_pbit();
  test_interlocks();
  test_full_ibit_pass();
  test_one_solenoid_at_a_time();
  test_detects_faults();
  test_prompts_timeout_and_skip();
  test_abort();
  test_cbit_solenoid_fault();
  test_cbit_solenoid_short();
  test_cut_failure_paths();
  test_outputs_between_steps();
  test_rerun_and_restore();
  test_prompt_countdown();
  test_pbit_details();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
