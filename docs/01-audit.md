# Audit: "Unified TCU & Telemetry Dashboard" spec (rev 0)

Scope: the original spec covered the architecture, the I/O matrix, the solenoid table, the
pseudocode framework and the three directives. Each finding below gives what is wrong, what
would happen in the car, and how the revised design fixes it (`docs/02-revised-spec.md`,
`firmware/tcu/`).

Severity:
- **CRITICAL**: can stall the engine, cause an unexpected shift or over-rev, or destroy hardware.
- **HIGH**: the feature doesn't work or isn't safe as specified.
- **MEDIUM**: works badly or is fragile.
- **LOW**: cosmetic or a portability issue.

Toyota service text for the 1999 RAV4 confirms the two most important corrections: the
solenoid logic (C1) and the +12 V high-side drive (C2). Items still marked *verify* must be
confirmed against the factory repair manual (RM) and electrical wiring diagram (EWD) for your
VIN before wiring anything. See `02-revised-spec.md` §0 for what is and isn't confirmed.

---

## Summary

| # | Severity | Finding |
|---|---|---|
| C1 | CRITICAL | Solenoid truth table is wrong for the 1999 RAV4: every gear maps to a different real gear |
| C2 | CRITICAL | Low-side N-MOSFET drivers can't switch Toyota's case-grounded, high-side-driven solenoids; no flyback protection |
| C3 | CRITICAL | Lockup has no speed or brake condition, so the engine stalls when you stop in 3rd/4th |
| C4 | CRITICAL | SL latches HIGH when you change modes, and is never released for shifts or low gears |
| C5 | CRITICAL | The ECU isolation relay is never driven and the master switch has no pin, so the ECU and TCU fight over the solenoids |
| C6 | CRITICAL | No over-rev protection, the directive covers only 1st, and VSS isn't wired at all |
| H1 | HIGH | Directive 2 is backwards: releasing lockup on hot ATF makes it hotter |
| H2 | HIGH | Raspberry Pi Pico is electrically incompatible (3.3 V pins, 5 V sensors, only 3 ADC pins) |
| H3 | HIGH | The "debounce" is an auto-repeat: holding [+] for 0.75 s shifts 1→4 |
| H4 | HIGH | Engaging mid-drive starts in gear 1 regardless of road speed |
| H5 | HIGH | Gear selector (PRNDL) isn't read, so the TCU shifts in P/R/N/2/L |
| H6 | HIGH | ECO/SPORT "shift maps" don't exist, and CUSTOM never shifts by itself |
| H7 | HIGH | No watchdog or defined reset state; outputs can latch or float |
| H8 | HIGH | Dummy-load resistors are undersized (13.8 W continuous on a 10 W part) |
| H9 | HIGH | Unprotected 12 V inputs, supply and battery divider |
| M1 | MEDIUM | Serial port and baud mismatch with the Nextion; printing can block |
| M2 | MEDIUM | ATF thermistor is read nowhere and has no conversion |
| M3 | MEDIUM | Raw, uncalibrated TPS thresholds without hysteresis |
| M4 | MEDIUM | No sensor fault detection |
| M5 | MEDIUM | No lockup release before a shift |
| M6 | MEDIUM | Relay coil is driven straight from an MCU pin |
| M7 | MEDIUM | Telemetry is incomplete |
| L1 | LOW | Functions used before they're declared (fails outside the Arduino IDE) |
| L2 | LOW | I/O matrix is missing VSS, tach, brake, PNP and ECU-sense signals |

---

## CRITICAL

### C1. Solenoid truth table is wrong
The spec's table is 1st = S1+S2, 2nd = S1, 3rd = none, 4th = S2. Toyota's service text for the
1999 RAV4 has S1 on in 1st and 2nd, with S2 joining it in 2nd, which matches the standard
Toyota 4-speed table:

| Gear (D range) | S1 | S2 |
|---|---|---|
| 1st | ON | OFF |
| 2nd | ON | ON |
| 3rd | OFF | ON |
| 4th (OD) | OFF | OFF |

With the spec's table, a command for "1st" engages **2nd**, "2nd" engages **1st**, "3rd"
engages **OD** and "4th" engages **3rd**. So the first sequential upshift (1→2) is really a
downshift (2nd→1st), and the next one (2→3) jumps from 1st to overdrive. The 1st-gear safety
directive would also guard the wrong gear.

The correct table also explains Toyota's fail-safe: with both solenoids dead, the hydraulics
give OD in D, 3rd in "2" and 1st in "L". That's why "relay off = factory ECU" is the right
default.

**Fix:** corrected table in `tcu_logic.cpp` (`kS1`/`kS2`). It's unit-tested, and the
locked-converter ratio check trips if the gear actually engaged doesn't match the command
(for example, S1 and S2 swapped in the harness).

### C2. The driver topology can't work on this transaxle
On the 1999 RAV4, current flows from the ECM's S1/S2/SL terminals through each solenoid to
ground (Toyota service text). In other words, the solenoids are **single-wire** parts grounded
through the transaxle case, and the ECM switches **+12 V** to them. An IRLZ44N used as a
low-side switch has no return path to interrupt. Wired anyway, it either does nothing or ends
up shorting the ECU's high-side output. The spec also leaves out flyback protection: each
solenoid (11–15 Ω, roughly 1 A) dumps its stored energy into the switch at turn-off. And with a
Pico, an IRLZ44N at 3.3 V gate drive isn't fully enhanced.

**Fix:** use protected **high-side smart switches** (PROFET class, with built-in inductive
clamp, short-circuit and over-temperature protection, and a current-sense pin for future
diagnostics). Logic-level inputs have pull-downs.

### C3. Lockup can stall the engine
```cpp
if (currentGear >= 3 && tpsVal < 300) digitalWrite(PIN_SL, HIGH);
```
Nothing in that condition checks road speed or the brake. In ECO, coasting to a stop in 3rd
(which manual mode allows) keeps the converter locked down to 0 km/h. A locked converter at a
stop stalls the engine, and with it you lose power steering and brake-booster assist. The I/O
matrix has no brake switch input at all.

**Fix:** lockup is only allowed in 3rd/4th, above a per-mode speed with hysteresis, with the
brake released, the ATF warm and the shift settled. The brake and low speed release it
**immediately**. The speed condition alone prevents a stall even if the brake input fails.

### C4. SL latches in NORMAL/SPORT
SL is only written in the ECO branch (both states) and the SPORT branch (only LOW). Lock up in
ECO, switch to NORMAL or SPORT, and SL stays HIGH forever: through downshifts to 2nd and 1st and
down to a standstill, which stalls the engine (C3). NORMAL has no lockup logic at all.

**Fix:** one lockup function handles every mode, gear and state, and STOCK forces SL off.

### C5. ECU isolation is never actuated
`PIN 6` (relay) is in the matrix but never set to OUTPUT and never written. The "Master Mode
Toggle (Stock vs Manual)" has no pin and no code. As written, the TCU drivers and the ECU's
output stages drive the same solenoid wires at the same time, and there's no STOCK mode.

**Fix:** a master switch on D26 (closed = CUSTOM, so an open or broken wire means STOCK), one
DPDT relay per solenoid, and a defined handover sequence. Relay de-energised (power loss,
reset, fault) = factory ECU in control, enforced by hardware pull-downs.

### C6. No over-rev protection, and no VSS
Directive 1 asks for a 1st-gear guard "using VSS interrupt data", but VSS isn't in the I/O
matrix and the code has no guard at all. The directive is also too narrow. With nominal
1999 RAV4 ratios, a 3→2 downshift at 130 km/h puts the engine around 6,700 rpm, and a
2→1 downshift at 90 km/h about 8,400 rpm. In D, 1st and 2nd drive through one-way clutches, so a high-speed downshift
into them won't always over-rev on a closed throttle. It does the moment you open the
throttle, though, and in the 2/L ranges the engine-braking bands apply.

A guard that trusts VSS also needs VSS to be **proven alive**. A broken VSS wire reads
0 km/h, and a guard fed 0 km/h would happily allow 1st at highway speed.

**Fix:**
- Every downshift is checked: `predicted rpm = km/h × RPM_PER_KMH[target] ≤ 5,800`.
- There's also a hard 45 km/h cap for 1st that doesn't depend on the ratio calibration.
- VSS is checked for plausibility two ways:
  - Speed collapsing faster than any car can brake means signal loss.
  - High rpm and throttle in a driving gear with 0 km/h means a dead sensor.
- A locked-converter rpm/speed cross-check catches a mis-scaled VSS.
- Any VSS fault hands control back to the ECU and stays latched.

---

## HIGH

### H1. Directive 2 is backwards
"Force disengagement of torque converter lockup if ATF exceeds 110 °C" makes the problem
worse. Converter **slip** is the transmission's biggest heat source; a locked converter
generates almost none. OEM strategies do the opposite: they apply the converter clutch
*earlier and in more conditions* when the fluid is hot, and warn the driver. Releasing lockup
at 110 °C on a hot climb would push the temperature up faster.

**Fix:**
- Below 35 °C ATF (*cold*): lockup is inhibited, which is normal OEM practice.
- At 110 °C and above (*hot*): the lockup window widens (40 km/h, up to 85 % throttle) and the
  display shows a HOT warning.
- At 125 °C and above: a CRITICAL warning.
- If the sensor fails, lockup goes back to the normal rules.

### H2. Raspberry Pi Pico is incompatible as specified
The RP2040's GPIOs and ADC are 3.3 V and **not 5 V-tolerant** (absolute maximum about
3.6 V). The TPS reaches 4.2 V and the oil sender 4.5 V, so either would damage the chip. The
Pico board also exposes only **3** ADC inputs, and the spec needs 4. The code assumes a
10-bit, 5 V ADC (`/1023.0*5.0`).

**Fix:** standardise on the **Arduino Mega 2560**: 5 V I/O, 16 ADC inputs, 4 UARTs, 6 external
interrupts. Using an RP2040 later would mean level shifting plus an external ADC.

### H3. Cooldown is not debounce
The 250 ms cooldown re-fires while the button is held: holding [+] for 0.75 s shifts 1→2→3→4.
250 ms is also shorter than a shift on this transaxle takes to complete hydraulically (about 0.5–1 s), so
commands can overlap. The inputs use only the AVR's internal 20–50 kΩ pull-ups on a cable run
of a few metres up the A-pillar, alongside ignition and blower-motor noise.

**Fix:**
- **Hardware:** 2.2 kΩ external pull-ups (a few mA of wetting current), a 1 kΩ series resistor
  plus 100 nF at the MCU, clamp diodes, and a shield grounded at the TCU end only.
- **Software:** a 25 ms stable-level debouncer, then **edge-triggered** shifts: one press
  gives one shift, and holding never repeats.
- A 700 ms minimum shift interval, and only single-step shifts.
- A stuck-button warning.

### H4. No gear sync at handover
`currentGear = 1` at boot. Flip to CUSTOM at 120 km/h and the TCU immediately commands "1st".
With C1, that's actually 2nd, at about 6,200 rpm.

**Fix:** the TCU senses the ECU's own S1/S2/SL outputs on the ECU side of the relays. CUSTOM
starts in the gear and lockup state the ECU is holding (a bumpless handover). Without sensing,
CUSTOM can only engage below 3 km/h, in 1st. CUSTOM also needs a fresh STOCK→CUSTOM flip after
every boot, so a reset mid-drive never silently re-engages.

### H5. Gear selector not read
The TCU would keep commanding solenoids in P, R, N, 2 and L, where the manual valve gives
different results. It also can't re-sync after D→N→D.

**Fix:** the PNP switch "D" signal goes in through an optocoupler. Outside D, control goes back
to the ECU, which handles R/2/L. Back in D, the TCU re-syncs to the ECU's gear. A broken wire
reads "not D", which fails safe.

### H6. Drive-mode maps don't exist
The spec promises an ECO "short-shift efficiency map" and a SPORT "aggressive shift-hold map".
The code only changes lockup. CUSTOM has no automatic shifting, so if you forget to downshift,
the car stops and pulls away in 4th.

**Fix:**
- **CUSTOM-AUTO:** per-mode upshift tables against throttle position, with per-mode downshift
  hysteresis, so kickdown happens naturally. Every shift goes through the same guards.
- **CUSTOM-MANUAL:** tap [+] or [-], tiptronic-style. While you slow down, the TCU forces
  downshifts so the car never stops in 3rd or 4th, and it forces an upshift at the rev limit.
  Hold [+] for 1 s to go back to AUTO.

### H7. No watchdog and no defined reset state
A firmware hang leaves the solenoids in their last state. During reset or programming, AVR
pins are high-Z, so the MOSFET gates float and the relay state is undefined.

**Fix:**
- A 250 ms hardware watchdog. A watchdog reset is reported and needs the master switch to be
  re-armed.
- 10 kΩ pull-downs on every driver input and on the relay driver.
- `setup()` writes LOW before `pinMode(OUTPUT)`.

Some older Mega 2560 bootloaders mishandle watchdog resets. Run the watchdog test in the
bring-up plan, and reflash the bootloader if it boot-loops.

### H8. Dummy loads are undersized
15 Ω at 14.4 V charging voltage is **13.8 W continuous**. S2 is held on through all of 2nd and
3rd, and S1 through 1st and 2nd, so these are continuous loads, not occasional pulses. A 10 W
part overheats, and the spec doesn't say how many are needed (three: S1, S2, SL).

**Fix:** 3 × 15 Ω **50 W aluminium-clad** resistors on a heatsink, away from wiring, with
fusing.

### H9. Unprotected electrical interfaces
- The battery divider (×3.2) reaches 5 V at 16 V and has no clamp. A load dump puts tens of
  volts on the ADC pin.
- The "12V-to-5V 3A buck" has no transient, reverse-polarity or fuse requirements.
- The 12 V-level signals (brake, PNP, ECU outputs, VSS, tach) would need conditioning, but the
  spec doesn't include them at all.

**Fix:**
- A 47 k/10 k divider (28.5 V full scale) with an RC filter and clamp.
- An automotive buck with a 40 V+ input rating, behind a fuse, a reverse-polarity element and a
  TVS.
- Optocouplers for every 12 V signal.
- The tach is taken from the ECU/DLC TAC signal, **never the coil primary**, which carries
  hundreds of volts.

---

## MEDIUM

- **M1. Serial and baud mismatch.** The matrix says TX1, but the code uses `Serial` (USB/TX0).
  Nextion displays ship at 9600 baud, not 115200. At 9600, the current 45-byte frame already
  takes about 47 ms to drain. Add the missing fields and it outgrows the 64-byte TX buffer, at
  which point `Serial.print` blocks the control loop.
  **Fix:** Nextion on `Serial1`. At boot the firmware sends `baud=115200` at 9600 (RAM only, so
  no EEPROM wear). It only sends changed fields, at most two commands per 20 ms tick, and only
  when the buffer has room. USB stays free for a CSV calibration log.
- **M2. ATF temperature unused.** `PIN_TRANS` is declared and never read; there's no
  thermistor maths.
  **Fix:** Beta-model conversion with a 1 kΩ pull-up (best resolution at 90–110 °C), plus
  open/short detection.
- **M3. TPS thresholds.** Raw ADC 300 and 600 are uncalibrated (300 counts is about 26 %
  throttle on a 0.5–4.2 V sensor) and have no hysteresis, so lockup would chatter at the
  boundary. The TPS is shared with the engine ECU, so the tap must be high-impedance and
  referenced to the ECU's sensor ground.
  **Fix:** calibrated 0–100 %, hysteresis on every threshold, and a 10 kΩ series tap.
- **M4. No sensor fault detection.**
  **Fix:** out-of-range voltages flag faults. TPS, VSS, supply and gear-ratio faults hand
  control back to the ECU, latched. Faults on the ATF or oil sensors raise warnings only.
- **M5. No lockup release before a shift.** That means harsh shifts and converter-clutch wear.
  **Fix:** SL releases, the gear changes 200 ms later, and lockup can't re-apply until the shift
  has settled (1.5 s).
- **M6. Relay coil on an MCU pin.** An automotive DPDT coil draws about 100 mA; an AVR pin is
  rated 40 mA absolute maximum.
  **Fix:** a logic-level MOSFET or ULN2803 driving a 12 V coil, with a flyback diode.
- **M7. Incomplete telemetry.** Only oil, volts and gear were sent.
  **Fix:** adds speed, rpm, TPS, ATF, lockup, control state, mode, faults, warnings and an
  event message (for example `DENIED: OVER-REV`).

## LOW

- **L1.** Functions are called before they're declared. That works in the Arduino IDE (it
  generates prototypes) but fails as a PlatformIO `.cpp`. The new code is ordered correctly and
  builds both ways.
- **L2.** The I/O matrix lacks VSS, tach, brake, the PNP "D" signal and ECU sensing. All are
  added in the revised pin map.

## What the original spec got right
- Isolating the ECU with dummy loads is the right approach: the factory ECU stays the fallback.
  It just needs a fail-safe default.
- The overhead console concept and the 8-core shielded run.
- The ratiometric oil-pressure formula (`(V − 0.5) × 25`) is correct for a 0.5–4.5 V,
  0–100 psi sender.
- A non-blocking, timestamp-scheduled loop.
- 110 °C is the right threshold for "hot ATF". It's only the action at that temperature that
  was inverted.

## How the three directives are now met

| Directive | Implementation |
|---|---|
| 1. Downshift safety via VSS | Every downshift is rpm-predicted and checked against 5,800 rpm, with a hard 45 km/h cap for 1st. VSS gets a deceleration and stall plausibility check plus a locked-converter ratio check. In MANUAL, the TCU forces an upshift at 6,200 rpm. |
| 2. Thermal protection | Corrected physics: cold inhibits lockup; hot widens the lockup window and warns; 125 °C raises a critical warning. Sensor faults fall back to the normal rules. |
| 3. Debouncing | RC filtering, external pull-ups and clamps in hardware, plus a 25 ms stable-level software debouncer, edge-triggered shifting, a 700 ms shift interval and stuck-button detection. |
