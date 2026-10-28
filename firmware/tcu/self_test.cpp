#include "self_test.h"

#include "config.h"

namespace tcu {

namespace {

inline bool elapsed(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t)(now - since) >= ms; }

// Plain names, shown on the screen and in the USB log. At most 10 characters
// for the ones that show a value (battery, pedal, probe, solenoids, relays).
const char* const kNames[T_COUNT] = {
    "BATTERY",    "GAS PEDAL",  "TEMP PROBE", "GYRO",          "SHIFTER",       "CALIBRATED",
    "RESTARTS",   "SCREEN",     "SOLENOID 1", "SOLENOID 2",    "LOCKUP SOL",    "RELAYS",
    "SHIFT UP",   "SHIFT DOWN", "ECO SWITCH", "SPORT SWITCH",  "MANUAL SWITCH", "BRAKE",
    "ON/OFF CUT", "SAFETY RESET",
};

constexpr uint8_t kPromptCount = PR_BRAKE - PR_UP + 1;

}  // namespace

const char* testName(TestId id) { return id < T_COUNT ? kNames[id] : "?"; }

TestInhibit testInhibit(const Inputs& in, bool switchMayBeOff) {
  if (in.leverInD) return INH_LEVER_D;
  if (in.speedMph > 0.6f) return INH_MOVING;
  // Two independent "engine running" indicators, so a missing tach can't
  // let the solenoid test run on a running engine.
  if ((in.rpmValid && in.engineRpm > cfg::TEST_ENGINE_RUNNING_RPM) || in.batteryV > cfg::TEST_CHARGING_V) {
    return INH_ENGINE;
  }
  if (in.batteryV < cfg::TEST_SUPPLY_MIN_V) return INH_SUPPLY;
  if (!switchMayBeOff) {
    if (!in.switchOn) return INH_MASTER;
    if (in.coilSenseValid && !in.coilPowered) return INH_NO_COIL;
  }
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
  set(T_IMU, !cfg::IMU_INSTALLED ? TR_SKIP : in.imuPresent ? TR_PASS : TR_FAIL);
  // A shift button held at boot is stuck.
  set(T_SWITCHES, !in.up && !in.down ? TR_PASS : TR_FAIL);
  // Not calibrated yet is expected on the bench: a reminder (CHECK), not a fault.
  set(T_CALIBRATED, cfg::CALIBRATED ? TR_PASS : TR_CHECK);
}

void SelfTest::runPowerOn(const Inputs& in, bool unexpectedWdtReset) {
  runPassive(in);
  set(T_LAST_RESET, unexpectedWdtReset ? TR_FAIL : TR_PASS);
  pbitDone_ = true;
  if (phase_ != PH_IDLE) return;  // IBIT results restored after its watchdog step: keep them
  // The two poles of ON/OFF must agree at key-on, whichever way it's set.
  set(T_ONOFF_CUT, !in.coilSenseValid ? TR_SKIP : in.switchOn == in.coilPowered ? TR_PASS : TR_FAIL);
  // Relay-release check: drivers on with the relays off. A closed contact
  // would let current through. Only parked with the lever out of D; the
  // solenoids are only reachable through a faulty relay anyway.
  if (in.solSenseValid && !in.leverInD && in.speedMph <= 0.6f) {
    out_ = TestOutputs();
    out_.s1 = out_.s2 = out_.sl = true;
    enter(PH_PBIT_RELEASE, in.nowMs);
  } else {
    set(T_RELAY_RELEASE, TR_SKIP);
  }
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

void SelfTest::endCutTest(uint32_t now) {
  out_ = TestOutputs();
  if (cfg::IBIT_WATCHDOG_TEST) {
    current_ = T_WATCHDOG;
    enter(PH_WATCHDOG, now);
  } else {
    set(T_WATCHDOG, TR_SKIP);
    finish();
  }
}

void SelfTest::finish() {
  out_ = TestOutputs();
  phase_ = PH_DONE;
}

void SelfTest::step(const Inputs& in, bool testEdge) {
  if (!active()) return;
  const uint32_t now = in.nowMs;
  const uint32_t inPhase = now - phaseSince_;
  if (phase_ == PH_PBIT_RELEASE) {
    if (in.leverInD || in.speedMph > 0.6f) {  // moved off: drop it, try next key-on
      out_ = TestOutputs();
      set(T_RELAY_RELEASE, TR_SKIP);
      phase_ = PH_IDLE;
    } else if (inPhase >= cfg::IBIT_SOLENOID_ON_MS) {
      float maxA = 0.0f;
      for (uint8_t i = 0; i < 3; i++) maxA = in.solAmps[i] > maxA ? in.solAmps[i] : maxA;
      set(T_RELAY_RELEASE, maxA <= cfg::SOL_OFF_MAX_A ? TR_PASS : TR_FAIL, maxA);
      out_ = TestOutputs();
      phase_ = PH_IDLE;
    }
    return;
  }
  // From the cut test on, ON/OFF is OFF by design.
  const TestInhibit why = testInhibit(in, phase_ == PH_CUT_PROMPT || phase_ == PH_CUT_CHECK || phase_ == PH_WATCHDOG);
  if (why != INH_NONE) {
    abort(why);  // e.g. engine started, lever moved, ON/OFF turned off: stop driving anything
    return;
  }

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
        // "Only this one on" also catches two switches wired to the same core.
        case PR_ECO: ok = in.eco && !in.sport && !in.manual; break;
        case PR_SPORT: ok = in.sport && !in.eco && !in.manual; break;
        case PR_MANUAL: ok = in.manual && !in.eco && !in.sport; break;
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
        } else {
          // Last: the ON/OFF hardware cut. Relays on, S1 on, current flowing...
          current_ = T_ONOFF_CUT;
          out_.relay = true;
          out_.s1 = true;
          enter(PH_CUT_ARM, now);
        }
      }
      break;
    }

    case PH_CUT_ARM:
      current_ = T_ONOFF_CUT;
      if (inPhase >= cfg::IBIT_RELAY_SETTLE_MS + cfg::IBIT_SOLENOID_ON_MS) {
        if (in.solSenseValid && in.solAmps[0] < cfg::SOL_MIN_A) {
          set(T_ONOFF_CUT, TR_FAIL, in.solAmps[0]);  // no current to cut: S1 or the relay is bad
          endCutTest(now);
        } else {
          enter(PH_CUT_PROMPT, now);
        }
      }
      break;

    case PH_CUT_PROMPT: {
      // ...then the operator turns ON/OFF OFF, with the Mega still commanding
      // the relays ON. The switch's power pole must drop them: no current, no
      // coil supply.
      current_ = T_ONOFF_CUT;
      if (!in.switchOn) {
        enter(PH_CUT_CHECK, now);  // give the relay time to drop out
      } else if (testEdge) {
        set(T_ONOFF_CUT, TR_SKIP);
        endCutTest(now);
      } else if (inPhase >= cfg::IBIT_PROMPT_TIMEOUT_MS) {
        set(T_ONOFF_CUT, TR_FAIL, in.solAmps[0]);
        endCutTest(now);
      }
      break;
    }

    case PH_CUT_CHECK:
      current_ = T_ONOFF_CUT;
      if (inPhase >= cfg::IBIT_RELAY_SETTLE_MS) {
        const bool noCurrent = !in.solSenseValid || in.solAmps[0] <= cfg::SOL_OFF_MAX_A;
        const bool noCoil = !in.coilSenseValid || !in.coilPowered;
        set(T_ONOFF_CUT, noCurrent && noCoil ? TR_PASS : TR_FAIL, in.solAmps[0]);
        endCutTest(now);
      }
      break;

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
  if (phase_ != PH_PROMPT && phase_ != PH_CUT_PROMPT) return 0;
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
