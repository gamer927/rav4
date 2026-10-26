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
// the over-rev guard is only as good as RPM_PER_KMH and VSS_PULSES_PER_KM.
constexpr bool CALIBRATED = false;

// ---------------------------------------------------------------- driveline
// Engine RPM per km/h in each gear with the torque converter LOCKED.
// Index 0 is unused. Defaults: 1999 RAV4 automatic ratios as published
// (2.810 / 1.549 / 1.000 / 0.734, final drive ~4.40) on the factory
// 215/70R16 tyres (~455 rev/km). Published final-drive figures for this car
// disagree (3.18-4.40), so CALIBRATE from logged data (§8.3) before arming.
constexpr float RPM_PER_KMH[5] = {0.0f, 93.8f, 51.7f, 33.4f, 24.5f};

// ------------------------------------------------------------------ sensors
constexpr float ADC_VREF = 5.0f;

// Vehicle speed sensor. Toyota meter-driven SPD is typically 4 pulses/rev at
// 637 rev/km = 2548 pulses/km. CALIBRATE against GPS.
constexpr float VSS_PULSES_PER_KM = 2548.0f;
constexpr uint32_t VSS_MIN_PERIOD_US = 2000;      // reject glitches (> ~700 km/h)
constexpr uint32_t VSS_TIMEOUT_US = 1500000;      // no pulse for 1.5 s -> 0 km/h

// Engine tach (4-cyl, wasted-spark igniter): 2 pulses per crank revolution.
constexpr bool TACH_INSTALLED = true;
constexpr float TACH_PULSES_PER_REV = 2.0f;
constexpr uint32_t TACH_MIN_PERIOD_US = 1500;     // reject glitches (> 20k rpm)
constexpr uint32_t TACH_TIMEOUT_US = 200000;      // no pulse for 200 ms -> 0 rpm

// Factory ECU S1/S2/SL outputs sensed upstream of the relays (opto inputs).
// Set false if not wired: CUSTOM can then only be entered at a standstill.
constexpr bool ECU_SENSE_INSTALLED = true;

// Throttle position (linear VTA). CALIBRATE closed/WOT volts on the car.
constexpr float TPS_CLOSED_V = 0.60f;
constexpr float TPS_WOT_V = 4.00f;
constexpr float TPS_VALID_MIN_V = 0.15f;          // below = shorted / open ground
constexpr float TPS_VALID_MAX_V = 4.85f;          // above = open signal / short to VC

// 0-100 psi ratiometric oil transducer (0.5-4.5 V).
constexpr float OIL_V_ZERO = 0.5f;
constexpr float OIL_V_FULL = 4.5f;
constexpr float OIL_PSI_FULL = 100.0f;
constexpr float OIL_VALID_MIN_V = 0.25f;
constexpr float OIL_VALID_MAX_V = 4.75f;

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

// ------------------------------------------------------------------- timing
constexpr uint32_t CONTROL_PERIOD_MS = 10;
constexpr uint16_t DEBOUNCE_SWITCH_MS = 25;       // console switches (A-pillar run)
constexpr uint16_t DEBOUNCE_OPTO_MS = 40;         // brake / lever / ECU sense
constexpr uint32_t MIN_SHIFT_INTERVAL_MS = 700;   // one shift must finish first
constexpr uint32_t AUTO_MIN_TIME_IN_GEAR_MS = 1200;
constexpr uint32_t LOCKUP_RELEASE_BEFORE_SHIFT_MS = 200;
constexpr uint32_t LOCKUP_SETTLE_AFTER_SHIFT_MS = 1500;
constexpr uint32_t LOCKUP_MIN_OFF_MS = 1000;      // anti-chatter before re-apply
constexpr uint32_t LONG_PRESS_MS = 1000;          // hold [+] = back to AUTO
constexpr uint32_t STUCK_BUTTON_MS = 10000;

// ------------------------------------------------------------------- safety
constexpr float STATIONARY_KMH = 3.0f;
constexpr float MAX_DOWNSHIFT_RPM = 5800.0f;      // predicted rpm after downshift
constexpr float FIRST_GEAR_MAX_KMH = 45.0f;       // hard cap, independent of ratios
constexpr float REV_LIMIT_UPSHIFT_RPM = 6200.0f;
constexpr uint32_t REV_LIMIT_HOLD_MS = 150;
constexpr bool MANUAL_REV_LIMIT_UPSHIFT = true;
// MANUAL: forced downshift below these speeds (index = current gear) so the
// car never stops in 3rd/4th. Set [2] to 0 to allow 2nd-gear (snow) starts.
constexpr float MANUAL_MIN_KMH[5] = {0.0f, 0.0f, 8.0f, 18.0f, 35.0f};
constexpr float UPSHIFT_MARGIN_KMH = 4.0f;        // no upshift into a forced-downshift zone
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
constexpr float VSS_DROP_CHECK_MIN_KMH = 15.0f;
constexpr float VSS_MAX_DROP_KMH_PER_100MS = 8.0f;

// Locked-converter ratio check: catches swapped S1/S2, wrong gear, bad VSS.
constexpr bool RATIO_CHECK_ENABLED = true;
constexpr float RATIO_CHECK_MIN_KMH = 30.0f;
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
constexpr float HOT_LU_ON_KMH = 40.0f;
constexpr float HOT_LU_OFF_KMH = 34.0f;
constexpr float HOT_LU_TPS_ON = 85.0f;
constexpr float HOT_LU_TPS_OFF = 95.0f;

constexpr float OIL_LOW_PSI = 5.0f;
constexpr float OIL_CHECK_RPM = 600.0f;
constexpr uint32_t OIL_LOW_MS = 2000;

// ------------------------------------------------------- per-mode lockup map
// Index: 0 = ECO, 1 = NORMAL, 2 = SPORT.
constexpr uint8_t LU_MIN_GEAR = 3;
constexpr float LU_ON_KMH[3] = {50.0f, 58.0f, 70.0f};
constexpr float LU_OFF_KMH[3] = {44.0f, 52.0f, 64.0f};
constexpr float LU_TPS_ON[3] = {45.0f, 55.0f, 35.0f};   // SPORT drops lockup early
constexpr float LU_TPS_OFF[3] = {55.0f, 65.0f, 45.0f};

// ------------------------------------------------------ per-mode shift maps
// Upshift speed (km/h) vs throttle at 0/25/50/75/100 %.
// [mode][shift: 0 = 1->2, 1 = 2->3, 2 = 3->4][tps point]
// WOT points sit at ~5.0-5.8k rpm with the default RPM_PER_KMH.
constexpr uint8_t TPS_POINTS = 5;
constexpr float UPSHIFT_KMH[3][3][TPS_POINTS] = {
    {  // ECO: short-shift
        {17.0f, 19.0f, 29.0f, 46.0f, 55.0f},
        {31.0f, 36.0f, 54.0f, 79.0f, 103.0f},
        {48.0f, 53.0f, 74.0f, 108.0f, 150.0f},
    },
    {  // NORMAL
        {18.0f, 24.0f, 38.0f, 53.0f, 58.0f},
        {36.0f, 48.0f, 70.0f, 94.0f, 106.0f},
        {54.0f, 66.0f, 94.0f, 126.0f, 156.0f},
    },
    {  // SPORT: hold gears
        {22.0f, 34.0f, 50.0f, 58.0f, 60.0f},
        {42.0f, 60.0f, 84.0f, 102.0f, 110.0f},
        {66.0f, 84.0f, 114.0f, 150.0f, 168.0f},
    },
};
// Downshift = upshift(tps) - hysteresis. Small hysteresis = eager kickdown.
constexpr float DOWNSHIFT_HYST_KMH[3] = {11.0f, 12.0f, 7.0f};
constexpr float MIN_DOWNSHIFT_KMH = 3.0f;

}  // namespace cfg
