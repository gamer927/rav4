// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Pure control logic for the 1999 RAV4 TCU. No Arduino dependencies, so it
// builds for AVR and for the host unit tests (test/test_tcu_logic.cpp).
#pragma once
#include <stdint.h>

namespace tcu {

// There is no NORMAL: you pick ECO, SPORT or MANUAL (which drives like SPORT).
enum DriveMode : uint8_t { MODE_ECO = 0, MODE_SPORT = 1 };
enum Control : uint8_t { CTRL_STOCK = 0, CTRL_AUTO = 1, CTRL_MANUAL = 2 };
// The mode switches: exactly one of ECO, SPORT and MANUAL must be on.
enum ModeSelect : uint8_t { SEL_OK = 0, SEL_NONE, SEL_SEVERAL };

// Faults hand control back to the factory ECU and stay latched until the
// ON/OFF switch is turned OFF.
enum FaultBits : uint16_t {
  FAULT_SUPPLY = 1u << 0,
  FAULT_VSS = 1u << 1,
  FAULT_TPS = 1u << 2,
  FAULT_RATIO = 1u << 3,
  FAULT_SOLENOID = 1u << 4,  // solenoid current doesn't match the command
  FAULT_ONOFF = 1u << 5,     // the ON/OFF switch's two poles disagree (wiring or switch)
  FAULT_RELAY = 1u << 6,     // a relay stayed closed with its coil off (welded contact)
  FAULT_LEVER = 1u << 7,     // the engine started with the lever reading D: that input is stuck on
};

// Warnings are shown on the display; CUSTOM control continues.
enum WarnBits : uint16_t {
  WARN_ATF_SENSOR = 1u << 0,
  WARN_ATF_HOT = 1u << 1,
  WARN_ATF_CRITICAL = 1u << 2,
  WARN_MODE_SWITCH = 1u << 3,   // not exactly one mode switch on: factory ECU shifts
  WARN_BUTTON_STUCK = 1u << 4,
  WARN_WDT_RESET = 1u << 5,
  WARN_TACH = 1u << 6,          // no engine-speed signal while driving: speed x ratio used instead
  WARN_SHIFT_UP = 1u << 7,      // MANUAL at the redline: shift up (the TCU won't do it for you)
};

enum Event : uint8_t {
  EV_NONE = 0,
  EV_CUSTOM,             // TCU took control
  EV_STOCK,              // handed back to factory ECU
  EV_FAULT,              // handed back because of a fault
  EV_NEED_SYNC,          // CUSTOM requested but gear unknown: stop the car
  EV_SHIFT,              // driver shift accepted
  EV_DENY_OVERREV,       // downshift would over-rev
  EV_DENY_TOO_SLOW,      // upshift would lug / be undone immediately
  EV_DENY_BUSY,          // previous shift still in progress
  EV_DENY_LIMIT,         // already in 1st / 4th
  EV_AUTO_DOWNSHIFT,     // MANUAL: forced downshift while slowing
  EV_REV_LIMIT_UPSHIFT,  // forced upshift at rev limit
  EV_AUTO_RESUMED,       // MANUAL switch flipped off: back to the AUTO map
  EV_MANUAL,             // MANUAL switch flipped on: the shifter shifts
  EV_DENY_NOT_MANUAL,    // shifter clicked with the MANUAL switch off
  EV_MODE_SELECT,        // handed back: no mode switch, or more than one, is on
};

struct Inputs {
  uint32_t nowMs = 0;
  // Debounced switch states, true = active.
  bool up = false, down = false, eco = false, sport = false;
  bool manual = false;           // MANUAL switch ON: the shifter lever shifts
  bool customRequested = false;  // ON/OFF switch ON (and the firmware calibrated)
  // The ON/OFF switch has two poles: one is read here (switchOn, raw), the
  // other carries the relay coils' power (coilPowered, sensed on A7). OFF on
  // the power pole drops the relays in hardware, whatever the firmware does.
  bool switchOn = false;
  bool coilSenseValid = false, coilPowered = false;
  bool relayStuck = false;       // the relay-release self-test found a closed contact
  bool brake = false;
  bool leverInD = false;
  // Factory ECU solenoid commands, sensed upstream of the relays.
  bool ecuSenseValid = false, ecuS1 = false, ecuS2 = false, ecuSL = false;
  // Sensors.
  float speedMph = 0.0f;
  float engineRpm = 0.0f;
  bool rpmValid = false;
  float tpsPct = 0.0f;
  bool tpsValid = false;
  float atfC = 0.0f;
  bool atfValid = false;
  float batteryV = 0.0f;
  // Solenoid current from the high-side drivers' current-sense pins
  // (index 0 = S1, 1 = S2, 2 = SL). Only meaningful when solSenseValid.
  bool solSenseValid = false;
  float solAmps[3] = {0.0f, 0.0f, 0.0f};
  // Lateral acceleration (g, either sign) from the gyro; latValid = false when
  // there's no gyro or its bias hasn't been learned yet.
  bool latValid = false;
  float latG = 0.0f;
  bool imuPresent = false;  // gyro answered on the bus (for the self-test)
};

struct Outputs {
  bool relayCustom = false;  // true = ECU isolated onto dummy loads
  bool s1 = false, s2 = false, sl = false;
  uint8_t gear = 0;          // TCU gear in CUSTOM, ECU's gear in STOCK, 0 = unknown
  bool lockup = false;       // converter lockup commanded (by TCU or ECU)
  bool cornerHold = false;   // automatic shifts paused: cornering
  bool gearHold = false;     // upshifts paused: brake or lift-off hold
  Control control = CTRL_STOCK;
  DriveMode mode = MODE_ECO;      // the last valid selection
  ModeSelect modeSelect = SEL_OK;  // shown after MODE_SWITCH_GRACE_MS
  bool armed = false;        // ON/OFF switch flipped OFF -> ON since boot
  uint16_t faults = 0;
  uint16_t warnings = 0;
  Event event = EV_NONE;
  uint8_t eventSeq = 0;      // increments whenever an event is (re)raised
};

// 1999 RAV4 (Toyota 4-speed ECT) shift-solenoid truth table, D range.
void solenoidsForGear(uint8_t gear, bool& s1, bool& s2);
uint8_t gearFromSolenoids(bool s1, bool s2);
float upshiftMph(DriveMode mode, uint8_t fromGear, float tpsPct);
float downshiftMph(DriveMode mode, uint8_t fromGear, float tpsPct);

// Time-based debouncer: a new level is accepted after it has been stable for
// holdMs. Removes contact bounce and short noise bursts on long cable runs.
class Debouncer {
 public:
  explicit Debouncer(bool initial = false) : stable_(initial), candidate_(initial) {}
  bool update(bool raw, uint32_t nowMs, uint16_t holdMs) {
    if (raw != candidate_) {
      candidate_ = raw;
      since_ = nowMs;
    } else if (candidate_ != stable_ && (uint32_t)(nowMs - since_) >= holdMs) {
      stable_ = candidate_;
    }
    return stable_;
  }
  bool state() const { return stable_; }

 private:
  bool stable_;
  bool candidate_;
  uint32_t since_ = 0;
};

// True once `cond` has held continuously for holdMs.
struct Persist {
  bool active = false;
  uint32_t since = 0;
  bool update(bool cond, uint32_t nowMs, uint32_t holdMs);
};

class Controller {
 public:
  void begin(uint32_t nowMs, bool watchdogReset);
  const Outputs& step(const Inputs& in);
  const Outputs& outputs() const { return out_; }

 private:
  void updateMode(const Inputs& in);
  void updateButtons(const Inputs& in);
  void updateWarnings(const Inputs& in);
  uint16_t evaluateFaults(const Inputs& in);
  void updateArming(const Inputs& in);
  void tryEnterCustom(const Inputs& in);
  void exitToStock(Event why);
  void runCustom(const Inputs& in);
  bool requestShift(int target, float speedMph, bool byDriver);
  void commitGear(uint8_t gear);
  void revLimitGuard(const Inputs& in);
  void autoSchedule(const Inputs& in);
  void manualSafety(const Inputs& in);
  void updateLockup(const Inputs& in);
  void updateCorner(const Inputs& in);
  void updateDecel(const Inputs& in);
  void updateThrottle(const Inputs& in);
  bool cornerHoldActive() const;
  bool brakeDownshift(const Inputs& in);
  void setLockup(bool on);
  void compose(const Inputs& in);
  void raise(Event ev, bool always);

  Outputs out_;
  uint32_t now_ = 0;
  bool prevCustomReq_ = true;  // a switch already in CUSTOM at boot does not arm
  uint32_t offSinceMs_ = 0;    // when the switch last went OFF
  bool armed_ = false;
  uint16_t latched_ = 0;
  uint16_t active_ = 0;
  bool wdtReset_ = false;

  uint8_t gear_ = 0;
  uint8_t pendingGear_ = 0;  // waiting for lockup release before shifting
  bool slCmd_ = false;
  uint32_t lastShiftMs_ = 0;
  uint32_t gearSinceMs_ = 0;
  uint32_t pendingSinceMs_ = 0;
  uint32_t lastLockupChangeMs_ = 0;
  bool hot_ = false;
  bool cold_ = false;

  bool cornering_ = false;
  uint32_t cornerSinceMs_ = 0;
  Persist straight_;
  float decelRefMph_ = 0.0f;
  uint32_t decelRefMs_ = 0;
  float decelMphPerS_ = 0.0f;
  bool brakeHold_ = false;
  uint32_t brakeReleasedMs_ = 0;
  bool prevBrake_ = false;
  float tpsShift_ = 0.0f;  // smoothed throttle the shift map sees
  uint32_t lastStepMs_ = 0;
  float tpsRef_ = 0.0f;
  uint32_t tpsRefMs_ = 0;
  bool tipIn_ = false;
  bool liftHold_ = false;
  uint32_t liftHoldSinceMs_ = 0;
  bool highTpsSeen_ = false;
  uint32_t lastHighTpsMs_ = 0;

  bool selValid_ = false;   // exactly one mode switch on right now
  bool selError_ = false;   // ...not for MODE_SWITCH_GRACE_MS: back to STOCK
  bool manualSel_ = false;  // MANUAL was the last valid selection
  uint32_t selBadSince_ = 0;

  bool prevUp_ = false, prevDown_ = false;
  bool upEdge_ = false, downEdge_ = false;
  uint32_t upSince_ = 0, downSince_ = 0;

  Persist supplyBad_, tpsBad_, vssStall_, ratioBad_, overRev_, solBad_[3], onoffBad_, tachLost_;
  Persist engineOff_;
  bool engineWasOff_ = false;  // stopped, waiting for the next start
  bool crankDip_ = false;      // ...and the starter has pulled the battery down
  bool leverStuck_ = false;    // kept until the next power-up
  float dropRefMph_ = 0.0f;
  uint32_t dropRefMs_ = 0;
};

}  // namespace tcu
