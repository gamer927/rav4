# Engineering Specification rev 3: RAV4 Transmission Controller

*All speeds in this document and in the firmware are in mph.*

**Vehicle:** 1999 Toyota RAV4 (first generation, SXA10 2WD / SXA11 4WD), 4-speed ECT automatic,
3S-FE 2.0 L engine
**Controller:** Arduino Mega 2560 (`firmware/tcu/`)
**Supersedes:** rev 0 (audit: [`01-audit.md`](01-audit.md)) and rev 1. Rev 2 is
transmission-only: the oil-pressure sender and the Nextion dashboard are gone, and the console is
an ON/OFF switch, an ECO switch, a SPORT switch, [+]/[−] shift buttons and a TEST button, with a
small OLED screen. Rev 3 makes it buildable by hand: through-hole MOSFET drivers with
current-sensor modules instead of surface-mount PROFETs (§3.3), a corrected speed-signal
comparator (wiring guide §6), the TCU split into a power board, a logic board and a hub on the
Mega, and 3D-printed housings.

**Console controls**

| Control | Type | What it does |
|---|---|---|
| ON/OFF | Toggle switch | OFF = factory transmission control (STOCK). ON = this controller drives the shifts (CUSTOM) |
| ECO | Toggle switch | Early upshifts, early lockup |
| SPORT | Toggle switch | Holds gears longer, quick kickdown. ECO and SPORT both off = NORMAL; both on = NORMAL plus an `ECO+SPT` warning |
| [+] / [−] | Push buttons | Manual shifts (tiptronic-style). Hold [+] 1 s to go back to automatic |
| TEST | Push button | Tap: next screen. Hold 2 s: run the self-test (§7) |

> **Safety.** This system takes control of the drivetrain. Everything marked **VERIFY** must
> be confirmed against the factory repair manual (RM) and electrical wiring diagram (EWD) for
> your VIN. Calibrate everything marked **CALIBRATE** (§8) before you first select CUSTOM.
> Do all early testing off public roads (§9).

## 0. The 1999 RAV4: what's confirmed and what isn't

These are the facts the design depends on, with how sure each one is.

| Item | 1999 RAV4 value | Status |
|---|---|---|
| Engine | 3S-FE 2.0 L, 127 hp @ 5,400 rpm, 132 lb-ft @ 4,600 rpm (US rating) | Published US specs. Other markets list 96–99 kW @ 5,800–6,000 and ~180 N·m @ 4,400 |
| Bore × stroke | 86.0 × 86.0 mm (square) | Several sources agree |
| Compression ratio | 9.5:1 or 9.8:1 | Sources disagree |
| Redline / fuel cut | ~6,300 rpm redline, fuel cut ~6,500 rpm | **Not found in any published source.** The firmware's limits (downshift guard 5,800, forced upshift 6,200) already sit below it. Check your tachometer's red zone |
| Torque curve | Peak 4,600, broad from ~2,500, falling off past ~5,500 | No factory curve published; shape is an estimate |
| Tyres | 215/70R16 (~732 rev/mile) | Published specs and VIN decodes |
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
| **STOCK** | Factory ECU (relays de-energised) | Power-up, ON/OFF switch OFF, lever not in D, any fault, MCU reset |
| **CUSTOM-AUTO** | TCU, using the ECO/NORMAL/SPORT shift and lockup maps | ON/OFF switch flipped OFF→ON with the lever in D; or hold [+] for 1 s while in MANUAL |
| **CUSTOM-MANUAL** | TCU, holding the gear you selected | Tap [+] or [-] while in CUSTOM |

Rules that never change:
1. **De-energised = factory.** Power loss, reset, watchdog, firmware hang or a broken ON/OFF
   switch wire all leave the factory ECU in control.
2. **CUSTOM must be armed by a fresh OFF→ON flip** after every boot. Leaving the switch ON
   doesn't arm it at key-on, and a reset mid-drive doesn't re-engage.
3. **The lever must be in D.** Outside D, the ECU handles P/R/N/2/L. Returning to D re-enters
   CUSTOM, synced to the ECU's current gear.
4. **Bumpless handover.** CUSTOM starts in the gear and lockup state the ECU is commanding at
   that moment, sensed from the ECU outputs. If it can't sense them, it only engages at a
   standstill, in 1st.
5. **Faults latch.** A critical fault hands control back to the ECU until the ON/OFF switch is
   turned OFF, which acknowledges the fault.

## 2. Architecture

```
 OVERHEAD CONSOLE                        8-core shielded (shield grounded at TCU end only)
 [ON/OFF] [ECO] [SPORT] [+] [-] [TEST]  LED ────────────────────────────────┐
                                                                            v
 ┌──────────────────────────── UNDER-DASH TCU ENCLOSURE ───────────────────────────────┐
 │  Fused IG feed → reverse-polarity + TVS → 5 V/3 A automotive buck → Mega 5 V, OLED    │
 │                                                                                       │
 │  Arduino Mega 2560 ── input conditioning (RC + clamps, optos for 12 V signals)        │
 │        │  D8/D9/D10 → 3 × P-MOSFET high-side + ACS712 ──────┐                         │
 │        │  D6 → MOSFET/ULN → 3 × DPDT relay coils             │                         │
 │        │  I²C → console OLED (dash)                          v                         │
 │        │                           ┌── DPDT ×3 (S1, S2, SL) ─────────────┐             │
 │        │   ECU S1/S2/SL outputs ───┤ NC: ECU → solenoid    (de-energised) │             │
 │        │   (sensed via optos) ─────┤ NO: ECU → 15 Ω/50 W dummy            │             │
 │        │                           │ NO: TCU driver → solenoid (energised)│             │
 │        │                           └──────────────┬───────────────────────┘             │
 └────────┼──────────────────────────────────────────┼─────────────────────────────────────┘
          │                                          v
   Sensors: TPS tap, VSS, tach (TAC),        Transaxle solenoids S1, S2, SL
   brake (STP), PNP "D", ATF temp, battery    (single-wire, case-grounded)
```

## 3. Hardware

### 3.1 Controller
**Arduino Mega 2560** (or a quality clone with an ATmega16U2 USB bridge).
- 5 V-tolerant I/O, so the 0.5–4.5 V sensors connect directly through protection.
- 16 ADC channels, USB serial for the calibration log, 6 external interrupts (VSS + tach).
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

- Each channel is a **P-channel MOSFET high-side switch** (IRF4905, TO-220, through-hole, so it
  can be hand-soldered on perfboard). D8/D9/D10 drive spare channels of the relay driver
  (ULN2803), which pulls the gate down through 4.7 kΩ; a 10 kΩ gate–source pull-up keeps it off,
  and a 15 V zener protects the gate during a load dump.
- A **UF4007 + 24 V zener clamp** from the drain to ground catches the solenoid's inductive kick
  and lets it release quickly, like the ECM's own driver (a plain freewheel diode would slow the
  release).
- An **ACS712-05B hall current-sensor module** in each MOSFET's +12 V feed reports the solenoid
  current to A4/A5/A6 (185 mV/A, around ~2.5 V at zero). The firmware proves each solenoid
  draws its rated current, both in the self-test and continuously while driving (§7). Each
  sensor's zero is re-learned while the relays are released and that driver is off, when no
  current can flow. `IS_AMPS_PER_VOLT` and `IS_ZERO_V` are in `config.h`; calibrate on the bench
  with a 15 Ω load.
- A 10 kΩ pull-down on every drive pin keeps the drivers off during reset.
- No short-circuit protection is built into a MOSFET (a PROFET has it). The 5 A fuse on X1-1
  protects the wiring, and the firmware hands control back to the ECU if a driven solenoid draws
  more than `SOL_OVER_A` (2.5 A) for 0.3 s.
- *Revision 3 change:* earlier revisions specified PROFET smart switches (BTS6143D/BTS50085).
  The only through-hole one is end-of-life with a long lead time, and the surface-mount ones are
  hard to hand-solder. A PROFET still works: set `IS_ZERO_V = 0` and
  `IS_AMPS_PER_VOLT = kILIS / R_IS`.

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
| Console controls (UP, DOWN, ECO, SPORT, ON/OFF, TEST) | Switch to ground | 2.2 kΩ pull-up to 5 V, 1 kΩ series, 100 nF to ground at the MCU, BAT54S clamp |
| Brake | Stop-lamp switch STP (+12 V when pressed) | PC817 opto (LED + 2.2 kΩ), output pulls the MCU pin low |
| Lever in D | PNP switch "D" output (feeds the D indicator) | PC817 opto as above |
| ECU S1/S2/SL sense | ECU side of each relay (COM-A) | PC817 opto with 3.3 kΩ (about 4 mA load) |
| VSS | SPD signal to the ECU (meter output, about 4 pulses/rev) | Comparator with hysteresis (LM393) or a 6N137 opto. High-impedance tap; never load the line |
| Tach | ECU/DLC TAC signal | 10 kΩ series resistor + 5.1 V zener, or an opto. **Never the coil primary** (hundreds of volts) |
| TPS | ECU VTA and its sensor ground E2 (both tapped) | VTA → 10 kΩ series → A0, and E2 → 10 kΩ series → A1, each with 100 nF and a BAT54S clamp. The firmware reads A0 − A1, so the TCU never ties its ground to E2 (no ground loop that could shift the ECM's throttle reading) |
| ATF temperature | DS18B20 probe in the cooler line (§3.8) | 4.7 kΩ pull-up DQ→5 V at the TCU, 100 Ω series, 5.1 V TVS to ground. (Alternative: analog NTC sender on A2 with a 1 kΩ pull-up; set `ATF_SENSOR = ATF_NTC`) |
| Battery | IG feed | 47 kΩ / 10 kΩ divider, 100 nF, 5.1 V zener |

### 3.6 Console cable (8-core shielded)
Step-by-step wiring, connector pin-outs and the full wire list are in
[`03-wiring-guide.md`](03-wiring-guide.md), with diagrams
[`wiring-diagram.svg`](wiring-diagram.svg) and [`wiring-detail-a.svg`](wiring-detail-a.svg).

1. [+] button
2. [−] button
3. ECO switch
4. SPORT switch
5. ON/OFF switch
6. TEST button
7. Switch common (ground)
8. LED anode (D7 → 330 Ω; the cathode goes to the common ground)

Ground the shield drain at the TCU end only. Every control switches to ground, so a broken wire
reads as "off": ON/OFF falls back to STOCK, and ECO/SPORT fall back to NORMAL.

### 3.7 Console display (OLED)
- A 0.96" or 1.3" 128×64 I²C OLED, mounted in the dash or centre console within about 1 m of
  the TCU box. 0.96" modules are usually SSD1306; many 1.3" modules are SH1106. Set
  `OLED_SH1106` to match.
- Use a 4-wire shielded cable (5 V, GND, SDA, SCL). The firmware runs I²C at 100 kHz for the
  long run and uses a 3 ms bus timeout, so a damaged cable can't hang the controller. If the
  module lacks pull-ups, add 4.7 kΩ on SDA and SCL at the TCU end.
- Don't run I²C up the A-pillar: it isn't built for long cables. That's why the TEST button goes
  in the console cable but the screen sits in the dash.

### 3.8 ATF temperature probe (DS18B20 in the cooler line)
Yes, this works, with these corrections to the usual "Option B" write-up:

- **Use pin D33, not D2.** D2 is the speed-sensor interrupt.
- **Buy the high-temperature version.** The DS18B20 chip reads up to **125 °C**, and the
  firmware's warnings sit at 110 °C (hot) and 125 °C (critical). Many cheap "waterproof" probes
  use PVC cable rated for only 80–105 °C, which goes soft next to a hot cooler line. Get a
  **threaded stainless probe** (1/8" NPT or M10 thread) with **PTFE or silicone cable rated
  125 °C or more**. Above 125 °C the reading isn't guaranteed, but by then the critical warning
  has been showing for a while.
- **Fitting:** a brass or aluminium barbed tee with a threaded side port, sized to the hose
  (measure the inside diameter; 5/16" and 3/8" are common). Seal the probe thread with
  ATF-resistant sealant, and use fuel-injection-style clamps on both barbs. If you replace any
  hose, use transmission-cooler hose (SAE J1532), not heater hose.
- **Which line:** the line *from* the transaxle *to* the radiator carries the hottest fluid
  and gives the most useful reading. Confirm which one it is in the RM, or warm the engine and
  feel or IR-check which hose heats first. Keep the probe away from the exhaust. Step-by-step
  placement: wiring guide §9.
- **Fluid:** cutting the hose loses some ATF. Top up with the fluid listed in your owner's
  manual, and check the level hot, on the dipstick, as the manual describes.
- **Wiring:** this is a 2–3 m run from the engine bay to the TCU behind the glovebox. Use
  3-core shielded cable through a firewall grommet, with the shield grounded at the TCU only.
  Fit the 4.7 kΩ pull-up at the TCU end; if readings drop out on the long run, try 2.2 kΩ.
- **"No calibration needed"** is essentially true: ±0.5 °C up to 85 °C, ±2 °C above. Note it
  reads cooler-line temperature, which lags the pan slightly.

How the firmware reads it:
- One reading per second (start a conversion, read it 800 ms later). This blocks the loop for
  about 7 ms once a second, and interrupts are paused for no more than ~70 µs at a time.
- Every reading is CRC-checked, and an unplugged or shorted probe is detected.
- 85.0 °C is also the chip's power-on value, so it's only accepted when it agrees with the
  previous reading. 85 °C is a normal ATF temperature, so it can't simply be rejected.
- Three bad readings in a row raise `WARN_ATF_SENSOR`, and lockup falls back to the normal
  rules.

### 3.9 Gyro for corner detection (MPU-6050)
- A GY-521 (MPU-6050) module on the same I²C bus as the OLED, at address 0x68. Mount it firmly
  inside or next to the TCU box with its chip **flat**, so the Z axis is vertical; which way it
  points doesn't matter. The 3D-printed TCU enclosure has standoffs for it
  ([`hardware/`](../hardware/README.md)); placement and testing: wiring guide §9.
- Only the yaw rate is used. Lateral g = yaw rate × road speed, which ignores road camber and a
  tilted mounting (an accelerometer would read both as cornering).
- The gyro's zero offset drifts, so it is re-learned every time the car is stopped. After
  power-up, corner hold only activates after about 3 s stationary. Until then, or if the gyro
  stops answering, it is off and the car shifts normally.

## 4. Pin matrix (Arduino Mega 2560)

| Pin | Dir | Signal | Active | Notes |
|---|---|---|---|---|
| D2 (INT4) | in | VSS pulses | falling edge | Comparator or opto output |
| D3 (INT5) | in | Tach pulses | falling edge | 2 pulses/rev (4-cyl) |
| D6 | out | Relay bank driver | HIGH = TCU in control | 10 k pull-down; MOSFET/ULN, never the coil directly |
| D7 | out | Console LED | HIGH | 330 Ω series |
| D8 | out | S1 high-side IN | HIGH = on | 10 k pull-down |
| D9 | out | S2 high-side IN | HIGH = on | 10 k pull-down |
| D10 | out | SL high-side IN | HIGH = on | 10 k pull-down |
| D13 | out | Status LED (on-board) | HIGH | Mirrors D7 |
| D22 | in | Upshift [+] (button or lever microswitch) | LOW | Edge-triggered; `SWAP_SHIFT_INPUTS` swaps D22/D23 |
| D23 | in | Downshift [-] (button or lever microswitch) | LOW | Edge-triggered |
| D24 | in | Mode ECO | LOW | |
| D25 | in | Mode SPORT | LOW | |
| D26 | in | ON/OFF switch | LOW = ON (CUSTOM) | Open = STOCK (fail-safe) |
| D27 | in | Brake | LOW = pressed | Opto |
| D28 | in | Lever in D | LOW = in D | Opto; open = "not D" (fail-safe) |
| D29 | in | ECU S1 sense | LOW = ECU S1 on | Opto |
| D30 | in | ECU S2 sense | LOW = ECU S2 on | Opto |
| D31 | in | ECU SL sense | LOW = ECU SL on | Opto |
| D32 | in | TEST button | LOW = pressed | Console core 8 |
| D33 | 1-Wire | ATF probe (DS18B20) DQ | – | 4.7 k pull-up to 5 V |
| D20 / D21 | I²C | OLED (0x3C) and gyro (0x68) SDA / SCL | – | 100 kHz |
| A0 | an | TPS (VTA) | – | 10 k series tap |
| A1 | an | TPS ground reference (E2) | – | 10 k series tap; subtracted from A0 |
| A2 | an | ATF thermistor (only if `ATF_SENSOR = ATF_NTC`) | – | 1 k pull-up |
| A3 | an | Battery | – | ×5.7 divider |
| A4 / A5 / A6 | an | S1 / S2 / SL current sense | – | Driver IS pins |
| USB | – | CSV log, 115200 baud | – | Calibration (§8) |

VSS and tach use the interrupt pins D2/D3. The buttons don't need interrupts; they're polled and
debounced.

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
- The OLED is redrawn every 100 ms, sent in small chunks so it never blocks the loop.
- The USB CSV log runs at 10 Hz.
- The 250 ms watchdog is kicked on every pass.

### 6.2 Shift requests (manual, auto and protective all use the same path)
A request is **refused** if any of these is true:
- The target is outside 1–4, or isn't exactly one step from the current gear.
- A previous shift happened less than **700 ms** ago, or one is still pending.
- **Downshift:** `mph × RPM_PER_MPH[target] > 5,800`, or the target is 1st above **28 mph**.
- **Upshift:** speed is below the MANUAL forced-downshift threshold for the target gear plus a
  2.5 mph margin. Without this, the TCU would immediately undo the shift.

If the request is accepted with lockup applied, SL releases first and the gear changes
**200 ms** later. After any gear change, lockup stays inhibited for **1.5 s**.

### 6.3 CUSTOM-AUTO
- **Upshift** when `speed ≥ UPSHIFT_MPH[mode][gear](tps)`. The tables are interpolated at
  0/25/50/75/100 % throttle.
- **Downshift** when `speed < upshift(tps) − hysteresis[mode]` (ECO 7, NORMAL 7.5, SPORT 4.5 mph).
  Opening the throttle raises the downshift line, which gives kickdown; the over-rev guard
  refuses any kickdown that would over-rev.
- At least **1.2 s** in gear between automatic shifts, so the box doesn't hunt between gears at
  part throttle.
- **Kickdown** (throttle ≥ 80 % in ECO/NORMAL, ≥ 60 % in SPORT) skips that 1.2 s wait.
  Downshifts then only wait for the previous shift to finish (700 ms), so 4→3 is immediate and
  3→2 follows 0.7 s later. Example in SPORT, flooring it in 4th at 40 mph: 3rd at once, 2nd at
  0.7 s, then upshift to 3rd at 68 mph.

| Upshift mph at 0 / 25 / 50 / 75 / 100 % TPS | 1→2 | 2→3 | 3→4 |
|---|---|---|---|
| ECO (short-shift) | 11/12/18/29/34 | 19/22/34/49/64 | 30/33/46/67/93 |
| NORMAL | 11/15/24/33/36 | 22/30/43/58/66 | 34/41/58/78/97 |
| SPORT (hold) | 14/21/31/36/37 | 26/37/52/63/68 | 41/52/71/93/104 |

With the 1999 RAV4 default ratios, the WOT points sit at about 5,000–5,700 rpm, measured as
"locked converter" rpm (speed × ratio). At full throttle the converter slips 5–10 %, and rpm keeps
climbing during the ~0.5 s shift. So the engine really peaks at roughly **5,400–5,800 (ECO and
NORMAL)** and **6,000–6,200 (SPORT)**, just under a 6,300 redline. These are starting maps:
re-check them against your calibrated `RPM_PER_MPH` and logged rpm.

**Why SPORT shifts near the redline, not at 5,200–5,500.** Shifting "back into the torque band"
sounds right but is slower with these gear steps. After an upshift, the engine drops a long way:

| Shift | Ratio step | rpm drop from 6,000 | Power before → after (approx.) |
|---|---|---|---|
| 1→2 | 1.81 | 6,000 → 3,300 | ~123 hp → ~75 hp |
| 2→3 | 1.55 | 6,000 → 3,900 | ~123 hp → ~95 hp |
| 3→4 | 1.36 | 6,000 → 4,400 | ~123 hp → ~110 hp |

At a given road speed, acceleration follows engine *power*. Every lower gear still makes more
power than the next gear would, all the way to the redline, even though torque is falling past
4,600. The power figures use the estimated curve, so they're approximate, but the gap is too big
to flip. So for maximum acceleration SPORT holds each gear to near the redline. ECO and NORMAL
shift earlier on purpose, for quietness and economy, not speed.

### 6.4 CUSTOM-MANUAL
- **MANUAL is SPORT that you shift yourself.** It uses SPORT's lockup rules, coast unlock and
  tip-in release whatever the ECO/SPORT switches are set to. Going back to AUTO restores the
  switch mode. Brake downshifts and corner hold are automatic-shift features, so they don't apply:
  in MANUAL the gear is yours.
- A tap on [+] or [-] gives one shift. Holding a button never repeats. Any tap while in AUTO
  switches to MANUAL.
- Hold [+] for **1 s** to return to AUTO.
- **Forced downshift** when slowing below 22 mph (4th), 11 mph (3rd) or 5 mph (2nd), so the
  car never stops in a high gear. To allow 2nd-gear snow starts, set `MANUAL_MIN_MPH[2] = 0`.
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
| ON / OFF speed (mph) | 22 / 19 | 36 / 32 | 43 / 40 | 25 / 21 |
| Locked-rpm floor (ON / OFF) | 1,250 / 1,100 | 1,400 / 1,250 | 1,800 / 1,650 | 1,250 / 1,100 |
| ON / OFF TPS (%) | 45 / 55 | 55 / 65 | 35 / 45 | 85 / 95 |

Lockup needs **both** the speed and the locked-rpm floor (speed × gear ratio), so the floor makes
it per gear. ECO locks 3rd from ~23 mph but 4th only from ~32 mph; it releases at ~20 and
~28 mph. **Anti-lug:** below 1,600 locked rpm, lockup only holds at ≤ 25 % throttle. Above
that, the converter's slip and torque multiplication do the work instead of the engine labouring.

**Highway lockup (all modes, including MANUAL).** In 4th from 2,000 locked rpm (~51 mph), the
throttle limits rise to 85 % on / 95 % off. Holding 90 mph takes most of the engine's power
(roughly half to two-thirds pedal), so without this the converter would unlock exactly when
you're cruising fast or climbing a highway grade. Staying locked at 90 mph (~3,550 rpm) means no
slip: lower rpm, less fuel, and much less ATF heat on long runs. Only a near wide-open throttle
releases it, and in SPORT/MANUAL a quick stab of the pedal still releases it instantly (tip-in
release), so SPORT keeps its response.

Coast release (SPORT, §6.8) also counts as a release condition.

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
| `FAULT_VSS` | Speed drops more than 5 mph in 100 ms from 9 mph or above (signal loss), **or** more than 2,500 rpm and 15 % TPS in a gear of 2nd or higher, brake off, at 0 mph for 3 s |
| `FAULT_RATIO` | Lockup applied for 1.5 s, 19 mph or more, and rpm differs from `mph × RPM_PER_MPH[gear]` by more than 25 % for 1.5 s (wrong gear engaged, swapped S1/S2 wiring, bad VSS or tach) |
| `FAULT_SOLENOID` | For 300 ms, a solenoid commanded on draws less than 0.6 A, or one commanded off draws more than 0.2 A (broken wire, relay didn't switch, shorted driver). Needs the current-sense wiring |

| Warning | Condition |
|---|---|
| `WARN_ATF_SENSOR` | ATF thermistor open or short |
| `WARN_ATF_HOT` / `WARN_ATF_CRITICAL` | 110 °C / 125 °C |
| `WARN_MODE_SWITCH` | ECO and SPORT both on; running NORMAL |
| `WARN_BUTTON_STUCK` | [+] or [-] held for 10 s |
| `WARN_WDT_RESET` | The last reset was caused by the watchdog |

The console LED shows the state:
- **Solid:** TCU in control.
- **Slow blink:** armed and waiting to stop before engaging.
- **Fast blink:** fault (turn the ON/OFF switch OFF to acknowledge).

### 6.8 Per-mode driving features

| Feature | ECO | NORMAL | SPORT | What it does |
|---|---|---|---|---|
| Hold gears longer, shift near the top at full throttle | – | – | ✓ | The SPORT shift map (§6.3): full-throttle upshifts at about 6,000–6,200 real engine rpm, just under the redline, which is the fastest way through these gears |
| **Corner hold** | ✓ | ✓ | ✓ | No automatic up- or downshifts (kickdown included) while lateral g ≥ 0.25. Shifting resumes 0.7 s after it drops below 0.12 g, and after 10 s at most. Your own [+]/[−] and the over-rev upshift always work |
| **Brake downshift** | – | – | ✓ | Braking in a straight line (≥ 2 mph/s decel): step down one gear at a time while the lower gear lands at ≤ 4,200 rpm, then hold that gear until you open the throttle (≥ 20 %) or 3 s after releasing the brake. Never mid-corner |
| **Early lockup** | ✓ | – | – | ECO locks the converter as soon as the engine can take it (1,250 rpm locked: ~23 mph in 3rd, ~32 in 4th) and keeps it through light throttle. A slipping converter is the biggest fuel waste in an automatic, so this is ECO's main economy gain |
| **Highway lockup** | ✓ | ✓ | ✓ | In 4th above ~51 mph, lockup holds up to 95 % throttle, so a long fast run or a highway grade doesn't slip and heat the fluid (§6.5) |
| **Coast lockup** | ✓ | ✓ | – | Lockup stays on when you lift, down to the rpm floor, keeping the engine turning with the wheels so the ECU's deceleration fuel cut lasts longer |
| **Anti-lug** | ✓ | ✓ | ✓ | Below 1,600 locked rpm, more than 25 % throttle releases lockup, so early lockup never makes the engine labour |
| **Coast unlock** | – | – | ✓ | Lockup released on a closed throttle (< 2 %, re-allowed at 5 %), so the converter is already slipping when you get back on the throttle |
| **Throttle smoothing** | 0.8 s | 0.25 s | off | The shift map sees a smoothed throttle, so a quick stab of the pedal doesn't cause a downshift; only a sustained push does. That's what makes ECO feel calm. Flooring it (≥ 80 %) bypasses the smoothing, so kickdown is never delayed |
| **Kickdown threshold** | 80 % | 80 % | 60 % | Throttle at or above this is a kickdown: downshifts skip the 1.2 s anti-hunting wait and the throttle smoothing |
| **Lift-off hold** | – | – | ✓ | Snap off the throttle after a hard push (≥ 40 % → ≤ 10 % within 0.5 s) and it holds the gear instead of upshifting, for up to 4 s or until you're back on the throttle (≥ 20 %). Keeps SPORT in gear between corners. A gentle ease-off still upshifts normally. The screen shows `HOLD` |
| **Tip-in release** | – | – | ✓ | A fast stab (≥ 150 %/s) releases lockup at once, even below SPORT's lockup throttle limit, so the converter's torque multiplication is there immediately. Not when the ATF is hot |
| Lockup released on the brake | ✓ | ✓ | ✓ | Always (stall protection) |

MANUAL behaves as SPORT for every row except the two automatic-shift features (corner hold,
brake downshift): there, you choose the gear.

Each one is a per-mode setting in `config.h` (`CORNER_HOLD`, `BRAKE_DOWNSHIFT`,
`LU_COAST_RELEASE`, `SHIFT_TPS_SMOOTH_MS`, `LU_TIPIN_RELEASE`, `KICKDOWN_TPS`, `LIFT_HOLD`), so you can turn them on or off
for any mode.

How each mode feels, overall:
- **ECO (calm):** early upshifts, early lockup that stays on when you lift, and a 0.8 s
  smoothed throttle for shift decisions. It ignores pedal stabs and settles into the
  highest sensible gear.
- **NORMAL:** in between, with light smoothing.
- **SPORT (sharp):** holds gears, kicks down on any real push, releases lockup on a fast stab
  and on a closed throttle, and pre-selects the exit gear under braking.

On engine braking with this transaxle: in D, **1st and 2nd drive through one-way clutches and
freewheel on a closed throttle**. Only a 4→3 downshift adds engine braking. Brake downshifts
into 2nd or 1st just pre-select the exit gear without pulling the car down, so they can't jerk
the car. For real engine braking in 2nd/1st, use the lever's 2 or L positions (the factory ECU
handles those).

**Not possible on this car:**
- **Throttle blip / rev-matching on downshifts.** The 1999 RAV4 has a cable throttle; there's
  nothing for the controller to open. It isn't needed in an automatic anyway: the converter is
  unlocked before every shift, and its fluid coupling absorbs the speed difference. That
  cushioning is what the blip does in a manual. Hijacking the idle-air valve for this would be
  unsafe and wouldn't give enough air.
- **Engine throttle mapping.** Also needs drive-by-wire. The controller only reads the throttle
  sensor and never changes what the pedal does, so the pedal stays exactly as linear as the
  factory made it. The modes change response through the transmission instead (gear choice,
  kickdown eagerness, converter slip), which on an automatic is most of what you feel.
- **Don't modify the throttle linkage** (progressive cams, pedal spacers) to soften the pedal:
  on this transaxle the throttle (TV) cable to the transmission sets line pressure from throttle
  opening. Changing the linkage geometry changes line pressure and shift firmness, and can
  cause slipping.

## 7. Built-in test and console screen

The system tests itself in three ways, modelled on aircraft built-in test (BIT):

| Level | When | What it checks | Moves anything? |
|---|---|---|---|
| **PBIT** (power-up) | Automatically, 1.5 s after every boot | Battery, TPS and ATF probe reading, gyro answering, no shift button stuck, calibration done, last reset wasn't a crash | No |
| **CBIT** (continuous) | Always, while running | Every fault and warning in §6.7, including solenoid current | Hands back to the ECU on a fault |
| **IBIT** (press-to-test) | Hold TEST for 2 s | Everything below | Yes, so it's interlocked |

**IBIT interlock** (the equivalent of an aircraft's weight-on-wheels check). The test refuses
to start, and aborts immediately if any condition changes mid-test, unless all of these hold:
- The ON/OFF switch is OFF.
- The lever is not in D.
- Speed is 0.
- The engine is off: tach below 100 rpm **and** battery below 13.4 V (the alternator isn't
  charging). Two independent checks, so a missing tach can't let it run on a running engine.
  A battery charger connected also reads as "engine running".
- The battery is at least 11.0 V.

On an abort, every output switches off at once.

**IBIT sequence** (about 20 s, most of it the switch prompts):

| # | Step | Pass condition |
|---|---|---|
| 1 | Lamp test: console LED, status LED and the OLED (screen inverts) for 1.5 s | You look. Recorded as CHK |
| 2 | Re-run the PBIT checks | As PBIT |
| 3 | Relays on; pulse S1, then S2, then SL for 0.4 s each, one at a time | 0.6–1.6 A on the pulsed solenoid. Engine off means no line pressure, so the valves only click |
| 4 | Relays off; all drivers on for 0.4 s | ≤ 0.2 A. Current here means a welded or stuck relay |
| 5 | Prompts: PRESS [+], PRESS [−], ECO ON SPORT OFF, SPORT ON ECO OFF, PRESS BRAKE | Detected within 8 s. TEST skips a prompt (SKIP); a timeout is a FAIL. Covers the whole A-pillar cable |
| 6 | Watchdog: the board deliberately stops servicing the watchdog | It must reboot within 0.25 s; the results survive the reset and the test records PASS. If the screen freezes on `WATCHDOG TEST`, it failed |

Results are PASS, FAIL, CHK (needs a human: lamps, or solenoids without current sense) or SKIP.
The full table also prints on the USB log.

**TEST button:**
- Short press: next screen (MAIN → SENSORS → TEST).
- Hold 2 s: run IBIT.
- During IBIT, a press skips the current prompt.

**Screens** (rendered from the real firmware in a simulator: [`oled-screens.png`](oled-screens.png)):
- **MAIN:** gear and mode in large text; control state (STOCK/AUTO/MAN) and lockup; BIT
  status; active faults and warnings; the event line; speed and ATF temperature.
- **SENSORS:** speed, rpm, throttle, ATF temperature, battery and solenoid currents.
- **TEST:** test progress and prompts, then the summary with FAIL items first, then CHK items.


## 8. Calibration (do this in STOCK mode first)
The CSV log on USB (115200 baud) works in STOCK. While the ECU is in control, gear and lockup
come from the ECU sense inputs, so you can calibrate everything while the factory ECU drives.

Log columns: `ms, ctrl, mode, gear, lockup, mph, rpm, rpm_per_mph, tps, atf, volts, faults, warnings`

1. **TPS:** with the engine off and the ignition on, log closed throttle and WOT. Set
   `TPS_CLOSED_V` and `TPS_WOT_V`.
2. **VSS:** at a steady 30 and 60 mph by GPS, scale `VSS_PULSES_PER_MILE` so that `mph`
   matches.
3. **RPM_PER_MPH:**
   - Cruise at a steady speed in 3rd and in 4th with `lockup = 1` and average `rpm_per_mph`.
     Because 3rd is direct (1.000), K3 is the final drive × tyre factor on its own, so it's the
     cleanest number to measure.
   - Check 4th: K4 / K3 should be about 0.734. If it isn't, your ratios differ from the
     published set; find out why before going further.
   - Derive 1st and 2nd: K1 = K3 × 2.810, K2 = K3 × 1.549.
   - Values measured in 1st/2nd with the converter unlocked read slightly high because of slip.
     That's conservative (safe) for the over-rev guard.
   - **The over-rev guard is only as good as these numbers.**
   - When every step is done, set `CALIBRATED = true` in `config.h` and reflash. Until then the
     ON/OFF switch is ignored and the display shows `NOT CALIBRATED`.
4. **ATF thermistor:** check the reading at ambient and at hot operating temperature against an
   IR thermometer on the cooler line, then adjust `ATF_R25_OHMS` and `ATF_BETA`.
5. **Battery:** compare with a multimeter, then trim `VBAT_DIVIDER`.
6. **Factory shift map (for tuning NORMAL):** with the switch OFF, the log also prints every
   shift and lockup change the factory ECU makes, for example
   `# ECU shift 2>3 at 28 mph, TPS 30 %, 2400 rpm`. Drive a mix of gentle, normal and firm
   acceleration and collect these lines. The NORMAL map is an estimate of a stock-like schedule;
   adjust its `UPSHIFT_MPH` row so NORMAL lands near the factory points, so NORMAL feels like
   stock plus the extras.

## 9. Bring-up and test plan
1. **Host tests:** `make test`. Every logic rule in §6, and the self-test sequence and interlocks
   in §7, has a unit test.
2. **Bench:**
   - Mega + drivers + relays, with 15 Ω power resistors and LEDs standing in for the solenoids.
   - Use a function generator or a second Arduino for VSS and tach.
   - Calibrate `IS_AMPS_PER_VOLT` with a 15 Ω load, then run IBIT (hold TEST). Every line
     should PASS, including WATCHDOG. If the board boot-loops after the watchdog step, reflash a
     bootloader that handles watchdog resets.
   - Check the truth table, the handover and every fault in §6.7.
3. **In the car, static:**
   - Ignition on, engine off, lever in P: run IBIT. It checks the real solenoids, relays and the
     whole console cable.
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
| VSS pulses/mile | ~4101 (2548/km) | Calibrate (§8) |
| Tach source | ECU/DLC TAC, 2 pulses/rev | EWD |
| PNP "D" signal | +12 V in D | EWD |
| Redline / fuel cut | Guard at 5,800, forced upshift at 6,200 | RM |
| ATF cooler "out" line and hose size | Line from transaxle to radiator | RM; measure the hose ID |
