// Calibration & tuning constants for the 1999 Toyota RAV4 TCU.
//
// Everything marked VERIFY or CALIBRATE is a starting estimate, not a known
// value for your vehicle. See docs/02-revised-spec.md §8 (calibration) before
// driving in CUSTOM mode.
#pragma once
#include <stdint.h>

namespace cfg {

// CUSTOM mode cannot be armed until this is true. Set it only after the
// driveline and sensor calibration in docs/02-revised-spec.md §8 is done:
// the over-rev guard is only as good as RPM_PER_MPH and VSS_PULSES_PER_MILE.
constexpr bool CALIBRATED = false;

// ---------------------------------------------------------------- driveline
// All speeds in this file are in mph.
//
// Engine RPM per mph in each gear with the torque converter LOCKED.
// Index 0 is unused. Defaults: 1999 RAV4 automatic ratios as published
// (2.810 / 1.549 / 1.000 / 0.734, final drive ~4.40) on the factory
// 215/70R16 tyres (~732 rev/mile). Published final-drive figures for this car
// disagree (3.18-4.40), so CALIBRATE from logged data (§8.3) before arming.
// e.g. 4th: 39.4 rpm per mph -> ~3,550 rpm at 90 mph.
constexpr float RPM_PER_MPH[5] = {0.0f, 151.0f, 83.2f, 53.8f, 39.4f};

// ------------------------------------------------------------------ sensors
constexpr float ADC_VREF = 5.0f;

// Vehicle speed sensor. Toyota meter-driven SPD is typically 4 pulses/rev at
// 637 rev/km = 2548 pulses/km = ~4101 pulses/mile. CALIBRATE against GPS.
constexpr float VSS_PULSES_PER_MILE = 4101.0f;
constexpr uint32_t VSS_MIN_PERIOD_US = 2000;      // reject glitches (> ~440 mph)
constexpr uint32_t VSS_TIMEOUT_US = 1500000;      // no pulse for 1.5 s -> 0 mph

// Engine tach (4-cyl, wasted-spark igniter): 2 pulses per crank revolution.
constexpr bool TACH_INSTALLED = true;
constexpr float TACH_PULSES_PER_REV = 2.0f;
constexpr uint32_t TACH_MIN_PERIOD_US = 1500;     // reject glitches (> 20k rpm)
constexpr uint32_t TACH_TIMEOUT_US = 200000;      // no pulse for 200 ms -> 0 rpm

// Factory ECU S1/S2/SL outputs sensed upstream of the relays (opto inputs).
// Set false if not wired: CUSTOM can then only be entered at a standstill.
constexpr bool ECU_SENSE_INSTALLED = true;

// Throttle position (linear VTA). CALIBRATE closed/WOT volts on the car.
// VTA is measured against the ECM's sensor ground E2, which is sensed on its
// own pin (A1) and subtracted, so the TCU never ties its ground to E2 (no
// ground loop that could shift the ECM's own throttle reading).
constexpr bool TPS_E2_SENSE = true;
constexpr float TPS_CLOSED_V = 0.60f;
constexpr float TPS_WOT_V = 4.00f;
constexpr float TPS_VALID_MIN_V = 0.15f;          // below = shorted / open ground
constexpr float TPS_VALID_MAX_V = 4.85f;          // above = open signal / short to VC

// ATF temperature sensor. DS18B20 = digital 1-Wire probe in the cooler line
// (pin D33, 4.7k pull-up to 5 V); reads -55..125 C, factory-calibrated.
// NTC = analog thermistor sender on A2 (constants below).
enum AtfSensor : uint8_t { ATF_DS18B20, ATF_NTC };
constexpr AtfSensor ATF_SENSOR = ATF_DS18B20;
constexpr uint32_t DS18B20_PERIOD_MS = 1000;      // one 12-bit reading per second
constexpr uint8_t DS18B20_MAX_MISSES = 3;         // bad reads in a row before "sensor fault"

// ATF NTC thermistor, read through a pull-up to ADC_VREF. A 1 k pull-up puts
// best resolution around 90-110 C where it matters. VERIFY R25/Beta for your
// sender (datasheet or 3-point calibration).
constexpr float ATF_PULLUP_OHMS = 1000.0f;
constexpr float ATF_R25_OHMS = 10000.0f;
constexpr float ATF_BETA = 3950.0f;
constexpr uint16_t ATF_ADC_SHORT = 8;             // raw ADC at/below = shorted
constexpr uint16_t ATF_ADC_OPEN = 1018;           // raw ADC at/above = open

// Battery divider 47k / 10k (x5.7): 28.5 V full scale, survives load dump with
// the input clamp described in the spec.
constexpr float VBAT_DIVIDER = 5.7f;

// Solenoid current sense: high-side driver IS pins on A4/A5/A6.
// amps = volts x IS_AMPS_PER_VOLT, where IS_AMPS_PER_VOLT = kILIS / R_IS from
// the driver datasheet. CALIBRATE on the bench with a 15 ohm load.
// Set false if the IS pins aren't wired; the circuit tests then report CHK.
constexpr bool SOLENOID_SENSE_INSTALLED = true;
constexpr float IS_AMPS_PER_VOLT = 1.0f;
// A healthy 11-15 ohm solenoid at 11.5-14.5 V draws 0.77-1.32 A.
constexpr float SOL_MIN_A = 0.6f;
constexpr float SOL_MAX_A = 1.6f;
constexpr float SOL_OFF_MAX_A = 0.2f;
constexpr uint32_t SOL_FAULT_MS = 300;

// ------------------------------------------------------------- built-in test
constexpr uint32_t PBIT_DELAY_MS = 1500;          // let sensor filters settle after boot
constexpr uint32_t TEST_LONG_PRESS_MS = 2000;     // hold TEST this long to start IBIT
constexpr uint32_t IBIT_LAMP_MS = 1500;
constexpr uint32_t IBIT_RELAY_SETTLE_MS = 150;
constexpr uint32_t IBIT_SOLENOID_ON_MS = 400;
constexpr uint32_t IBIT_SOLENOID_GAP_MS = 200;
constexpr uint32_t IBIT_PROMPT_TIMEOUT_MS = 8000;
constexpr bool IBIT_WATCHDOG_TEST = true;         // last IBIT step: deliberate watchdog reset
// "Engine running" for the IBIT interlock: tach above this, OR the battery
// above the charging voltage (alternator working). Two independent signs, so a
// dead tach wire can't let the solenoid test run on a running engine.
constexpr float TEST_ENGINE_RUNNING_RPM = 100.0f;
constexpr float TEST_CHARGING_V = 13.4f;
constexpr float TEST_SUPPLY_MIN_V = 11.0f;

// Console OLED (128x64, I2C). 0.96" modules are usually SSD1306; many 1.3"
// modules are SH1106.
constexpr bool OLED_INSTALLED = true;
constexpr bool OLED_SH1106 = false;
constexpr uint8_t OLED_I2C_ADDR = 0x3C;

// ------------------------------------------------------------------- timing
constexpr uint32_t CONTROL_PERIOD_MS = 10;
constexpr uint16_t DEBOUNCE_SWITCH_MS = 25;       // console switches (A-pillar run)
constexpr uint16_t DEBOUNCE_OPTO_MS = 40;         // brake / lever / ECU sense
constexpr uint32_t MIN_SHIFT_INTERVAL_MS = 700;   // one shift must finish first
constexpr uint32_t AUTO_MIN_TIME_IN_GEAR_MS = 1200;  // anti-hunting dwell
// Throttle at or above this is a kickdown: AUTO downshifts skip the dwell
// above and only wait for the previous shift to finish (MIN_SHIFT_INTERVAL).
constexpr float KICKDOWN_TPS[3] = {80.0f, 80.0f, 60.0f};  // per mode: ECO, NORMAL, SPORT
constexpr uint32_t LOCKUP_RELEASE_BEFORE_SHIFT_MS = 200;
constexpr uint32_t LOCKUP_SETTLE_AFTER_SHIFT_MS = 1500;
constexpr uint32_t LOCKUP_MIN_OFF_MS = 1000;      // anti-chatter before re-apply
constexpr uint32_t LONG_PRESS_MS = 1000;          // hold [+] = back to AUTO
// Sequential shifter lever: set true to swap the [+] and [-] inputs, e.g. to
// change between "pull = upshift" (rally style) and "push = upshift"
// without rewiring the lever's two microswitches.
constexpr bool SWAP_SHIFT_INPUTS = false;
constexpr uint32_t STUCK_BUTTON_MS = 10000;

// ------------------------------------------------------------------- safety
constexpr float STATIONARY_MPH = 2.0f;
constexpr float MAX_DOWNSHIFT_RPM = 5800.0f;      // predicted rpm after downshift
constexpr float FIRST_GEAR_MAX_MPH = 28.0f;       // hard cap, independent of ratios
constexpr float REV_LIMIT_UPSHIFT_RPM = 6200.0f;
constexpr uint32_t REV_LIMIT_HOLD_MS = 150;
constexpr bool MANUAL_REV_LIMIT_UPSHIFT = true;
// MANUAL: forced downshift below these speeds (index = current gear) so the
// car never stops in 3rd/4th. Set [2] to 0 to allow 2nd-gear (snow) starts.
constexpr float MANUAL_MIN_MPH[5] = {0.0f, 0.0f, 5.0f, 11.0f, 22.0f};
constexpr float UPSHIFT_MARGIN_MPH = 2.5f;        // no upshift into a forced-downshift zone
constexpr bool ENTER_IN_MANUAL = false;           // CUSTOM starts in AUTO map

constexpr float SUPPLY_MIN_V = 10.5f;
constexpr float SUPPLY_MAX_V = 16.0f;
constexpr uint32_t SUPPLY_FAULT_MS = 500;
constexpr uint32_t TPS_FAULT_MS = 200;

// VSS plausibility: engine pulling in a driving gear with no road speed.
constexpr float VSS_CHECK_RPM = 2500.0f;
constexpr float VSS_CHECK_TPS = 15.0f;
constexpr uint32_t VSS_CHECK_MS = 3000;
// VSS plausibility: deceleration no car can do (~2.3 g) = signal loss.
constexpr float VSS_DROP_CHECK_MIN_MPH = 9.0f;
constexpr float VSS_MAX_DROP_MPH_PER_100MS = 5.0f;

// Locked-converter ratio check: catches swapped S1/S2, wrong gear, bad VSS.
constexpr bool RATIO_CHECK_ENABLED = true;
constexpr float RATIO_CHECK_MIN_MPH = 19.0f;
constexpr float RATIO_TOLERANCE = 0.25f;
constexpr uint32_t RATIO_SETTLE_MS = 1500;
constexpr uint32_t RATIO_FAULT_MS = 1500;

// ------------------------------------------------------------------ thermal
// Lockup REDUCES heat (no converter slip). Hot ATF therefore widens the
// lockup window rather than cancelling lockup. Cold ATF inhibits lockup.
constexpr float ATF_COLD_INHIBIT_C = 35.0f;
constexpr float ATF_COLD_RELEASE_C = 40.0f;
constexpr float ATF_HOT_C = 110.0f;               // 230 F
constexpr float ATF_HOT_EXIT_C = 105.0f;
constexpr float ATF_CRITICAL_C = 125.0f;
constexpr float HOT_LU_ON_MPH = 25.0f;
constexpr float HOT_LU_OFF_MPH = 21.0f;
constexpr float HOT_LU_TPS_ON = 85.0f;
constexpr float HOT_LU_TPS_OFF = 95.0f;

// ------------------------------------------------------- per-mode lockup map
// Index: 0 = ECO, 1 = NORMAL, 2 = SPORT.
constexpr uint8_t LU_MIN_GEAR = 3;
constexpr float LU_ON_MPH[3] = {22.0f, 36.0f, 43.0f};   // ECO: as early as the rpm floor allows
constexpr float LU_OFF_MPH[3] = {19.0f, 32.0f, 40.0f};
// Lockup only when the locked engine speed (mph x ratio) is at least this,
// so it is per gear: ECO locks 3rd from ~23 mph but 4th only from ~32 mph.
// Released LU_MIN_RPM_HYST below it.
constexpr float LU_MIN_RPM[3] = {1250.0f, 1400.0f, 1800.0f};
constexpr float LU_MIN_RPM_HYST = 150.0f;
// Anti-lug: below LU_LUG_RPM, lockup only at light throttle (above it the
// converter's slip and torque multiplication do the work instead).
constexpr float LU_LUG_RPM = 1600.0f;
constexpr float LU_LUG_TPS = 25.0f;
// Highway lockup: in 4th at highway rpm, lockup holds up to a much higher
// throttle than the mode's normal limit, so long fast runs and highway grades
// stay locked: lower rpm, less fuel, and far less ATF heat. A wide-open
// kickdown still releases it, and in SPORT (and MANUAL) a quick stab of the
// pedal does too (tip-in release), so SPORT keeps its response.
constexpr bool HWY_LOCKUP[3] = {true, true, true};
constexpr float HWY_LU_MIN_RPM = 2000.0f;  // locked rpm in 4th: from ~51 mph
// A RAV4 needs most of its power to hold 90 mph (roughly half to two-thirds
// pedal), so only a near wide-open throttle releases highway lockup.
constexpr float HWY_LU_TPS_ON = 85.0f;
constexpr float HWY_LU_TPS_OFF = 95.0f;
constexpr float LU_TPS_ON[3] = {45.0f, 55.0f, 35.0f};   // SPORT drops lockup early
constexpr float LU_TPS_OFF[3] = {55.0f, 65.0f, 45.0f};

// ------------------------------------------------------ per-mode shift maps
// Upshift speed (mph) vs throttle at 0/25/50/75/100 %.
// [mode][shift: 0 = 1->2, 1 = 2->3, 2 = 3->4][tps point]
// WOT points sit at ~5.0-5.8k rpm with the default RPM_PER_MPH.
constexpr uint8_t TPS_POINTS = 5;
constexpr float UPSHIFT_MPH[3][3][TPS_POINTS] = {
    {  // ECO: short-shift
        {11.0f, 12.0f, 18.0f, 29.0f, 34.0f},
        {19.0f, 22.0f, 34.0f, 49.0f, 64.0f},
        {30.0f, 33.0f, 46.0f, 67.0f, 93.0f},
    },
    {  // NORMAL
        {11.0f, 15.0f, 24.0f, 33.0f, 36.0f},
        {22.0f, 30.0f, 43.0f, 58.0f, 66.0f},
        {34.0f, 41.0f, 58.0f, 78.0f, 97.0f},
    },
    {  // SPORT: hold gears
        {14.0f, 21.0f, 31.0f, 36.0f, 37.0f},
        {26.0f, 37.0f, 52.0f, 63.0f, 68.0f},
        {41.0f, 52.0f, 71.0f, 93.0f, 104.0f},
    },
};
// Downshift = upshift(tps) - hysteresis. Small hysteresis = eager kickdown.
constexpr float DOWNSHIFT_HYST_MPH[3] = {7.0f, 7.5f, 4.5f};
constexpr float MIN_DOWNSHIFT_MPH = 2.0f;

// ----------------------------------------------- per-mode driving features
// Index: 0 = ECO, 1 = NORMAL, 2 = SPORT.

// Corner hold: no automatic shifts (up or down, kickdown included) while the
// car is cornering, so a shift can't unsettle it mid-turn. Manual [+]/[-] and
// the over-rev upshift still work. Lateral g comes from the gyro's yaw rate x
// road speed (MPU-6050 on I2C). No gyro, or no bias learned yet = no hold.
constexpr bool IMU_INSTALLED = true;
constexpr bool CORNER_HOLD[3] = {true, true, true};
constexpr float CORNER_ENTER_G = 0.25f;
constexpr float CORNER_EXIT_G = 0.12f;
constexpr uint32_t CORNER_EXIT_MS = 700;          // straight this long before shifting resumes
constexpr uint32_t CORNER_HOLD_MAX_MS = 10000;    // long sweeper or bad sensor: stop holding
constexpr float CORNER_MIN_MPH = 12.0f;

// Brake downshift: while braking in a straight line, step down one gear at a
// time as long as the lower gear lands at or below this rpm, so the car is in
// the right gear for the exit. After it, the gear is held until the driver
// opens the throttle or BRAKE_HOLD_MS pass after releasing the brake.
// Note: in D, 1st and 2nd freewheel on one-way clutches, so only 4->3 adds
// engine braking; lower steps just pre-select the exit gear.
constexpr bool BRAKE_DOWNSHIFT[3] = {false, false, true};
constexpr float BRAKE_DOWNSHIFT_RPM[3] = {0.0f, 0.0f, 4200.0f};
constexpr float BRAKE_DOWNSHIFT_MIN_DECEL = 2.0f;  // mph per second
constexpr uint32_t BRAKE_HOLD_MS = 3000;
constexpr float BRAKE_HOLD_TPS = 20.0f;

// Coast unlock: release the converter clutch on a closed throttle, so the
// converter is already slipping (multiplying torque) when you get back on it.
// ECO keeps lockup on the coast for fuel cut.
constexpr bool LU_COAST_RELEASE[3] = {false, false, true};
constexpr float COAST_TPS_OFF = 2.0f;             // release below this throttle
constexpr float COAST_TPS_ON = 5.0f;              // allow re-lock above this

// Throttle smoothing for shift decisions: the shift map sees a low-passed
// throttle with this time constant, so a quick stab of the pedal doesn't
// cause a downshift; only a sustained push does. A kickdown (>= KICKDOWN_TPS)
// always uses the raw throttle, so it is never delayed.
constexpr uint32_t SHIFT_TPS_SMOOTH_MS[3] = {800, 250, 0};

// Lift-off hold: after a hard push (>= LIFT_FROM_TPS), lifting quickly
// (to <= LIFT_TO_TPS within LIFT_WINDOW_MS) holds the gear instead of
// upshifting, for up to LIFT_HOLD_MS or until the throttle comes back. Keeps
// SPORT in gear between corners instead of short-shifting on every lift.
constexpr bool LIFT_HOLD[3] = {false, false, true};
constexpr float LIFT_FROM_TPS = 40.0f;
constexpr float LIFT_TO_TPS = 10.0f;
constexpr uint32_t LIFT_WINDOW_MS = 500;
constexpr uint32_t LIFT_HOLD_MS = 4000;
constexpr float LIFT_RELEASE_TPS = 20.0f;

// Tip-in release: a fast stab of the pedal releases lockup at once, even
// below the mode's TPS limit, so the converter multiplies torque right away.
constexpr bool LU_TIPIN_RELEASE[3] = {false, false, true};
constexpr float TIPIN_RATE_PCT_PER_S = 150.0f;    // e.g. +15 % throttle within 0.1 s

}  // namespace cfg
