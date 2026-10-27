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

  Bench() {
    in.nowMs = 5000;
    in.rpmValid = true;
    in.tpsValid = true;
    in.tpsPct = 0;
    in.atfValid = true;
    in.atfC = 20;
    in.batteryV = 12.5f;  // ignition on, engine off: not charging
    in.imuPresent = true;
  }
  void physics() {
    const TestOutputs& o = t.outputs();
    const bool drive[3] = {o.s1, o.s2, o.sl};
    const bool routed = o.relay || relayWelded;
    for (int i = 0; i < 3; i++) in.solAmps[i] = (drive[i] && routed && !solenoidOpen[i]) ? 0.95f : 0.0f;
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
  CHECK(b.t.result(T_CALIBRATED) == (cfg::CALIBRATED ? TR_PASS : TR_FAIL));
  CHECK(!b.t.active());

  Bench c;
  c.in.up = true;           // button stuck at boot
  c.in.atfValid = false;    // sender unplugged
  c.t.runPowerOn(c.in, true);
  CHECK(c.t.result(T_SWITCHES) == TR_FAIL);
  CHECK(c.t.result(T_ATF) == TR_FAIL);
  CHECK(c.t.result(T_LAST_RESET) == TR_FAIL);
}

static void test_interlocks() {
  std::printf("IBIT interlocks (weight-on-wheels style)\n");
  struct Case { const char* what; void (*apply)(Inputs&); TestInhibit expect; };
  const Case cases[] = {
      {"master at CUSTOM", [](Inputs& i) { i.customRequested = true; }, INH_MASTER},
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

  // Board resets via the watchdog; HAL restores the saved table.
  SelfTest after;
  after.restoreAfterWatchdog(b.t.results(), b.t.values(), true);
  after.runPowerOn(b.in, false);
  CHECK(after.ibitDone());
  CHECK(after.result(T_WATCHDOG) == TR_PASS);
  CHECK(after.result(T_SOL_S2) == TR_PASS);
  const TestSummary s = after.summary();
  CHECK(s.fail == (cfg::CALIBRATED ? 0 : 1));  // only "not calibrated" on a fresh build
  CHECK(s.check == 1);                          // lamps
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
  step(50);
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
  auto step = [&](uint32_t ms) {
    for (uint32_t x = 0; x < ms; x += 10) {
      in.nowMs += 10;
      const Outputs& o = c.outputs();
      in.solAmps[0] = o.relayCustom && o.s1 ? s1On : 0.0f;
      in.solAmps[1] = o.relayCustom && o.s2 ? 0.95f : 0.0f;
      in.solAmps[2] = o.relayCustom && o.sl ? 0.95f : 0.0f;
      c.step(in);
    }
  };
  step(50);
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
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
