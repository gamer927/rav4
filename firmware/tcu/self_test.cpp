#include "self_test.h"

#include "config.h"

namespace tcu {

namespace {

inline bool elapsed(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t)(now - since) >= ms; }

const char* const kNames[T_COUNT] = {
    "SUPPLY", "TPS",     "ATF SENS", "SWITCHES", "CALIBRATN", "LAST RST", "LAMPS",    "SOL S1",
    "SOL S2", "SOL SL",  "RELAY REL", "BTN [+]", "BTN [-]",   "ECO SW",   "SPORT SW", "BRAKE SW",
    "WATCHDOG",
};

constexpr uint8_t kPromptCount = PR_BRAKE - PR_UP + 1;

}  // namespace

const char* testName(TestId id) { return id < T_COUNT ? kNames[id] : "?"; }

TestInhibit testInhibit(const Inputs& in) {
  if (in.customRequested) return INH_MASTER;
  if (in.leverInD) return INH_LEVER_D;
  if (in.speedKmh > 1.0f) return INH_MOVING;
  // Two independent "engine running" indicators, so a missing tach can't
  // let the solenoid test run on a running engine.
  if ((in.rpmValid && in.engineRpm > cfg::TEST_ENGINE_RUNNING_RPM) || in.batteryV > cfg::TEST_CHARGING_V) {
    return INH_ENGINE;
  }
  if (in.batteryV < cfg::TEST_SUPPLY_MIN_V) return INH_SUPPLY;
  return INH_NONE;
}

void SelfTest::set(TestId id, TestResult r, float v) {
  results_[id] = r;
  values_[id] = v;
}

void SelfTest::runPassive(const Inputs& in) {
  set(T_SUPPLY, in.batteryV >= cfg::SUPPLY_MIN_V && in.batteryV <= cfg::SUPPLY_MAX_V ? TR_PASS : TR_FAIL,
      in.batteryV);
  set(T_TPS, in.tpsValid ? TR_PASS : TR_FAIL, in.tpsPct);
  set(T_ATF, in.atfValid ? TR_PASS : TR_FAIL, in.atfC);
  // A shift button held at boot is stuck.
  set(T_SWITCHES, !in.up && !in.down ? TR_PASS : TR_FAIL);
  set(T_CALIBRATED, cfg::CALIBRATED ? TR_PASS : TR_FAIL);
}

void SelfTest::runPowerOn(const Inputs& in, bool unexpectedWdtReset) {
  runPassive(in);
  set(T_LAST_RESET, unexpectedWdtReset ? TR_FAIL : TR_PASS);
  pbitDone_ = true;
}

bool SelfTest::start(const Inputs& in) {
  if (active()) return false;
  inhibit_ = testInhibit(in);
  if (inhibit_ != INH_NONE) return false;
  for (uint8_t i = T_LAMPS; i < T_COUNT; i++) set((TestId)i, TR_NONE);
  aborted_ = false;
  runPassive(in);
  out_ = TestOutputs();
  enter(PH_LAMPS, in.nowMs);
  return true;
}

void SelfTest::enter(TestPhase p, uint32_t nowMs) {
  phase_ = p;
  phaseSince_ = nowMs;
}

void SelfTest::abort(TestInhibit why) {
  out_ = TestOutputs();
  aborted_ = true;
  inhibit_ = why;
  phase_ = PH_DONE;
}

void SelfTest::finish() {
  out_ = TestOutputs();
  phase_ = PH_DONE;
}

void SelfTest::step(const Inputs& in, bool testEdge) {
  if (!active()) return;
  const TestInhibit why = testInhibit(in);
  if (why != INH_NONE) {
    abort(why);  // e.g. engine started or lever moved: stop driving anything
    return;
  }
  const uint32_t now = in.nowMs;
  const uint32_t inPhase = now - phaseSince_;

  switch (phase_) {
    case PH_LAMPS:
      current_ = T_LAMPS;
      out_.lamps = true;
      if (inPhase >= cfg::IBIT_LAMP_MS) {
        out_.lamps = false;
        set(T_LAMPS, TR_CHECK);  // the operator saw them, or didn't
        out_.relay = true;
        enter(PH_RELAY_ON, now);
      }
      break;

    case PH_RELAY_ON:
      if (inPhase >= cfg::IBIT_RELAY_SETTLE_MS) {
        solIndex_ = 0;
        enter(PH_SOL_ON, now);
      }
      break;

    case PH_SOL_ON: {
      // Relay energised, one driver on: the solenoid must draw its rated current.
      // Engine off means no line pressure, so this only clicks the valve.
      current_ = (TestId)(T_SOL_S1 + solIndex_);
      out_.s1 = solIndex_ == 0;
      out_.s2 = solIndex_ == 1;
      out_.sl = solIndex_ == 2;
      if (inPhase >= cfg::IBIT_SOLENOID_ON_MS) {
        if (in.solSenseValid) {
          const float a = in.solAmps[solIndex_];
          set(current_, a >= cfg::SOL_MIN_A && a <= cfg::SOL_MAX_A ? TR_PASS : TR_FAIL, a);
        } else {
          set(current_, TR_CHECK);  // listen for the click
        }
        out_.s1 = out_.s2 = out_.sl = false;
        enter(PH_SOL_GAP, now);
      }
      break;
    }

    case PH_SOL_GAP:
      if (inPhase >= cfg::IBIT_SOLENOID_GAP_MS) {
        if (++solIndex_ < 3) {
          enter(PH_SOL_ON, now);
        } else {
          out_.relay = false;
          enter(PH_RELAY_OFF, now);
        }
      }
      break;

    case PH_RELAY_OFF:
      if (inPhase >= cfg::IBIT_RELAY_SETTLE_MS) enter(PH_RELEASE_CHECK, now);
      break;

    case PH_RELEASE_CHECK: {
      // Relay released, all drivers on: their contacts are open, so no current
      // may flow. Current here means a welded or stuck relay.
      current_ = T_RELAY_RELEASE;
      out_.s1 = out_.s2 = out_.sl = true;
      if (inPhase >= cfg::IBIT_SOLENOID_ON_MS) {
        if (in.solSenseValid) {
          float maxA = 0.0f;
          for (uint8_t i = 0; i < 3; i++) maxA = in.solAmps[i] > maxA ? in.solAmps[i] : maxA;
          set(T_RELAY_RELEASE, maxA <= cfg::SOL_OFF_MAX_A ? TR_PASS : TR_FAIL, maxA);
        } else {
          set(T_RELAY_RELEASE, TR_SKIP);
        }
        out_.s1 = out_.s2 = out_.sl = false;
        promptIndex_ = 0;
        enter(PH_PROMPT, now);
      }
      break;
    }

    case PH_PROMPT: {
      current_ = (TestId)(T_BTN_UP + promptIndex_);
      bool ok = false;
      switch (PR_UP + promptIndex_) {
        case PR_UP: ok = in.up; break;
        case PR_DOWN: ok = in.down; break;
        case PR_ECO: ok = in.eco && !in.sport; break;
        case PR_SPORT: ok = in.sport && !in.eco; break;
        case PR_BRAKE: ok = in.brake; break;
      }
      TestResult r = TR_NONE;
      if (ok) r = TR_PASS;
      else if (testEdge) r = TR_SKIP;  // operator skipped this one
      else if (inPhase >= cfg::IBIT_PROMPT_TIMEOUT_MS) r = TR_FAIL;
      if (r != TR_NONE) {
        set(current_, r);
        if (++promptIndex_ < kPromptCount) {
          enter(PH_PROMPT, now);
        } else if (cfg::IBIT_WATCHDOG_TEST) {
          current_ = T_WATCHDOG;
          enter(PH_WATCHDOG, now);
        } else {
          set(T_WATCHDOG, TR_SKIP);
          finish();
        }
      }
      break;
    }

    case PH_WATCHDOG:
      // The HAL saves results and stops kicking the watchdog. If the board
      // comes back, restoreAfterWatchdog() records the pass.
      out_ = TestOutputs();
      out_.triggerWatchdog = true;
      break;

    default:
      break;
  }
}

void SelfTest::restoreAfterWatchdog(const uint8_t* results, const float* values, bool watchdogWorked) {
  for (uint8_t i = 0; i < T_COUNT; i++) {
    results_[i] = results[i] <= TR_SKIP ? results[i] : (uint8_t)TR_NONE;
    values_[i] = values[i];
  }
  set(T_WATCHDOG, watchdogWorked ? TR_PASS : TR_FAIL);
  out_ = TestOutputs();
  aborted_ = false;
  inhibit_ = INH_NONE;
  phase_ = PH_DONE;
}

uint32_t SelfTest::promptRemainingMs(uint32_t nowMs) const {
  if (phase_ != PH_PROMPT) return 0;
  const uint32_t in = nowMs - phaseSince_;
  return in >= cfg::IBIT_PROMPT_TIMEOUT_MS ? 0 : cfg::IBIT_PROMPT_TIMEOUT_MS - in;
}

TestSummary SelfTest::summary() const {
  TestSummary s;
  for (uint8_t i = 0; i < T_COUNT; i++) {
    switch (results_[i]) {
      case TR_PASS: s.pass++; break;
      case TR_FAIL: s.fail++; break;
      case TR_CHECK: s.check++; break;
      case TR_SKIP: s.skip++; break;
      default: break;
    }
  }
  return s;
}

}  // namespace tcu
