#include "tcu_logic.h"

#include "config.h"

namespace tcu {

namespace {

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float absf(float v) { return v < 0.0f ? -v : v; }
inline bool elapsed(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t)(now - since) >= ms; }

float interpTps(const float (&row)[cfg::TPS_POINTS], float tpsPct) {
  const float step = 100.0f / (cfg::TPS_POINTS - 1);
  const float x = clampf(tpsPct, 0.0f, 100.0f) / step;
  const uint8_t i = (uint8_t)x;
  if (i >= cfg::TPS_POINTS - 1) return row[cfg::TPS_POINTS - 1];
  return row[i] + (row[i + 1] - row[i]) * (x - i);
}

// Index 0 unused. 1st: S1 on; 2nd: S1+S2; 3rd: S2; 4th (OD): both off.
// Both-off = OD matches Toyota's hydraulic fail-safe for a dead TCU in D.
const bool kS1[5] = {false, true, true, false, false};
const bool kS2[5] = {false, false, true, true, false};

}  // namespace

void solenoidsForGear(uint8_t gear, bool& s1, bool& s2) {
  if (gear < 1 || gear > 4) gear = 0;
  s1 = kS1[gear];
  s2 = kS2[gear];
}

uint8_t gearFromSolenoids(bool s1, bool s2) {
  if (s1) return s2 ? 2 : 1;
  return s2 ? 3 : 4;
}

float upshiftKmh(DriveMode mode, uint8_t fromGear, float tpsPct) {
  if (fromGear < 1 || fromGear > 3) return 1e9f;
  return interpTps(cfg::UPSHIFT_KMH[mode][fromGear - 1], tpsPct);
}

float downshiftKmh(DriveMode mode, uint8_t fromGear, float tpsPct) {
  if (fromGear < 2 || fromGear > 4) return -1.0f;
  const float v = upshiftKmh(mode, fromGear - 1, tpsPct) - cfg::DOWNSHIFT_HYST_KMH[mode];
  return v < cfg::MIN_DOWNSHIFT_KMH ? cfg::MIN_DOWNSHIFT_KMH : v;
}

bool Persist::update(bool cond, uint32_t nowMs, uint32_t holdMs) {
  if (!cond) {
    active = false;
    return false;
  }
  if (!active) {
    active = true;
    since = nowMs;
  }
  return elapsed(nowMs, since, holdMs);
}

void Controller::begin(uint32_t nowMs, bool watchdogReset) {
  *this = Controller();
  now_ = nowMs;
  wdtReset_ = watchdogReset;
  upSince_ = downSince_ = nowMs;
  dropRefMs_ = nowMs;
}

const Outputs& Controller::step(const Inputs& in) {
  now_ = in.nowMs;
  updateMode(in);
  updateButtons(in);
  updateWarnings(in);
  active_ = evaluateFaults(in);
  updateArming(in);

  if (out_.control != CTRL_STOCK) {
    latched_ |= active_;
    if (latched_) {
      exitToStock(EV_FAULT);
    } else if (!armed_ || !in.customRequested || !in.leverInD) {
      exitToStock(EV_STOCK);
    } else {
      runCustom(in);
    }
  } else if (armed_ && in.customRequested && in.leverInD && !latched_ && !active_) {
    tryEnterCustom(in);
  }

  compose(in);
  return out_;
}

void Controller::updateMode(const Inputs& in) {
  if (in.eco && in.sport) {
    out_.mode = MODE_NORMAL;  // impossible on a rotary switch: wiring fault
  } else if (in.eco) {
    out_.mode = MODE_ECO;
  } else if (in.sport) {
    out_.mode = MODE_SPORT;
  } else {
    out_.mode = MODE_NORMAL;
  }
}

void Controller::updateButtons(const Inputs& in) {
  const bool both = in.up && in.down;
  if (in.up && !prevUp_) upSince_ = now_;
  if (in.down && !prevDown_) downSince_ = now_;
  // Edge-triggered: holding a button never auto-repeats shifts.
  upEdge_ = in.up && !prevUp_ && !both;
  downEdge_ = in.down && !prevDown_ && !both;

  upLong_ = false;
  if (!in.up) {
    upLongFired_ = false;
  } else if (!both && !upLongFired_ && elapsed(now_, upSince_, cfg::LONG_PRESS_MS)) {
    upLong_ = true;
    upLongFired_ = true;
  }
  prevUp_ = in.up;
  prevDown_ = in.down;
}

void Controller::updateWarnings(const Inputs& in) {
  uint16_t w = 0;
  if (in.atfValid) {
    if (in.atfC < cfg::ATF_COLD_INHIBIT_C) cold_ = true;
    else if (in.atfC >= cfg::ATF_COLD_RELEASE_C) cold_ = false;
    if (in.atfC >= cfg::ATF_HOT_C) hot_ = true;
    else if (in.atfC < cfg::ATF_HOT_EXIT_C) hot_ = false;
    if (hot_) w |= WARN_ATF_HOT;
    if (in.atfC >= cfg::ATF_CRITICAL_C) w |= WARN_ATF_CRITICAL;
  } else {
    // Unknown temperature: fall back to the normal (non-thermal) lockup rules.
    cold_ = hot_ = false;
    w |= WARN_ATF_SENSOR;
  }

  if (!in.oilValid) w |= WARN_OIL_SENSOR;
  const bool oilLow = in.oilValid && in.rpmValid && in.engineRpm > cfg::OIL_CHECK_RPM && in.oilPsi < cfg::OIL_LOW_PSI;
  if (oilLow_.update(oilLow, now_, cfg::OIL_LOW_MS)) w |= WARN_OIL_LOW;

  if (in.eco && in.sport) w |= WARN_MODE_SWITCH;
  if ((in.up && elapsed(now_, upSince_, cfg::STUCK_BUTTON_MS)) ||
      (in.down && elapsed(now_, downSince_, cfg::STUCK_BUTTON_MS))) {
    w |= WARN_BUTTON_STUCK;
  }
  if (wdtReset_) w |= WARN_WDT_RESET;
  out_.warnings = w;
}

uint16_t Controller::evaluateFaults(const Inputs& in) {
  uint16_t f = 0;
  const bool custom = out_.control != CTRL_STOCK;

  const bool supplyBad = in.batteryV < cfg::SUPPLY_MIN_V || in.batteryV > cfg::SUPPLY_MAX_V;
  if (supplyBad_.update(supplyBad, now_, cfg::SUPPLY_FAULT_MS)) f |= FAULT_SUPPLY;
  if (tpsBad_.update(!in.tpsValid, now_, cfg::TPS_FAULT_MS)) f |= FAULT_TPS;

  // VSS dead: engine pulling hard in a driving gear but no road speed. Every
  // over-rev guard trusts VSS, so this must not go unnoticed.
  uint8_t g = 0;
  if (custom) g = gear_;
  else if (in.ecuSenseValid) g = gearFromSolenoids(in.ecuS1, in.ecuS2);
  const bool stall = in.leverInD && g >= 2 && !in.brake && in.rpmValid && in.engineRpm > cfg::VSS_CHECK_RPM &&
                     in.tpsValid && in.tpsPct > cfg::VSS_CHECK_TPS && in.speedKmh < cfg::STATIONARY_KMH;
  if (vssStall_.update(stall, now_, cfg::VSS_CHECK_MS)) f |= FAULT_VSS;

  // VSS lost at speed: reported speed collapses faster than any car can brake
  // (also trips on a locked-wheel skid, where handing back to the ECU is fine).
  if (elapsed(now_, dropRefMs_, 100)) {
    if (dropRefKmh_ >= cfg::VSS_DROP_CHECK_MIN_KMH && dropRefKmh_ - in.speedKmh > cfg::VSS_MAX_DROP_KMH_PER_100MS) {
      f |= FAULT_VSS;
    }
    dropRefKmh_ = in.speedKmh;
    dropRefMs_ = now_;
  }

  // With the converter locked, engine rpm must match speed x ratio of the
  // commanded gear. A mismatch means wrong gear (e.g. S1/S2 swapped), a
  // hydraulic fault, or bad VSS/tach data.
  bool ratioBad = false;
  if (custom && slCmd_ && !pendingGear_ && elapsed(now_, lastLockupChangeMs_, cfg::RATIO_SETTLE_MS) && in.rpmValid &&
      in.speedKmh >= cfg::RATIO_CHECK_MIN_KMH) {
    const float expected = in.speedKmh * cfg::RPM_PER_KMH[gear_];
    ratioBad = absf(in.engineRpm - expected) > expected * cfg::RATIO_TOLERANCE;
  }
  if (cfg::RATIO_CHECK_ENABLED && ratioBad_.update(ratioBad, now_, cfg::RATIO_FAULT_MS)) f |= FAULT_RATIO;
  return f;
}

void Controller::updateArming(const Inputs& in) {
  if (!in.customRequested) {
    // STOCK position: disarm and acknowledge any latched faults.
    armed_ = false;
    latched_ = 0;
  } else if (!prevCustomReq_) {
    // Only a fresh STOCK -> CUSTOM flip arms the TCU. A reset mid-drive (or a
    // switch left in CUSTOM at key-on) therefore stays on the factory ECU.
    armed_ = true;
  }
  prevCustomReq_ = in.customRequested;
}

void Controller::tryEnterCustom(const Inputs& in) {
  uint8_t g = 0;
  if (in.speedKmh < cfg::STATIONARY_KMH) {
    g = 1;
  } else if (in.ecuSenseValid) {
    // Bumpless handover: start in the gear the ECU is holding right now.
    g = gearFromSolenoids(in.ecuS1, in.ecuS2);
    if (in.speedKmh * cfg::RPM_PER_KMH[g] > cfg::MAX_DOWNSHIFT_RPM) g = 0;
  }
  if (!g) {
    raise(EV_NEED_SYNC, false);
    return;
  }
  gear_ = g;
  pendingGear_ = 0;
  gearSinceMs_ = now_;
  lastShiftMs_ = now_ - cfg::MIN_SHIFT_INTERVAL_MS;
  slCmd_ = in.ecuSenseValid && in.ecuSL && g >= cfg::LU_MIN_GEAR && !in.brake;
  lastLockupChangeMs_ = now_;
  overRev_ = Persist();
  out_.control = cfg::ENTER_IN_MANUAL ? CTRL_MANUAL : CTRL_AUTO;
  raise(EV_CUSTOM, true);
}

void Controller::exitToStock(Event why) {
  out_.control = CTRL_STOCK;
  gear_ = 0;
  pendingGear_ = 0;
  slCmd_ = false;
  raise(why, true);
}

void Controller::runCustom(const Inputs& in) {
  if (pendingGear_ && elapsed(now_, pendingSinceMs_, cfg::LOCKUP_RELEASE_BEFORE_SHIFT_MS)) {
    commitGear(pendingGear_);
    pendingGear_ = 0;
  }

  if (upLong_ && out_.control == CTRL_MANUAL) {
    out_.control = CTRL_AUTO;
    raise(EV_AUTO_RESUMED, true);
  } else if (upEdge_ || downEdge_) {
    // Tiptronic-style: any tap in AUTO drops into MANUAL and shifts.
    out_.control = CTRL_MANUAL;
    if (requestShift(gear_ + (upEdge_ ? 1 : -1), in.speedKmh, true)) raise(EV_SHIFT, true);
  }

  revLimitGuard(in);
  if (out_.control == CTRL_AUTO) {
    autoSchedule(in);
  } else {
    manualSafety(in);
  }
  updateLockup(in);
}

bool Controller::requestShift(int target, float speedKmh, bool byDriver) {
  Event deny = EV_NONE;
  if (target < 1 || target > 4) {
    deny = EV_DENY_LIMIT;
  } else if (pendingGear_ || !elapsed(now_, lastShiftMs_, cfg::MIN_SHIFT_INTERVAL_MS)) {
    deny = EV_DENY_BUSY;
  } else if (target < gear_) {
    // Directive 1, generalised: guard every downshift, not only into 1st.
    if (speedKmh * cfg::RPM_PER_KMH[target] > cfg::MAX_DOWNSHIFT_RPM ||
        (target == 1 && speedKmh > cfg::FIRST_GEAR_MAX_KMH)) {
      deny = EV_DENY_OVERREV;
    }
  } else if (speedKmh < cfg::MANUAL_MIN_KMH[target] + cfg::UPSHIFT_MARGIN_KMH) {
    deny = EV_DENY_TOO_SLOW;
  }
  if (deny != EV_NONE) {
    if (byDriver) raise(deny, true);
    return false;
  }

  lastShiftMs_ = now_;
  if (slCmd_) {
    // Release the converter clutch first; shift once it has let go.
    setLockup(false);
    pendingGear_ = (uint8_t)target;
    pendingSinceMs_ = now_;
  } else {
    commitGear((uint8_t)target);
  }
  return true;
}

void Controller::commitGear(uint8_t gear) {
  gear_ = gear;
  gearSinceMs_ = now_;
}

void Controller::revLimitGuard(const Inputs& in) {
  if (gear_ >= 4 || pendingGear_ || (out_.control == CTRL_MANUAL && !cfg::MANUAL_REV_LIMIT_UPSHIFT)) {
    overRev_.active = false;
    return;
  }
  const float rpm = in.rpmValid ? in.engineRpm : in.speedKmh * cfg::RPM_PER_KMH[gear_];
  if (overRev_.update(rpm > cfg::REV_LIMIT_UPSHIFT_RPM, now_, cfg::REV_LIMIT_HOLD_MS)) {
    if (requestShift(gear_ + 1, in.speedKmh, false)) raise(EV_REV_LIMIT_UPSHIFT, true);
  }
}

void Controller::autoSchedule(const Inputs& in) {
  if (pendingGear_) return;
  const float tps = in.tpsPct;
  // A kickdown must not wait out the anti-hunting dwell, so 4->3->2 takes
  // about one shift interval per step instead of over a second.
  const bool kickdown = tps >= cfg::KICKDOWN_TPS;
  if (!kickdown && !elapsed(now_, gearSinceMs_, cfg::AUTO_MIN_TIME_IN_GEAR_MS)) return;
  if (gear_ < 4 && in.speedKmh >= upshiftKmh(out_.mode, gear_, tps)) {
    if (!kickdown || elapsed(now_, gearSinceMs_, cfg::AUTO_MIN_TIME_IN_GEAR_MS)) {
      requestShift(gear_ + 1, in.speedKmh, false);
    }
  } else if (gear_ > 1 && in.speedKmh < downshiftKmh(out_.mode, gear_, tps)) {
    // Kickdowns that would over-rev are refused by requestShift().
    requestShift(gear_ - 1, in.speedKmh, false);
  }
}

void Controller::manualSafety(const Inputs& in) {
  if (pendingGear_ || gear_ <= 1) return;
  if (in.speedKmh < cfg::MANUAL_MIN_KMH[gear_]) {
    if (requestShift(gear_ - 1, in.speedKmh, false)) raise(EV_AUTO_DOWNSHIFT, true);
  }
}

void Controller::updateLockup(const Inputs& in) {
  const uint8_t m = out_.mode;
  const float onKmh = hot_ ? cfg::HOT_LU_ON_KMH : cfg::LU_ON_KMH[m];
  const float offKmh = hot_ ? cfg::HOT_LU_OFF_KMH : cfg::LU_OFF_KMH[m];
  const float tpsOn = hot_ ? cfg::HOT_LU_TPS_ON : cfg::LU_TPS_ON[m];
  const float tpsOff = hot_ ? cfg::HOT_LU_TPS_OFF : cfg::LU_TPS_OFF[m];

  // Brake and low speed always release: a locked converter stalls the engine.
  const bool blocked = pendingGear_ || gear_ < cfg::LU_MIN_GEAR || in.brake || cold_ ||
                       !elapsed(now_, gearSinceMs_, cfg::LOCKUP_SETTLE_AFTER_SHIFT_MS);
  if (slCmd_) {
    if (blocked || in.speedKmh < offKmh || in.tpsPct > tpsOff) setLockup(false);
  } else if (!blocked && in.speedKmh >= onKmh && in.tpsPct <= tpsOn &&
             elapsed(now_, lastLockupChangeMs_, cfg::LOCKUP_MIN_OFF_MS)) {
    setLockup(true);
  }
}

void Controller::setLockup(bool on) {
  if (slCmd_ == on) return;
  slCmd_ = on;
  lastLockupChangeMs_ = now_;
}

void Controller::compose(const Inputs& in) {
  const bool custom = out_.control != CTRL_STOCK;
  out_.relayCustom = custom;
  out_.armed = armed_;
  out_.faults = latched_ | active_;
  if (custom) {
    solenoidsForGear(gear_, out_.s1, out_.s2);
    out_.sl = slCmd_;
    out_.gear = gear_;
    out_.lockup = slCmd_;
  } else {
    // Factory ECU in control: TCU drivers off, report what the ECU is doing.
    out_.s1 = out_.s2 = out_.sl = false;
    const bool known = in.ecuSenseValid && in.leverInD;
    out_.gear = known ? gearFromSolenoids(in.ecuS1, in.ecuS2) : 0;
    out_.lockup = in.ecuSenseValid && in.ecuSL;
  }
}

void Controller::raise(Event ev, bool always) {
  if (!always && out_.event == ev) return;
  out_.event = ev;
  out_.eventSeq++;
}

}  // namespace tcu
