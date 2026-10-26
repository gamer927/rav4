// Pure control logic for the 1999 RAV4 TCU. No Arduino dependencies, so it
// builds for AVR and for the host unit tests (test/test_tcu_logic.cpp).
#pragma once
#include <stdint.h>

namespace tcu {

enum DriveMode : uint8_t { MODE_ECO = 0, MODE_NORMAL = 1, MODE_SPORT = 2 };
enum Control : uint8_t { CTRL_STOCK = 0, CTRL_AUTO = 1, CTRL_MANUAL = 2 };

// Faults hand control back to the factory ECU and stay latched until the
// master switch is returned to STOCK.
enum FaultBits : uint16_t {
  FAULT_SUPPLY = 1u << 0,
  FAULT_VSS = 1u << 1,
  FAULT_TPS = 1u << 2,
  FAULT_RATIO = 1u << 3,
  FAULT_SOLENOID = 1u << 4,  // solenoid current doesn't match the command
};

// Warnings are shown on the display; CUSTOM control continues.
enum WarnBits : uint16_t {
  WARN_ATF_SENSOR = 1u << 0,
  WARN_ATF_HOT = 1u << 1,
  WARN_ATF_CRITICAL = 1u << 2,
  WARN_MODE_SWITCH = 1u << 3,   // ECO and SPORT both on: running NORMAL
  WARN_BUTTON_STUCK = 1u << 4,
  WARN_WDT_RESET = 1u << 5,
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
  EV_AUTO_RESUMED,       // MANUAL -> AUTO (long press [+])
};

struct Inputs {
  uint32_t nowMs = 0;
  // Debounced switch states, true = active.
  bool up = false, down = false, eco = false, sport = false;
  bool customRequested = false;  // master switch in CUSTOM position
  bool brake = false;
  bool leverInD = false;
  // Factory ECU solenoid commands, sensed upstream of the relays.
  bool ecuSenseValid = false, ecuS1 = false, ecuS2 = false, ecuSL = false;
  // Sensors.
  float speedKmh = 0.0f;
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
  Control control = CTRL_STOCK;
  DriveMode mode = MODE_NORMAL;
  bool armed = false;        // master switch cycled into CUSTOM since boot
  uint16_t faults = 0;
  uint16_t warnings = 0;
  Event event = EV_NONE;
  uint8_t eventSeq = 0;      // increments whenever an event is (re)raised
};

// 1999 RAV4 (Toyota 4-speed ECT) shift-solenoid truth table, D range.
void solenoidsForGear(uint8_t gear, bool& s1, bool& s2);
uint8_t gearFromSolenoids(bool s1, bool s2);
float upshiftKmh(DriveMode mode, uint8_t fromGear, float tpsPct);
float downshiftKmh(DriveMode mode, uint8_t fromGear, float tpsPct);

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
  bool requestShift(int target, float speedKmh, bool byDriver);
  void commitGear(uint8_t gear);
  void revLimitGuard(const Inputs& in);
  void autoSchedule(const Inputs& in);
  void manualSafety(const Inputs& in);
  void updateLockup(const Inputs& in);
  void updateCorner(const Inputs& in);
  void updateDecel(const Inputs& in);
  bool cornerHoldActive() const;
  bool brakeDownshift(const Inputs& in);
  void setLockup(bool on);
  void compose(const Inputs& in);
  void raise(Event ev, bool always);

  Outputs out_;
  uint32_t now_ = 0;
  bool prevCustomReq_ = true;  // a switch already in CUSTOM at boot does not arm
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
  float decelRefKmh_ = 0.0f;
  uint32_t decelRefMs_ = 0;
  float decelKmhPerS_ = 0.0f;
  bool brakeHold_ = false;
  uint32_t brakeReleasedMs_ = 0;
  bool prevBrake_ = false;

  bool prevUp_ = false, prevDown_ = false;
  bool upEdge_ = false, downEdge_ = false, upLong_ = false, upLongFired_ = false;
  uint32_t upSince_ = 0, downSince_ = 0;

  Persist supplyBad_, tpsBad_, vssStall_, ratioBad_, overRev_, solBad_[3];
  float dropRefKmh_ = 0.0f;
  uint32_t dropRefMs_ = 0;
};

}  // namespace tcu
