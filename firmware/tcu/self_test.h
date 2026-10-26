// Built-in test (BIT), modelled on avionics practice:
//   PBIT  power-up BIT: passive checks, run once after every boot.
//   CBIT  continuous BIT: the Controller's faults and warnings (tcu_logic.cpp).
//   IBIT  initiated BIT: hold the TEST button. Exercises lamps, relays,
//         solenoid circuits, console switches and the watchdog. Interlocked
//         like "weight on wheels": only parked, engine off, master at STOCK.
// Pure logic, no Arduino dependencies; unit-tested in test/test_self_test.cpp.
#pragma once
#include <stdint.h>

#include "tcu_logic.h"

namespace tcu {

enum TestId : uint8_t {
  // Passive: PBIT, and re-run at the start of IBIT.
  T_SUPPLY,
  T_TPS,
  T_ATF,
  T_SWITCHES,
  T_CALIBRATED,
  T_LAST_RESET,
  // Active: IBIT only.
  T_LAMPS,
  T_SOL_S1,
  T_SOL_S2,
  T_SOL_SL,
  T_RELAY_RELEASE,
  T_BTN_UP,
  T_BTN_DOWN,
  T_SW_ECO,
  T_SW_SPORT,
  T_BRAKE,
  T_WATCHDOG,
  T_COUNT
};

enum TestResult : uint8_t {
  TR_NONE = 0,  // not run
  TR_PASS,
  TR_FAIL,
  TR_CHECK,     // done, but needs a human to confirm (lamps, unsensed solenoids)
  TR_SKIP,
};

// Why IBIT may not run (or was aborted).
enum TestInhibit : uint8_t {
  INH_NONE = 0,
  INH_MASTER,   // master switch not at STOCK
  INH_LEVER_D,  // lever in D
  INH_MOVING,
  INH_ENGINE,   // engine running (tach, or battery at charging voltage)
  INH_SUPPLY,   // battery below 11.0 V
};

enum TestPhase : uint8_t {
  PH_IDLE = 0,
  PH_LAMPS,
  PH_RELAY_ON,
  PH_SOL_ON,
  PH_SOL_GAP,
  PH_RELAY_OFF,
  PH_RELEASE_CHECK,
  PH_PROMPT,
  PH_WATCHDOG,  // HAL must save results and let the watchdog fire
  PH_DONE,
};

enum TestPrompt : uint8_t { PR_NONE = 0, PR_UP, PR_DOWN, PR_ECO, PR_SPORT, PR_BRAKE };

struct TestOutputs {
  bool relay = false;
  bool s1 = false, s2 = false, sl = false;
  bool lamps = false;
  bool triggerWatchdog = false;
};

struct TestSummary {
  uint8_t pass = 0, fail = 0, check = 0, skip = 0;
};

const char* testName(TestId id);
// Interlock. `in.customRequested` must be the raw master switch position.
TestInhibit testInhibit(const Inputs& in);

class SelfTest {
 public:
  // PBIT. `unexpectedWdtReset`: last reset was a watchdog that IBIT didn't ask for.
  void runPowerOn(const Inputs& in, bool unexpectedWdtReset);
  // Start IBIT. Returns false (and sets inhibit()) if the interlock refuses.
  bool start(const Inputs& in);
  // Advance IBIT. `testEdge` = TEST button pressed this step (skips a prompt).
  void step(const Inputs& in, bool testEdge);
  // After a reboot caused by the IBIT watchdog step: restore saved results.
  void restoreAfterWatchdog(const uint8_t* results, const float* values, bool watchdogWorked);

  bool active() const { return phase_ != PH_IDLE && phase_ != PH_DONE; }
  bool hasRun() const { return phase_ == PH_DONE || pbitDone_; }
  bool ibitDone() const { return phase_ == PH_DONE; }
  TestPhase phase() const { return phase_; }
  TestPrompt prompt() const { return phase_ == PH_PROMPT ? (TestPrompt)(PR_UP + promptIndex_) : PR_NONE; }
  uint32_t promptRemainingMs(uint32_t nowMs) const;
  TestInhibit inhibit() const { return inhibit_; }  // start refusal or abort reason
  bool aborted() const { return aborted_; }
  TestId currentTest() const { return current_; }
  const TestOutputs& outputs() const { return out_; }
  TestResult result(TestId id) const { return (TestResult)results_[id]; }
  float value(TestId id) const { return values_[id]; }
  const uint8_t* results() const { return results_; }
  const float* values() const { return values_; }
  TestSummary summary() const;

 private:
  void runPassive(const Inputs& in);
  void set(TestId id, TestResult r, float v = 0.0f);
  void enter(TestPhase p, uint32_t nowMs);
  void abort(TestInhibit why);
  void finish();

  TestPhase phase_ = PH_IDLE;
  uint32_t phaseSince_ = 0;
  uint8_t solIndex_ = 0;
  uint8_t promptIndex_ = 0;
  bool pbitDone_ = false;
  bool aborted_ = false;
  TestInhibit inhibit_ = INH_NONE;
  TestId current_ = T_SUPPLY;
  TestOutputs out_;
  uint8_t results_[T_COUNT] = {};
  float values_[T_COUNT] = {};
};

}  // namespace tcu
