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

float upshiftMph(DriveMode mode, uint8_t fromGear, float tpsPct) {
  if (fromGear < 1 || fromGear > 3) return 1e9f;
  return interpTps(cfg::UPSHIFT_MPH[mode][fromGear - 1], tpsPct);
}

float downshiftMph(DriveMode mode, uint8_t fromGear, float tpsPct) {
  if (fromGear < 2 || fromGear > 4) return -1.0f;
  const float v = upshiftMph(mode, fromGear - 1, tpsPct) - cfg::DOWNSHIFT_HYST_MPH[mode];
  return v < cfg::MIN_DOWNSHIFT_MPH ? cfg::MIN_DOWNSHIFT_MPH : v;
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
  selBadSince_ = nowMs;
  offSinceMs_ = nowMs;
  dropRefMs_ = nowMs;
  decelRefMs_ = nowMs;
  lastStepMs_ = tpsRefMs_ = nowMs;
}

const Outputs& Controller::step(const Inputs& in) {
  now_ = in.nowMs;
  updateMode(in);
  updateButtons(in);
  updateCorner(in);
  updateDecel(in);
  updateThrottle(in);
  updateWarnings(in);
  active_ = evaluateFaults(in);
  updateArming(in);

  if (out_.control != CTRL_STOCK) {
    latched_ |= active_;
    if (latched_) {
      exitToStock(EV_FAULT);
    } else if (!armed_ || !in.customRequested || !in.leverInD) {
      exitToStock(EV_STOCK);
    } else if (selError_) {
      exitToStock(EV_MODE_SELECT);  // not latched: fixing the switches re-engages
    } else {
      runCustom(in);
    }
  } else if (armed_ && in.customRequested && in.leverInD && !latched_ && !active_ && selValid_) {
    tryEnterCustom(in);
  }

  compose(in);
  return out_;
}

// Exactly one of ECO, SPORT and MANUAL must be on. Anything else (none, or
// two or three) for longer than MODE_SWITCH_GRACE_MS hands control back to
// the factory ECU until the switches are fixed. The grace time lets you move
// from one switch to another (both on, or none, for a moment) without a
// handover; meanwhile the last valid selection carries on.
void Controller::updateMode(const Inputs& in) {
  const uint8_t on = (in.eco ? 1 : 0) + (in.sport ? 1 : 0) + (in.manual ? 1 : 0);
  selValid_ = on == 1;
  if (selValid_) {
    selBadSince_ = now_;
    selError_ = false;
    manualSel_ = in.manual;
    // MANUAL is SPORT that you shift yourself: SPORT's lockup and pedal response.
    out_.mode = in.eco ? MODE_ECO : MODE_SPORT;
  } else if (elapsed(now_, selBadSince_, cfg::MODE_SWITCH_GRACE_MS)) {
    selError_ = true;
  }
  out_.modeSelect = !selError_ ? SEL_OK : on ? SEL_SEVERAL : SEL_NONE;
}

void Controller::updateButtons(const Inputs& in) {
  const bool both = in.up && in.down;
  if (in.up && !prevUp_) upSince_ = now_;
  if (in.down && !prevDown_) downSince_ = now_;
  // Edge-triggered: holding a button never auto-repeats shifts.
  upEdge_ = in.up && !prevUp_ && !both;
  downEdge_ = in.down && !prevDown_ && !both;
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

  if (selError_) w |= WARN_MODE_SWITCH;
  // Driving with no tach signal: the over-rev guards fall back to speed x
  // ratio and the ratio check pauses. Tell the driver.
  if (tachLost_.update(cfg::TACH_INSTALLED && !in.rpmValid && in.speedMph > cfg::TACH_LOST_MIN_MPH, now_,
                       cfg::TACH_LOST_MS)) {
    w |= WARN_TACH;
  }
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
                     in.tpsValid && in.tpsPct > cfg::VSS_CHECK_TPS && in.speedMph < cfg::STATIONARY_MPH;
  if (vssStall_.update(stall, now_, cfg::VSS_CHECK_MS)) f |= FAULT_VSS;

  // VSS lost at speed: reported speed collapses faster than any car can brake
  // (also trips on a locked-wheel skid, where handing back to the ECU is fine).
  if (elapsed(now_, dropRefMs_, 100)) {
    if (dropRefMph_ >= cfg::VSS_DROP_CHECK_MIN_MPH && dropRefMph_ - in.speedMph > cfg::VSS_MAX_DROP_MPH_PER_100MS) {
      f |= FAULT_VSS;
    }
    dropRefMph_ = in.speedMph;
    dropRefMs_ = now_;
  }

  // With the converter locked, engine rpm must match speed x ratio of the
  // commanded gear. A mismatch means wrong gear (e.g. S1/S2 swapped), a
  // hydraulic fault, or bad VSS/tach data.
  bool ratioBad = false;
  if (custom && slCmd_ && !pendingGear_ && elapsed(now_, lastLockupChangeMs_, cfg::RATIO_SETTLE_MS) && in.rpmValid &&
      in.speedMph >= cfg::RATIO_CHECK_MIN_MPH) {
    const float expected = in.speedMph * cfg::RPM_PER_MPH[gear_];
    ratioBad = absf(in.engineRpm - expected) > expected * cfg::RATIO_TOLERANCE;
  }
  if (cfg::RATIO_CHECK_ENABLED && ratioBad_.update(ratioBad, now_, cfg::RATIO_FAULT_MS)) f |= FAULT_RATIO;

  // Continuous solenoid circuit check: commanded on must draw its rated current
  // (not too little, not a short), commanded off must not draw any. Catches a
  // broken wire, a shorted wire, a relay that didn't switch, or a shorted driver.
  // out_ still holds last step's commands here.
  const bool cmd[3] = {out_.s1, out_.s2, out_.sl};
  for (uint8_t i = 0; i < 3; i++) {
    const float a = in.solAmps[i];
    const bool bad = in.solSenseValid && custom && out_.relayCustom &&
                     (cmd[i] ? (a < cfg::SOL_MIN_A || a > cfg::SOL_OVER_A) : a > cfg::SOL_OFF_MAX_A);
    if (solBad_[i].update(bad, now_, cfg::SOL_FAULT_MS)) f |= FAULT_SOLENOID;
  }

  // The ON/OFF switch's two poles must agree: the one the Mega reads, and the
  // one carrying the relay coils' power. ON with no coil power = a broken
  // wire or blown fuse; OFF with coil power = the hardware cut isn't working
  // (or the read wire is broken). Either way, no CUSTOM until it's fixed.
  const bool split = in.coilSenseValid && in.switchOn != in.coilPowered;
  if (onoffBad_.update(split, now_, cfg::ONOFF_FAULT_MS)) f |= FAULT_ONOFF;
  // A relay found closed with its coil off would mix the ECU's and the TCU's
  // solenoid feeds; never take over with one.
  if (in.relayStuck) f |= FAULT_RELAY;

  // The starter only turns in P or N. An engine start with the lever input
  // reading D means that input is stuck on, and the TCU would keep driving in
  // 2, L or R: don't trust it again until the next power-up.
  if (engineOff_.update(in.rpmValid && in.engineRpm < cfg::ENGINE_OFF_RPM && in.speedMph < cfg::STATIONARY_MPH, now_,
                        cfg::ENGINE_OFF_MS)) {
    if (!engineWasOff_) crankDip_ = false;
    engineWasOff_ = true;
  }
  if (engineWasOff_ && in.batteryV < cfg::CRANK_DIP_V) crankDip_ = true;
  if (engineWasOff_ && in.rpmValid && in.engineRpm > cfg::ENGINE_STARTED_RPM) {
    if (crankDip_ && in.leverInD) leverStuck_ = true;
    engineWasOff_ = false;
  }
  if (leverStuck_) f |= FAULT_LEVER;
  return f;
}

void Controller::updateArming(const Inputs& in) {
  if (!in.customRequested) {
    // STOCK position: disarm and acknowledge any latched faults.
    armed_ = false;
    latched_ = 0;
    if (prevCustomReq_) offSinceMs_ = now_;
  } else if (!prevCustomReq_ && elapsed(now_, offSinceMs_, cfg::ARM_OFF_MIN_MS)) {
    // Only a fresh STOCK -> CUSTOM flip arms the TCU, after a real OFF of at
    // least ARM_OFF_MIN_MS: a reset mid-drive, a switch left ON at key-on, or
    // a glitch while the inputs settle after boot never arms it.
    armed_ = true;
  }
  prevCustomReq_ = in.customRequested;
}

void Controller::tryEnterCustom(const Inputs& in) {
  uint8_t g = 0;
  if (in.speedMph < cfg::STATIONARY_MPH) {
    g = 1;
  } else if (in.ecuSenseValid) {
    // Bumpless handover: start in the gear the ECU is holding right now.
    g = gearFromSolenoids(in.ecuS1, in.ecuS2);
    if (in.speedMph * cfg::RPM_PER_MPH[g] > cfg::MAX_DOWNSHIFT_RPM) g = 0;
    // Cross-check the sensed gear with the engine: rpm must sit between the
    // locked figure for that gear and what converter slip can add. A dead
    // sense input (reads "both off" = 4th) or a mis-wired one fails this, so
    // the TCU waits for a stop instead of taking over in the wrong gear.
    if (g) {
      const float locked = in.speedMph * cfg::RPM_PER_MPH[g];
      const bool fits = in.rpmValid && locked >= cfg::ENTRY_MIN_LOCKED_RPM &&
                        in.engineRpm >= locked * cfg::ENTRY_RPM_MIN_RATIO &&
                        in.engineRpm <= locked * cfg::ENTRY_RPM_MAX_RATIO;
      if (!fits) g = 0;
    }
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
  brakeHold_ = liftHold_ = highTpsSeen_ = false;
  lastLockupChangeMs_ = now_;
  overRev_ = Persist();
  out_.control = manualSel_ ? CTRL_MANUAL : CTRL_AUTO;
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

  // The MANUAL switch picks who shifts: on, the shifter lever; off, the map.
  const Control want = manualSel_ ? CTRL_MANUAL : CTRL_AUTO;
  if (out_.control != want) {
    out_.control = want;
    raise(want == CTRL_MANUAL ? EV_MANUAL : EV_AUTO_RESUMED, true);
  }
  if (upEdge_ || downEdge_) {
    if (out_.control != CTRL_MANUAL) {
      raise(EV_DENY_NOT_MANUAL, true);
    } else if (requestShift(gear_ + (upEdge_ ? 1 : -1), in.speedMph, true)) {
      raise(EV_SHIFT, true);
    }
  }

  revLimitGuard(in);
  if (out_.control == CTRL_AUTO) {
    autoSchedule(in);
  } else {
    manualSafety(in);
  }
  updateLockup(in);
}

bool Controller::requestShift(int target, float speedMph, bool byDriver) {
  Event deny = EV_NONE;
  if (target < 1 || target > 4) {
    deny = EV_DENY_LIMIT;
  } else if (pendingGear_ || !elapsed(now_, lastShiftMs_, cfg::MIN_SHIFT_INTERVAL_MS)) {
    deny = EV_DENY_BUSY;
  } else if (target < gear_) {
    // Directive 1, generalised: guard every downshift, not only into 1st.
    if (speedMph * cfg::RPM_PER_MPH[target] > cfg::MAX_DOWNSHIFT_RPM ||
        (target == 1 && speedMph > cfg::FIRST_GEAR_MAX_MPH)) {
      deny = EV_DENY_OVERREV;
    }
  } else if (speedMph < cfg::MANUAL_MIN_MPH[target] + cfg::UPSHIFT_MARGIN_MPH) {
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
  const float rpm = in.rpmValid ? in.engineRpm : in.speedMph * cfg::RPM_PER_MPH[gear_];
  if (overRev_.update(rpm > cfg::REV_LIMIT_UPSHIFT_RPM, now_, cfg::REV_LIMIT_HOLD_MS)) {
    if (requestShift(gear_ + 1, in.speedMph, false)) raise(EV_REV_LIMIT_UPSHIFT, true);
  }
}

void Controller::updateCorner(const Inputs& in) {
  if (!in.latValid || in.speedMph < cfg::CORNER_MIN_MPH) {
    cornering_ = false;
    straight_.active = false;
    return;
  }
  const float g = absf(in.latG);
  if (!cornering_) {
    if (g >= cfg::CORNER_ENTER_G) {
      cornering_ = true;
      cornerSinceMs_ = now_;
      straight_.active = false;
    }
  } else if (straight_.update(g < cfg::CORNER_EXIT_G, now_, cfg::CORNER_EXIT_MS)) {
    cornering_ = false;
  }
}

bool Controller::cornerHoldActive() const {
  return cfg::CORNER_HOLD[out_.mode] && cornering_ && !elapsed(now_, cornerSinceMs_, cfg::CORNER_HOLD_MAX_MS);
}

void Controller::updateDecel(const Inputs& in) {
  // Deceleration over ~300 ms windows, mph per second (positive = slowing).
  if (elapsed(now_, decelRefMs_, 300)) {
    const float dt = (now_ - decelRefMs_) / 1000.0f;
    decelMphPerS_ = (decelRefMph_ - in.speedMph) / dt;
    decelRefMph_ = in.speedMph;
    decelRefMs_ = now_;
  }
}

void Controller::updateThrottle(const Inputs& in) {
  // Smoothed throttle for the shift map (first-order low-pass, per mode).
  const uint32_t tau = cfg::SHIFT_TPS_SMOOTH_MS[out_.mode];
  const uint32_t dt = now_ - lastStepMs_;
  lastStepMs_ = now_;
  tpsShift_ = tau == 0 ? in.tpsPct : tpsShift_ + (in.tpsPct - tpsShift_) * ((float)dt / (float)(tau + dt));
  // Tip-in: throttle rising faster than TIPIN_RATE over a ~100 ms window.
  if (elapsed(now_, tpsRefMs_, 100)) {
    const float rate = (in.tpsPct - tpsRef_) * 1000.0f / (float)(now_ - tpsRefMs_);
    tipIn_ = rate >= cfg::TIPIN_RATE_PCT_PER_S;
    tpsRef_ = in.tpsPct;
    tpsRefMs_ = now_;
  }
}

bool Controller::brakeDownshift(const Inputs& in) {
  const uint8_t m = out_.mode;
  if (!cfg::BRAKE_DOWNSHIFT[m] || !in.brake || cornering_ || gear_ <= 1) return false;
  if (decelMphPerS_ < cfg::BRAKE_DOWNSHIFT_MIN_DECEL) return false;
  if (in.speedMph * cfg::RPM_PER_MPH[gear_ - 1] > cfg::BRAKE_DOWNSHIFT_RPM[m]) return false;
  if (!requestShift(gear_ - 1, in.speedMph, false)) return false;
  brakeHold_ = true;
  return true;
}

void Controller::autoSchedule(const Inputs& in) {
  if (pendingGear_) return;
  // Brake hold: after a brake downshift, keep the gear for the corner exit
  // until the driver opens the throttle or has been off the brake a while.
  if (prevBrake_ && !in.brake) brakeReleasedMs_ = now_;
  prevBrake_ = in.brake;
  if (brakeHold_ && !in.brake &&
      (in.tpsPct >= cfg::BRAKE_HOLD_TPS || elapsed(now_, brakeReleasedMs_, cfg::BRAKE_HOLD_MS))) {
    brakeHold_ = false;
  }
  // Lift-off hold: a quick lift after a hard push keeps the gear.
  const uint8_t m = out_.mode;
  if (in.tpsPct >= cfg::LIFT_FROM_TPS) {
    highTpsSeen_ = true;
    lastHighTpsMs_ = now_;
  }
  if (cfg::LIFT_HOLD[m] && !liftHold_ && highTpsSeen_ && in.tpsPct <= cfg::LIFT_TO_TPS &&
      !elapsed(now_, lastHighTpsMs_, cfg::LIFT_WINDOW_MS)) {
    liftHold_ = true;
    liftHoldSinceMs_ = now_;
  }
  if (liftHold_ && (!cfg::LIFT_HOLD[m] || in.tpsPct >= cfg::LIFT_RELEASE_TPS ||
                    elapsed(now_, liftHoldSinceMs_, cfg::LIFT_HOLD_MS))) {
    liftHold_ = false;
  }
  // No automatic shifts mid-corner (manual and over-rev shifts still work).
  if (cornerHoldActive()) return;
  if (brakeDownshift(in)) return;
  // A kickdown must not wait out the anti-hunting dwell, so 4->3->2 takes
  // about one shift interval per step instead of over a second. It also
  // bypasses the throttle smoothing.
  const bool kickdown = in.tpsPct >= cfg::KICKDOWN_TPS[m];
  const float tps = kickdown ? in.tpsPct : tpsShift_;
  if (!kickdown && !elapsed(now_, gearSinceMs_, cfg::AUTO_MIN_TIME_IN_GEAR_MS)) return;
  if (gear_ < 4 && in.speedMph >= upshiftMph(out_.mode, gear_, tps)) {
    if (!brakeHold_ && !liftHold_ && (!kickdown || elapsed(now_, gearSinceMs_, cfg::AUTO_MIN_TIME_IN_GEAR_MS))) {
      requestShift(gear_ + 1, in.speedMph, false);
    }
  } else if (gear_ > 1 && in.speedMph < downshiftMph(out_.mode, gear_, tps)) {
    // Kickdowns that would over-rev are refused by requestShift().
    requestShift(gear_ - 1, in.speedMph, false);
  }
}

void Controller::manualSafety(const Inputs& in) {
  if (pendingGear_ || gear_ <= 1) return;
  if (in.speedMph < cfg::MANUAL_MIN_MPH[gear_]) {
    if (requestShift(gear_ - 1, in.speedMph, false)) raise(EV_AUTO_DOWNSHIFT, true);
  }
}

void Controller::updateLockup(const Inputs& in) {
  const uint8_t m = out_.mode;
  // Per-gear rpm floor on top of the mode's speed thresholds. Hot ATF wants
  // as much lockup as possible, so it uses the lowest floor (the anti-lug
  // rule below still protects the engine).
  const float k = cfg::RPM_PER_MPH[gear_ >= 1 && gear_ <= 4 ? gear_ : 4];
  const float minRpm = hot_ ? cfg::LU_MIN_RPM[MODE_ECO] : cfg::LU_MIN_RPM[m];
  const float floorOn = minRpm / k;
  const float floorOff = (minRpm - cfg::LU_MIN_RPM_HYST) / k;
  const float baseOn = hot_ ? cfg::HOT_LU_ON_MPH : cfg::LU_ON_MPH[m];
  const float baseOff = hot_ ? cfg::HOT_LU_OFF_MPH : cfg::LU_OFF_MPH[m];
  const float onMph = baseOn > floorOn ? baseOn : floorOn;
  const float offMph = baseOff > floorOff ? baseOff : floorOff;
  // Never lug the engine: at low locked rpm, lockup only on a light throttle.
  const bool lug = in.speedMph * k < cfg::LU_LUG_RPM && in.tpsPct > cfg::LU_LUG_TPS;
  float tpsOn = hot_ ? cfg::HOT_LU_TPS_ON : cfg::LU_TPS_ON[m];
  float tpsOff = hot_ ? cfg::HOT_LU_TPS_OFF : cfg::LU_TPS_OFF[m];
  // Highway: stay locked under load in 4th (cruise, grades, headwind).
  if (cfg::HWY_LOCKUP[m] && gear_ == 4 && in.speedMph * k >= cfg::HWY_LU_MIN_RPM) {
    if (tpsOn < cfg::HWY_LU_TPS_ON) tpsOn = cfg::HWY_LU_TPS_ON;
    if (tpsOff < cfg::HWY_LU_TPS_OFF) tpsOff = cfg::HWY_LU_TPS_OFF;
  }

  // Brake and low speed always release: a locked converter stalls the engine.
  // Coast release (per mode) keeps the converter slipping, ready for throttle.
  const bool coast = cfg::LU_COAST_RELEASE[m] && in.tpsPct < (slCmd_ ? cfg::COAST_TPS_OFF : cfg::COAST_TPS_ON);
  const bool blocked = pendingGear_ || gear_ < cfg::LU_MIN_GEAR || in.brake || cold_ || coast ||
                       !elapsed(now_, gearSinceMs_, cfg::LOCKUP_SETTLE_AFTER_SHIFT_MS);
  const bool tipIn = cfg::LU_TIPIN_RELEASE[m] && tipIn_ && !hot_;
  if (slCmd_) {
    if (blocked || tipIn || lug || in.speedMph < offMph || in.tpsPct > tpsOff) setLockup(false);
  } else if (!blocked && !lug && in.speedMph >= onMph && in.tpsPct <= tpsOn &&
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
  out_.cornerHold = custom && out_.control == CTRL_AUTO && cornerHoldActive();
  out_.gearHold = custom && out_.control == CTRL_AUTO && (brakeHold_ || liftHold_);
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
