# Engineering Specification rev 1: Unified TCU & Telemetry Dashboard

**Vehicle:** 1999 Toyota RAV4 (first generation, SXA10 2WD / SXA11 4WD), 4-speed ECT automatic,
3S-FE 2.0 L engine
**Controller:** Arduino Mega 2560 (`firmware/tcu/`)
**Supersedes:** rev 0. The defects found in rev 0 are listed in [`01-audit.md`](01-audit.md).

> **Safety.** This system takes control of the drivetrain. Everything marked **VERIFY** must
> be confirmed against the factory repair manual (RM) and electrical wiring diagram (EWD) for
> your VIN. Calibrate everything marked **CALIBRATE** (§8) before you first select CUSTOM.
> Do all early testing off public roads (§9).

## 0. The 1999 RAV4: what's confirmed and what isn't

These are the facts the design depends on, with how sure each one is.

| Item | 1999 RAV4 value | Status |
|---|---|---|
| Engine | 3S-FE 2.0 L, 127 hp @ 5,400 rpm, 132 lb-ft @ 4,600 rpm | Published specs |
| Tyres | 215/70R16 (~455 rev/km) | Published specs and VIN decodes |
| Solenoid drive | ECM output S1/S2/SL → solenoid → transaxle case → ground (ECM switches +12 V) | Toyota service text for this car |
| Shift logic | S1 on in 1st and 2nd; S2 joins in 2nd; SL = lockup | Toyota service text for this car; full table in §5 |
| Solenoid connector | S1 / S2 / SL = terminals A3 / A1 / A2 (2WD), B3 / B6 / B5 (4WD) | Toyota service text; **VERIFY** on your harness |
| ECM inputs it shifts from | SPD (speed) and THW (coolant temperature), plus throttle | Toyota service text |
| Gear ratios | 2.810 / 1.549 / 1.000 / 0.734, reverse 2.296 (3rd is direct 1:1) | Two spec databases agree; **VERIFY** |
| Final drive | ~4.40 | Sources disagree (3.18–4.40); **CALIBRATE** |
| Transaxle code | 4WD sources say A540H or A241H; 2WD differs | **Read the ID plate** |
| Diagnostics | OBD-II (DLC3 under the dash) | 1996+ |

What this means:
- **The solenoid table and the +12 V high-side drive are confirmed for this car.** Those were the
  two most dangerous errors in rev 0, and they're fixed.
- **The ratios and final drive aren't confirmed.** The firmware therefore ships with
  `CALIBRATED = false`, and CUSTOM can't be armed until you've measured them (§8). Toyota's
  4-speed families (A140/A240/A540) all use the same S1/S2 table, so the transaxle-code
  question doesn't change the wiring or the logic, only the ratios.

---

## 1. Operating concept

| State | Who drives the solenoids | How you get there |
|---|---|---|
| **STOCK** | Factory ECU (relays de-energised) | Power-up, master switch at STOCK, lever not in D, any fault, MCU reset |
| **CUSTOM-AUTO** | TCU, using the ECO/NORMAL/SPORT shift and lockup maps | Master switch flipped STOCK→CUSTOM with the lever in D; or hold [+] for 1 s while in MANUAL |
| **CUSTOM-MANUAL** | TCU, holding the gear you selected | Tap [+] or [-] while in CUSTOM |

Rules that never change:
1. **De-energised = factory.** Power loss, reset, watchdog, firmware hang or a broken master
   switch wire all leave the factory ECU in control.
2. **CUSTOM must be armed by a fresh STOCK→CUSTOM flip** after every boot. Leaving the switch in
   CUSTOM doesn't arm it at key-on, and a reset mid-drive doesn't re-engage.
3. **The lever must be in D.** Outside D, the ECU handles P/R/N/2/L. Returning to D re-enters
   CUSTOM, synced to the ECU's current gear.
4. **Bumpless handover.** CUSTOM starts in the gear and lockup state the ECU is commanding at
   that moment, sensed from the ECU outputs. If it can't sense them, it only engages at a
   standstill, in 1st.
5. **Faults latch.** A critical fault hands control back to the ECU until the master switch is
   returned to STOCK, which acknowledges the fault.

## 2. Architecture

```
 OVERHEAD CONSOLE                        8-core shielded (shield grounded at TCU end only)
 [STOCK|CUSTOM] [ECO|NORM|SPORT] [+][-] LED ────────────────────────────────┐
                                                                            v
 ┌──────────────────────────── UNDER-DASH TCU ENCLOSURE ───────────────────────────────┐
 │  Fused IG feed → reverse-polarity + TVS → 5 V/3 A automotive buck → Mega 5 V, Nextion │
 │                                                                                       │
 │  Arduino Mega 2560 ── input conditioning (RC + clamps, optos for 12 V signals)        │
 │        │  D8/D9/D10 → 3 × high-side smart switch (PROFET) ──┐                         │
 │        │  D6 → MOSFET/ULN → 3 × DPDT relay coils             │                         │
 │        │  TX1 → Nextion 3.5"                                 v                         │
 │        │                           ┌── DPDT ×3 (S1, S2, SL) ─────────────┐             │
 │        │   ECU S1/S2/SL outputs ───┤ NC: ECU → solenoid    (de-energised) │             │
 │        │   (sensed via optos) ─────┤ NO: ECU → 15 Ω/50 W dummy            │             │
 │        │                           │ NO: TCU driver → solenoid (energised)│             │
 │        │                           └──────────────┬───────────────────────┘             │
 └────────┼──────────────────────────────────────────┼─────────────────────────────────────┘
          │                                          v
   Sensors: TPS tap, VSS, tach (TAC),        Transaxle solenoids S1, S2, SL
   brake (STP), PNP "D", oil, ATF, battery    (single-wire, case-grounded)
```

## 3. Hardware

### 3.1 Controller
**Arduino Mega 2560** (or a quality clone with an ATmega16U2 USB bridge).
- 5 V-tolerant I/O, so the 0.5–4.5 V sensors connect directly through protection.
- 16 ADC channels, 4 UARTs (USB log + Nextion), 6 external interrupts (VSS + tach).
- The Pico is rejected: its pins are 3.3 V and not 5 V-tolerant, and it has only 3 ADC inputs.

Power the Mega from the regulated 5 V rail into its **5 V pin**. Don't feed 12 V into VIN: the
on-board linear regulator overheats and has no transient protection.

### 3.2 Power
- Ignition-switched (IG) feed with a 5 A fuse for the solenoid drivers and a 2 A fuse for the
  logic.
- Reverse-polarity protection: a P-FET ideal diode or a Schottky.
- A TVS sized for load dump (for example SMCJ24A) close to the enclosure entry.
- A 5 V/3 A automotive buck converter with an input rating of 40 V or more and LC input
  filtering.
- Use star grounding. Run a dedicated ground wire from the TCU to the engine/ECU ground point
  (E1), because the TPS is measured against the ECU's sensor ground.

### 3.3 Solenoid drivers (replaces the IRLZ44N low-side board)
On the 1999 RAV4 the ECM **switches +12 V** to each solenoid, and current returns through the
transaxle case to ground (Toyota service text, §0). Confirm on your car: with the ignition on
and the ECM commanding a solenoid, you should measure about 12 V between the ECM's S1 terminal
and ground.

- Use 3 × **high-side smart switches** (PROFET class, for example Infineon BTS6143D or
  BTS50085), each rated 3 A continuous or more. They have a built-in inductive clamp, short and
  over-temperature protection, and a current-sense (IS) pin for future open/short diagnostics.
- Put a 10 kΩ pull-down on every IN pin, so the drivers are off during reset.
- If you use a discrete P-FET instead: add an NPN level shifter, a gate pull-up to +B (off), and
  a freewheel diode across the solenoid (cathode to the driver output, anode to ground).
  A freewheel diode slows solenoid release. A diode plus 24 V zener clamp releases faster and is
  closer to the ECU's behaviour.

### 3.4 ECU isolation relays and dummy loads
Use one **automotive DPDT relay per solenoid**: 12 V coil, contacts rated for 5 A DC or more.

```
                       DPDT relay (×3: S1, S2, SL)
ECU output ──┬──── COM-A ─┬─ NC-A ───────────────┬──── to solenoid (case ground)
             │            └─ NO-A ── 15 Ω 50 W ── GND
     opto sense (ECU side,
     valid in both states)
TCU driver ───────── COM-B ─┬─ NO-B ───────────────┘
                            └─ NC-B ── (open)

Coil OFF: ECU → solenoid, TCU disconnected          (factory behaviour)
Coil ON : ECU → dummy (no DTC), TCU → solenoid
```

- **Dummy loads:** 3 × 15 Ω **50 W aluminium-clad** resistors on a heatsink. Each dissipates
  13.8 W continuously at 14.4 V. That's within the solenoid's 11–15 Ω spec (VERIFY), so the ECU
  sees a normal load.
- **Coils:** drive all three from D6 through one logic-level MOSFET or a ULN2803, with a
  flyback diode and a 10 kΩ pull-down on the gate.
- **Handover:** the firmware sets the driver outputs to the ECU's current state *before*
  energising the relays, so the only disturbance is the contacts' break-before-make gap
  (about 5–10 ms). That's shorter than the hydraulic response.

### 3.5 Input conditioning

| Signal | Source (VERIFY in the EWD) | Conditioning |
|---|---|---|
| Console switches (UP, DOWN, ECO, SPORT, MASTER) | Switch to ground | 2.2 kΩ pull-up to 5 V, 1 kΩ series, 100 nF to ground at the MCU, BAT54S clamp |
| Brake | Stop-lamp switch STP (+12 V when pressed) | PC817 opto (LED + 2.2 kΩ), output pulls the MCU pin low |
| Lever in D | PNP switch "D" output (feeds the D indicator) | PC817 opto as above |
| ECU S1/S2/SL sense | ECU side of each relay (COM-A) | PC817 opto with 3.3 kΩ (about 4 mA load) |
| VSS | SPD signal to the ECU (meter output, about 4 pulses/rev) | Comparator with hysteresis (LM393) or a 6N137 opto. High-impedance tap; never load the line |
| Tach | ECU/DLC TAC signal | 10 kΩ series resistor + 5.1 V zener, or an opto. **Never the coil primary** (hundreds of volts) |
| TPS | ECU VTA (tapped) | 10 kΩ series resistor (keeps the ECU's reading intact), 100 nF at the pin, BAT54S clamp |
| Oil pressure | 0–100 psi, 0.5–4.5 V sender powered from TCU 5 V (ratiometric) | 1 kΩ series resistor, 100 nF, clamp |
| ATF temperature | Inline NTC in the cooler line | 1 kΩ 0.1 % pull-up to 5 V, 100 nF |
| Battery | IG feed | 47 kΩ / 10 kΩ divider, 100 nF, 5.1 V zener |

### 3.6 Console cable (8-core shielded)
1. UP
2. DOWN
3. ECO
4. SPORT
5. MASTER
6. Switch common (ground)
7. LED (+, via a transistor in the TCU)
8. Spare

Ground the shield drain at the TCU end only. The rotary mode switch uses two wires: ECO and
SPORT both open means NORMAL, so a broken wire degrades to NORMAL.

## 4. Pin matrix (Arduino Mega 2560)

| Pin | Dir | Signal | Active | Notes |
|---|---|---|---|---|
| D2 (INT4) | in | VSS pulses | falling edge | Comparator or opto output |
| D3 (INT5) | in | Tach pulses | falling edge | 2 pulses/rev (4-cyl) |
| D6 | out | Relay bank driver | HIGH = TCU in control | 10 k pull-down; MOSFET/ULN, never the coil directly |
| D7 | out | Console LED | HIGH | Via transistor |
| D8 | out | S1 high-side IN | HIGH = on | 10 k pull-down |
| D9 | out | S2 high-side IN | HIGH = on | 10 k pull-down |
| D10 | out | SL high-side IN | HIGH = on | 10 k pull-down |
| D13 | out | Status LED (on-board) | HIGH | Mirrors D7 |
| D18 (TX1) | out | Nextion RX | – | D19/RX1 ← Nextion TX (unused) |
| D22 | in | Upshift [+] | LOW | Edge-triggered |
| D23 | in | Downshift [-] | LOW | Edge-triggered |
| D24 | in | Mode ECO | LOW | |
| D25 | in | Mode SPORT | LOW | |
| D26 | in | Master switch | LOW = CUSTOM | Open = STOCK (fail-safe) |
| D27 | in | Brake | LOW = pressed | Opto |
| D28 | in | Lever in D | LOW = in D | Opto; open = "not D" (fail-safe) |
| D29 | in | ECU S1 sense | LOW = ECU S1 on | Opto |
| D30 | in | ECU S2 sense | LOW = ECU S2 on | Opto |
| D31 | in | ECU SL sense | LOW = ECU SL on | Opto |
| A0 | an | TPS | – | 10 k series tap |
| A1 | an | Oil pressure | – | |
| A2 | an | ATF thermistor | – | 1 k pull-up |
| A3 | an | Battery | – | ×5.7 divider |
| USB | – | CSV log, 115200 baud | – | Calibration (§8) |

The shift buttons moved off D2/D3 because VSS and tach need interrupt pins. Serial1 (the
Nextion) already occupies interrupt pins 18/19, so D2/D3 are the natural choice. The buttons
don't need interrupts; they're polled and debounced.

## 5. Solenoid logic (1999 RAV4, D range)

| Gear | S1 | S2 | SL (lockup) |
|---|---|---|---|
| 1st | **ON** | OFF | OFF |
| 2nd | **ON** | **ON** | OFF |
| 3rd | OFF | **ON** | per lockup rules (§6.5) |
| 4th (OD) | OFF | OFF | per lockup rules (§6.5) |

- With both shift solenoids off, the hydraulics give OD in D (also the hydraulic fail-safe).
  The same table decodes the ECU's current gear from the sensed outputs.
- Line pressure stays mechanical, controlled by the throttle cable, so the TCU doesn't touch it.
- VERIFY that SL is an on/off solenoid on your unit. If your ECU drives it with a duty cycle,
  the sense opto will flicker, and the lockup output needs rethinking.
- 4WD (SXA11): leave any transfer or centre-diff circuits on the factory ECU.

## 6. Control logic (`firmware/tcu/tcu_logic.cpp`)

### 6.1 Loop
- Switches are sampled on every pass and debounced: 25 ms for console switches, 40 ms for optos.
- The controller runs every **10 ms**: it reads the inputs, steps the logic and applies the
  outputs.
- Nextion telemetry is serviced every 20 ms without blocking.
- The USB CSV log runs at 10 Hz.
- The 250 ms watchdog is kicked on every pass.

### 6.2 Shift requests (manual, auto and protective all use the same path)
A request is **refused** if any of these is true:
- The target is outside 1–4, or isn't exactly one step from the current gear.
- A previous shift happened less than **700 ms** ago, or one is still pending.
- **Downshift:** `km/h × RPM_PER_KMH[target] > 5,800`, or the target is 1st above **45 km/h**.
- **Upshift:** speed is below the MANUAL forced-downshift threshold for the target gear plus a
  4 km/h margin. Without this, the TCU would immediately undo the shift.

If the request is accepted with lockup applied, SL releases first and the gear changes
**200 ms** later. After any gear change, lockup stays inhibited for **1.5 s**.

### 6.3 CUSTOM-AUTO
- **Upshift** when `speed ≥ UPSHIFT_KMH[mode][gear](tps)`. The tables are interpolated at
  0/25/50/75/100 % throttle.
- **Downshift** when `speed < upshift(tps) − hysteresis[mode]` (ECO 11, NORMAL 12, SPORT 7 km/h).
  Opening the throttle raises the downshift line, which gives kickdown; the over-rev guard
  refuses any kickdown that would over-rev.
- At least **1.2 s** in gear between automatic shifts.

| Upshift km/h at 0 / 25 / 50 / 75 / 100 % TPS | 1→2 | 2→3 | 3→4 |
|---|---|---|---|
| ECO (short-shift) | 17/19/29/46/55 | 31/36/54/79/103 | 48/53/74/108/150 |
| NORMAL | 18/24/38/53/58 | 36/48/70/94/106 | 54/66/94/126/156 |
| SPORT (hold) | 22/34/50/58/60 | 42/60/84/102/110 | 66/84/114/150/168 |

With the 1999 RAV4 default ratios, the WOT points sit at about 5.0–5.7 k rpm (peak power is at
5,400). These are starting maps: re-check them against your calibrated `RPM_PER_KMH`.

### 6.4 CUSTOM-MANUAL
- A tap on [+] or [-] gives one shift. Holding a button never repeats. Any tap while in AUTO
  switches to MANUAL.
- Hold [+] for **1 s** to return to AUTO.
- **Forced downshift** when slowing below 35 km/h (4th), 18 km/h (3rd) or 8 km/h (2nd), so the
  car never stops in a high gear. To allow 2nd-gear snow starts, set `MANUAL_MIN_KMH[2] = 0`.
- **Forced upshift** at 6,200 rpm held for 150 ms (configurable).
- Refused requests show on the display: `DENIED: OVER-REV`, `DENIED: TOO SLOW` or
  `DENIED: SHIFTING`.

### 6.5 Torque-converter lockup
SL is **applied** when all of these are true:
- The gear is 3rd or 4th, with no shift pending and at least 1.5 s since the last shift.
- The brake is released.
- ATF is not cold.
- Speed ≥ the ON speed, and TPS ≤ the ON limit (table below).
- SL has been off for at least 1 s (anti-chatter).

SL is **released immediately** when any of these happens:
- The brake is pressed.
- A shift is requested.
- The gear drops below 3rd.
- Speed falls below the OFF speed, or TPS rises above the OFF limit.
- The TCU leaves CUSTOM.

| Lockup thresholds | ECO | NORMAL | SPORT | HOT (ATF ≥ 110 °C) |
|---|---|---|---|---|
| ON / OFF speed (km/h) | 50 / 44 | 58 / 52 | 70 / 64 | 40 / 34 |
| ON / OFF TPS (%) | 45 / 55 | 55 / 65 | 35 / 45 | 85 / 95 |

### 6.6 Thermal strategy (directive 2, corrected)
- **Cold** (ATF below 35 °C, clears at 40 °C): lockup is inhibited.
- **Hot** (110 °C and above, clears below 105 °C): the HOT column applies, so lockup engages
  earlier and holds under load. Converter slip is the main heat source, so a locked converter
  cools the fluid. The display shows `WARN_ATF_HOT`.
- **Critical** (125 °C and above): `WARN_ATF_CRITICAL`. Reduce load: lower speed, less towing,
  less climbing.
- **Sensor fault:** the normal rules apply and a warning is shown.

### 6.7 Faults (latched, hand control to the ECU) and warnings (display only)

| Fault | Condition |
|---|---|
| `FAULT_SUPPLY` | Battery below 10.5 V or above 16.0 V for 500 ms |
| `FAULT_TPS` | TPS voltage outside 0.15–4.85 V for 200 ms |
| `FAULT_VSS` | Speed drops more than 8 km/h in 100 ms from 15 km/h or above (signal loss), **or** more than 2,500 rpm and 15 % TPS in a gear of 2nd or higher, brake off, at 0 km/h for 3 s |
| `FAULT_RATIO` | Lockup applied for 1.5 s, 30 km/h or more, and rpm differs from `km/h × RPM_PER_KMH[gear]` by more than 25 % for 1.5 s (wrong gear engaged, swapped S1/S2 wiring, bad VSS or tach) |

| Warning | Condition |
|---|---|
| `WARN_ATF_SENSOR` / `WARN_OIL_SENSOR` | Open or short |
| `WARN_ATF_HOT` / `WARN_ATF_CRITICAL` | 110 °C / 125 °C |
| `WARN_OIL_LOW` | Below 5 psi with the engine above 600 rpm for 2 s |
| `WARN_MODE_SWITCH` | ECO and SPORT both active (wiring fault); mode forced to NORMAL |
| `WARN_BUTTON_STUCK` | [+] or [-] held for 10 s |
| `WARN_WDT_RESET` | The last reset was caused by the watchdog |

The console LED shows the state:
- **Solid:** TCU in control.
- **Slow blink:** armed and waiting to stop before engaging.
- **Fast blink:** fault (return the master switch to STOCK to acknowledge).

## 7. Display (Nextion 3.5" on Serial1)
At boot the firmware sends `baud=115200` at 9600 baud (the factory default). It's held in RAM
only, so there's no EEPROM wear. It then switches to 115200. Commands end in `FF FF FF`.
Only changed values are sent, plus a full refresh every 2 s.

| Object | Type | Content |
|---|---|---|
| `gear` | Number | 1–4; 0 = unknown/not in D |
| `spd` | Number | km/h |
| `rpm` | Number | rpm in steps of 50; −1 = invalid |
| `tps` | Number | % (−1 = fault) |
| `oil` | Number | psi (−1 = fault) |
| `atf` | Number | °C (−1 = fault) |
| `volt` | Number | Volts ×10 (142 = 14.2 V); use an Xfloat with 1 decimal |
| `lu` | Number | 1 = lockup applied |
| `flt`, `wrn` | Number | Fault and warning bitmasks (§6.7, `tcu_logic.h`) |
| `ctrl` | Text | `STOCK` / `AUTO` / `MAN` |
| `mode` | Text | `ECO` / `NORM` / `SPORT` |
| `msg` | Text | Event text, e.g. `DENIED: OVER-REV`, `STOP TO ENGAGE`, `FAULT - FACTORY ECU` |

## 8. Calibration (do this in STOCK mode first)
The CSV log on USB (115200 baud) works in STOCK. While the ECU is in control, gear and lockup
come from the ECU sense inputs, so you can calibrate everything while the factory ECU drives.

Log columns: `ms, ctrl, mode, gear, lockup, kmh, rpm, rpm_per_kmh, tps, atf, oil, volts, faults, warnings`

1. **TPS:** with the engine off and the ignition on, log closed throttle and WOT. Set
   `TPS_CLOSED_V` and `TPS_WOT_V`.
2. **VSS:** at a steady 50 and 100 km/h by GPS, scale `VSS_PULSES_PER_KM` so that `kmh`
   matches.
3. **RPM_PER_KMH:**
   - Cruise at a steady speed in 3rd and in 4th with `lockup = 1` and average `rpm_per_kmh`.
     Because 3rd is direct (1.000), K3 is the final drive × tyre factor on its own, so it's the
     cleanest number to measure.
   - Check 4th: K4 / K3 should be about 0.734. If it isn't, your ratios differ from the
     published set; find out why before going further.
   - Derive 1st and 2nd: K1 = K3 × 2.810, K2 = K3 × 1.549.
   - Values measured in 1st/2nd with the converter unlocked read slightly high because of slip.
     That's conservative (safe) for the over-rev guard.
   - **The over-rev guard is only as good as these numbers.**
   - When every step is done, set `CALIBRATED = true` in `config.h` and reflash. Until then the
     master switch is ignored and the display shows `NOT CALIBRATED`.
4. **ATF thermistor:** check the reading at ambient and at hot operating temperature against an
   IR thermometer on the cooler line, then adjust `ATF_R25_OHMS` and `ATF_BETA`.
5. **Battery:** compare with a multimeter, then trim `VBAT_DIVIDER`.

## 9. Bring-up and test plan
1. **Host tests:** `make test`. Every logic rule in §6 has a unit test.
2. **Bench:**
   - Mega + drivers + relays, with 15 Ω power resistors and LEDs standing in for the solenoids.
   - Use a function generator or a second Arduino for VSS and tach.
   - Check the truth table, the handover, every fault in §6.7, and that the watchdog drops the
     relays. To test the watchdog, temporarily add `while(1);` behind a test switch. If it
     boot-loops, reflash a bootloader that handles watchdog resets.
3. **In the car, static:**
   - Engine off: verify that every relay drops on power loss.
   - Measure ECU outputs into the dummies.
   - Confirm that no DTCs are set after a drive in STOCK with the TCU connected (OBD-II scan
     tool).
4. **Lift:** all four wheels off the ground. This is mandatory for the 4WD (full-time AWD) car;
   never test on two stands. Run through gears 1–4 in CUSTOM-MANUAL and check `FAULT_RATIO` stays clear with
   lockup applied.
5. **Closed lot, low speed:** handover at a standstill, 1↔2↔3 manual shifts, forced downshifts
   to a stop, brake releasing lockup.
6. **Road:** handover at speed (sync), AUTO in all three modes, kickdown, then deliberately
   attempt over-rev downshifts and check they're refused.

## 10. Open items to VERIFY before wiring

| Item | Default assumed | Where |
|---|---|---|
| Transaxle code (ID plate) | 4WD: A540H or A241H (sources differ) | ID plate on the transaxle |
| S1/S2/SL truth table | §5 (consistent with Toyota service text) | RM, ECT section |
| Solenoid drive polarity | ECM high-side (+12 V), case-grounded (confirmed in service text) | Measure |
| Solenoid connector pins | A3/A1/A2 (2WD), B3/B6/B5 (4WD) | EWD; your harness |
| Solenoid resistance | 11–15 Ω | RM; measure |
| SL on/off vs duty-cycle | on/off | RM; scope the ECU output |
| Gear ratios and final drive | 2.810/1.549/1.000/0.734, FD ~4.40 | RM; then calibrate (§8) |
| VSS pulses/km | 2548 | Calibrate (§8) |
| Tach source | ECU/DLC TAC, 2 pulses/rev | EWD |
| PNP "D" signal | +12 V in D | EWD |
| Redline / fuel cut | Guard at 5,800, forced upshift at 6,200 | RM |
