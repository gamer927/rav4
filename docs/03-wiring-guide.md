# Wiring guide: 1999 Toyota RAV4 transmission controller

Follow this guide alongside the two diagrams:

- [`wiring-diagram.svg`](wiring-diagram.svg) is the whole system. Every wire has a **W-number**
  that matches the wire list in §4.
- [`wiring-detail-a.svg`](wiring-detail-a.svg) is **Detail A**: one solenoid channel (relay,
  driver, dummy load, sense), built three times.

![System wiring diagram](wiring-diagram.png)

> **Read this first**
> - **Confirm every car wire with a meter before you cut or tap it.** §5 gives a test for each.
>   The Toyota terminal *names* (S1, SPD, VTA, …) are confirmed for this car, but ECM pin
>   numbers and wire colours vary by year, 2WD/4WD and market, and published pinouts disagree.
>   Use the factory repair manual (RM) and electrical wiring diagram (EWD) for your VIN.
> - **Disconnect the battery** before working on the ECM or solenoid wiring.
> - **Only S1, S2 and SL are cut.** Every other car connection is a *tap*: the factory wire
>   stays intact and the TCU listens to it.
> - **The ECM is static-sensitive.** Ground yourself, back-probe connectors, and never push
>   meter probes into the front of ECM pins.

---

## 1. What connects to what

| Connector (at the TCU) | Goes to | Wires |
|---|---|---|
| **X1 power** (3-pin) | Fuse box (ignition-switched 12 V), body ground | W1–W3 |
| **X2 solenoids** (6-pin) | The S1, S2, SL wires, cut between the ECM and the transaxle | W4–W9 |
| **X3 signals** (8-pin, 7 used) | Taps: lever "D", brake, ECM ground, speed, tach, throttle, throttle ground | W10–W16 |
| **X4 console** (8-pin) | Overhead console switches, buttons or shifter lever, LED | W20 (C1–C8) |
| **X5 OLED** (4-pin) | 2.42" screen in the console pod | W21 |
| **X6 ATF** (3-pin) | Temperature probe in the cooler line | W17–W19 |
| USB | Laptop, for flashing and the calibration log | – |

Use sealed automotive connectors (Deutsch DT or Superseal) so the box unplugs cleanly.

**Make a bypass plug for X2:** a mating 6-pin plug with three jumpers, 1–4, 2–5 and 3–6. Unplug
the TCU, plug this into the car side of X2, and the ECM drives the solenoids directly. The car is
100 % stock, even with the TCU removed.

## 2. Harness colour code and wire sizes

These colours are for **your** harness, the new wires from the TCU. Toyota's own wire colours
are in the EWD. Label both ends of every wire with its W-number (heat-shrink markers).

| Colour | Use | Size |
|---|---|---|
| Red | +12 V (W1, W2) and +5 V (W17, OLED 5V) | W1 16 AWG, others 18–22 AWG |
| Black | Ground (W3, W18, OLED GND, C7) | W3 16 AWG, others 18–22 AWG |
| Black/white | ECM ground reference E1 (W12) | 18 AWG |
| Yellow | S1/S2/SL, **ECM side** (W4–W6) | 18 AWG |
| Orange | S1/S2/SL, **solenoid side** (W7–W9) | 18 AWG |
| Blue | Signal taps (W10, W11, W13–W16) | 22 AWG; SPD+TAC and VTA+E2 as shielded pairs |
| Brown | ATF probe (W17–W19) | 3-core shielded cable |
| Green | Console (W20) | 8-core shielded cable, 24 AWG |
| Purple | OLED I²C (W21) | 4-core shielded cable, ≤ 1.5 m |

Ground every cable shield **at the TCU end only**.

## 3. Connector pin-outs

**X1 power**

| Pin | Wire | Signal | Inside the TCU |
|---|---|---|---|
| 1 | W1 | +12 V drivers, **5 A fuse** | TVS → current sensors and MOSFET drivers Q1–Q3, relay coils |
| 2 | W2 | +12 V logic, **2 A fuse** | Reverse-polarity diode → TVS → 5 V buck; battery sense (47k/10k → A3) |
| 3 | W3 | Body ground | Star ground point |

**X2 solenoids** (Detail A)

| Pin | Wire | Signal | Inside the TCU |
|---|---|---|---|
| 1 | W4 | S1, ECM side | K1 COM-A; ECU-sense opto → D29 |
| 2 | W5 | S2, ECM side | K2 COM-A; ECU-sense opto → D30 |
| 3 | W6 | SL, ECM side | K3 COM-A; ECU-sense opto → D31 |
| 4 | W7 | S1, solenoid side | K1 NC-A + K1 NO-B |
| 5 | W8 | S2, solenoid side | K2 NC-A + K2 NO-B |
| 6 | W9 | SL, solenoid side | K3 NC-A + K3 NO-B |

**X3 signals**

| Pin | Wire | Signal | Inside the TCU |
|---|---|---|---|
| 1 | W10 | Lever in **D** (+12 V in D) | PC817 opto → D28 |
| 2 | W11 | Brake **STP** (+12 V when pressed) | PC817 opto → D27 |
| 3 | W12 | ECM ground **E1** | TCU signal ground reference |
| 4 | W13 | Speed **SPD** | LM393 comparator → D2 |
| 5 | W14 | Tach **TAC** | PC817 opto → D3 |
| 6 | W15 | Throttle **VTA** | 10k series, 100 nF, clamp → A0 |
| 7 | W16 | Throttle ground **E2** | 10k series, 100 nF, clamp → A1 (measured, *not* tied to TCU ground) |
| 8 | – | spare | |

**X4 console** (W20, 8-core)

| Pin | Core | Signal | Mega pin |
|---|---|---|---|
| 1 | C1 | [+] button / lever upshift microswitch | D22 |
| 2 | C2 | [−] button / lever downshift microswitch | D23 |
| 3 | C3 | ECO switch | D24 |
| 4 | C4 | SPORT switch | D25 |
| 5 | C5 | ON/OFF switch | D26 |
| 6 | C6 | TEST button | D32 |
| 7 | C7 | Common ground: the other side of every switch, and the LED cathode | GND |
| 8 | C8 | Status LED anode (D7 → 330 Ω → C8) | D7 |

Each control switches its core to C7 (ground). Closed means active. A broken wire reads as off:
ON/OFF falls back to STOCK, and ECO/SPORT fall back to NORMAL.

**X5 OLED** (W21): 1 = 5 V, 2 = GND, 3 = SDA (D20), 4 = SCL (D21). At the 2.42" screen: VCC = 5 V,
GND, DIN = SDA, CLK = SCL, DC and CS to GND, RES to a 10k pull-up + 1 µF to GND (build book part 7).
**X6 ATF**: 1 = +5 V (W17), 2 = GND (W18), 3 = DQ (W19 → 100 Ω → D33, 4.7k pull-up to 5 V).
The **MPU-6050 gyro** sits inside the box on the same I²C bus (5 V, GND, D20, D21). Mount it flat (§9).

## 4. Wire list

| W | From (car) | To | Colour / size | Notes |
|---|---|---|---|---|
| W1 | Add-a-fuse output → 5 A inline fuse | X1-1 | Red 16 AWG | Fuse within 20 cm of the tap |
| W2 | Add-a-fuse output → 2 A inline fuse | X1-2 | Red 18 AWG | Same tap as W1, separate fuse |
| W3 | Body ground bolt | X1-3 | Black 16 AWG | Short; bare metal; star washer |
| W4 | ECM terminal **S1** (cut, ECM end) | X2-1 | Yellow 18 AWG | Solder + heat-shrink |
| W5 | ECM terminal **S2** (cut, ECM end) | X2-2 | Yellow 18 AWG | |
| W6 | ECM terminal **SL** (cut, ECM end) | X2-3 | Yellow 18 AWG | |
| W7 | S1 wire (cut, solenoid end) | X2-4 | Orange 18 AWG | |
| W8 | S2 wire (cut, solenoid end) | X2-5 | Orange 18 AWG | |
| W9 | SL wire (cut, solenoid end) | X2-6 | Orange 18 AWG | |
| W10 | Park/neutral switch **D** output | X3-1 | Blue 22 AWG | Tap |
| W11 | Stop-lamp switch **STP** output | X3-2 | Blue 22 AWG | Tap |
| W12 | ECM ground **E1** / engine ground | X3-3 | Black/white 18 AWG | |
| W13 | ECM **SPD** | X3-4 | Shielded pair with W14 | Tap (high impedance) |
| W14 | ECM **TAC** | X3-5 | Shielded pair with W13 | Tap. **Never the ignition coil (−) terminal** |
| W15 | ECM **VTA** | X3-6 | Shielded pair with W16 | Tap |
| W16 | ECM **E2** | X3-7 | Shielded pair with W15 | Tap |
| W17–W19 | DS18B20 probe red / black / yellow | X6-1/2/3 | Brown 3-core shielded | Through a firewall grommet |
| W20 | Console panel | X4 | Green 8-core shielded | Up the A-pillar, away from the airbag |
| W21 | Console-pod OLED | X5 | Purple 4-core shielded | 1.5 m maximum |

## 5. Finding each signal on the 1999 RAV4

Do these tests with a multimeter **before** you cut or tap. Write down the wire colour you find on
your car next to each W-number.

| Signal | Where (VERIFY in your EWD) | How to confirm it's the right wire |
|---|---|---|
| **IG 12 V** (W1, W2) | Interior fuse box: an ignition-only fuse, with an add-a-fuse | 12 V with the key ON and engine running, **0 V with the key OFF** |
| **Body ground** (W3) | A factory ground bolt near the ECM | < 0.5 Ω to the battery negative |
| **ECM location** | See the RM's "location of components" page. Many Toyotas of this era have it behind the glovebox or passenger kick panel, but check | – |
| **S1, S2, SL** (W4–W9) | ECM terminals named S1, S2, SL; the same three wires run to the solenoid connector on the transaxle | Battery disconnected. Unplug the transaxle solenoid connector and check continuity from each ECM wire to it. Expected: S1 → A3 (2WD) / B3 (4WD), S2 → A1 / B6, SL → A2 / B5 (Toyota service text; VERIFY). With the connector plugged back in, each reads **11–15 Ω to body ground** (through the solenoid). |
| **SPD** (W13) | ECM terminal SPD (speed signal from the meter) | Key ON, lever in N, roll the car slowly: the voltage pulses between ~0 V and ~5 V (or 12 V). About **1.1 pulses per second per mph** |
| **TAC** (W14) | ECM terminal TAC (or TAC at the engine-bay diagnostic connector, if fitted) | Engine idling (~750 rpm): about **25 Hz** in frequency mode (2 pulses per crank turn) |
| **VTA / E2** (W15, W16) | ECM terminals VTA and E2 (throttle sensor signal and its ground) | Key ON, engine off, measured VTA-to-E2: **~0.3–0.8 V closed**, rising smoothly to **~3.2–4.9 V** at full throttle. E2 is within a few mV of ground |
| **E1** (W12) | ECM ground terminal E1, or the engine ground strap | < 0.5 Ω to the engine block |
| **STP** (W11) | Stop-lamp switch on the brake pedal (output side), or ECM terminal STP | 0 V pedal up, **12 V pedal down** |
| **Lever D** (W10) | Park/neutral position switch "D" output (it also feeds the "D" light in the cluster) | Key ON: **12 V only with the lever in D**, 0 V in P, R, N, 2, L |

**How to tap a wire:** strip a 10 mm window without cutting the strands, wrap the new wire around
it, solder, and seal with adhesive heat-shrink. Don't use Scotch-lock taps.
**How to cut S1/S2/SL:** cut close to the ECM (inside the car, dry), leaving room to work. Solder
each end into its X2 harness wire and seal it.

## 6. Inside the TCU (Detail B: input and output circuits)

The TCU is three boards joined by two ribbon cables. Exact part positions, every wire, and the
order to build them in are in the [build book](05-build-book.md).

| Board | What's on it |
|---|---|
| **Power board** (90 × 150 mm perfboard) | Everything that touches the car's 12 V wiring: X1/X2/X3 and LOAD terminals, protection, the 5 V buck converter, relays, drivers, current sensors, optocouplers, speed comparator, throttle taps. Ribbon A (26-way) to the hub |
| **Logic board** (90 × 70 mm perfboard) | X4 console, X5 OLED and X6 ATF terminals, console input filters, ATF probe parts, the **gyro**. Ribbon B (16-way) to the hub |
| **Hub** (Mega prototype shield) | Two ribbon sockets, each pin wired to its Mega pin |

Detail A (the solenoid channel) is in [`wiring-detail-a.svg`](wiring-detail-a.svg). The other
circuits are below. A 10 kΩ pull-down sits on **every** output that drives something (D6, D8,
D9, D10), so everything is off while the Mega resets.

**Solenoid driver** (×3: D8/D9/D10 → S1/S2/SL)
```
D8 ── ULN2803 IN4 (10k pull-down) ; ULN2803 OUT4 ── 4.7k ── Q1 gate
+12 V DRV ── CS1 ACS712 (IP+ → IP−) ── Q1 source (IRF4905 P-MOSFET)
Q1 gate ── 10k ── source (pull-up = off) ; 15 V zener gate–source (cathode on source)
Q1 drain ── K1 COM-B ; clamp: drain ── UF4007 (cathode on drain) ── 24 V zener ── GND
CS1 OUT ── A4 (185 mV/A around ~2.5 V)
```
D8 high → the ULN2803 pulls the gate down → the MOSFET switches +12 V to the solenoid. The
zener clamp lets the solenoid release quickly, like the ECM's own driver.

**Console switch input** (×6: D22–D26, D32), on the logic board
```
+5V ── 2.2k ──┬── 1k ──┬── Mega pin
              │        ├── 100 nF ── GND
              │        └── BAT85 to 5V, BAT85 from GND (clamps)
            X4 core ── switch ── C7 (GND)
```

**12 V opto input** (brake D27, lever D28, ECU sense D29–D31, tach D3)
```
car signal ── 2.2k (3.3k for ECU sense) ──┬── PC817 LED (+) ── LED (−) ── GND
                                          └── 1N4148 reverse across the LED
PC817 transistor: collector ── Mega pin (10k pull-up to 5V), emitter ── GND
Mega reads LOW when the car signal is at +12 V.
```

**Speed (SPD) comparator**, high impedance so it never loads the car's line (D2)
```
SPD ── 22k ──┬── LM393 (+)        LM393 (−) ── 0.76 V (10k / 1.8k from 5V)
             ├── 10k ── GND       1M feedback (+) ← output (hysteresis)
             └── 5.1V zener       output ── 10k pull-up to 5V ── D2
```
The 22k/10k divider scales the signal by about 0.31, so a 5 V speed signal reaches 1.56 V and
a 12 V one 3.75 V, both well over the 0.76 V threshold. (Revision 3 fix: the earlier 47k/10k
divider with a 2.5 V threshold never switched.)

**Throttle tap** (A0, A1)
```
VTA ── 10k ──┬── A0      E2 ── 10k ──┬── A1
           100 nF                  100 nF
        BAT85 clamps            BAT85 clamps
(Firmware reads A0 − A1. E2 is never tied to the TCU ground.)
```

**Battery sense** (A3): `X1-2 (+12 V) ── 47k ──┬── A3; 10k to GND, 100 nF, 5.1 V zener`

**ATF probe** (D33): `X6-3 DQ ── 100 Ω ── D33; 4.7k from DQ to 5 V; 5.1 V zener to GND`
(use 2.2k if readings drop out on the long run)

**Relay coils** (D6): `D6 ── ULN2803 IN1-3 (10k pull-down); OUT1-3 ── K1/K2/K3 coil (−); coil (+) ── +12 V DRV; ULN COM ── +12 V DRV`

**Status LED** (D7): `D7 ── 330 Ω ── X4-8 (C8) ── LED ── X4-7 (C7)`

## 7. Build order

This is the short version. The [build book](05-build-book.md) is the full step-by-step version,
in order, with pictures.

1. **Bench:** build the TCU on the bench with 15 Ω power resistors (plus LEDs) in place of the
   three solenoids. Flash the firmware, calibrate `IS_AMPS_PER_VOLT`, hold TEST, and check every
   self-test line passes.
2. **Power (car):** W1–W3 only, with the ON/OFF switch OFF. Key ON: the screen shows the MAIN page
   and the power-up test runs.
3. **Signal taps:** W10–W16. Check the SENSORS page: speed 0, throttle 0 % → 100 % as you press
   the pedal, rpm at idle.
4. **Console and screen:** W20, W21. Hold TEST: the switch prompts check every console wire.
5. **ATF probe:** W17–W19, and the probe in the cooler line (spec §3.8). Top up the ATF.
6. **Solenoid intercept:** cut S1/S2/SL and fit X2 (W4–W9). With the switch OFF, the relays rest
   in factory position, so the car must drive **exactly** as before. Then try the bypass plug: it
   must drive the same. Check the OBD-II scanner for trouble codes.
7. **Calibrate** with the switch OFF (spec §8), set `CALIBRATED = true`, and follow spec §9: lift,
   then a closed lot, then the road.

## 8. Sequential shifter lever (instead of [+] and [−] buttons)

Any sim-racing style sequential lever with **two microswitches** works. This project is set up
for the DIY build in
[Beavis Motorsport's DIY sequential shifter guide](https://www.beavismotorsport.com/sim-racing/diy-sequential-shifter/)
(printed parts: Thingiverse [4557771](https://www.thingiverse.com/thing:4557771)). The
[console pod](../hardware/README.md#console-pod) has its 4 × M6, 116 × 74 mm bolt pattern on its
top deck.

- Build the lever as the guide describes, but **print it in PETG or ASA, not PLA**: PLA goes
  soft in a hot car.
- Use its two **V-155-1C25 roller limit switches**, and leave out the Arduino Pro Micro. That
  board turns the switches into a USB game controller for a PC; the TCU reads the switches
  directly.
- Upshift switch: COM → C7 (ground), NO → C1 ([+]).
- Downshift switch: COM → C7 (ground), NO → C2 ([−]).
- Leave each switch's NC terminal unconnected.
- To change between "pull = upshift" (rally style) and "push = upshift", set
  `SWAP_SHIFT_INPUTS` in `config.h`. You don't need to rewire.
- **In a car:** mount it rigidly where it can't foul the factory gear lever, your knee or the
  handbrake, and give it hard end-stops. The V-155 switches are open-frame. That's fine inside
  the cabin, but keep drinks away, or use sealed (IP67) V-type switches with the same footprint.
- The firmware's 25 ms debounce and edge detection mean one click is always exactly one shift,
  even with a bouncy switch.

## 9. Where everything goes (1999 RAV4 4WD)

| Part | Where | Why / how |
|---|---|---|
| **TCU box** ([3D-printed](../hardware/README.md#tcu-enclosure)) | Behind the glovebox, bolted **flat** to the dash's metal structure or a steel bracket | Close to the ECM and the console. Flat and rigid because the gyro is inside it. Mounting needs four bolts, not Velcro |
| **Gyro (MPU-6050)** | **Inside the TCU box**, on its two standoffs, chip up | See below |
| **Dummy-load heatsink** | Under the dash on metal, in moving air, ≥ 50 mm from plastic, wiring and the TCU box | ~14 W per resistor. Never inside the box |
| **Console pod** (switches, LED, OLED, shifter) | Centre console, bolted down through its bottom plate | Within ~1–1.5 m of the box, for the OLED's I²C cable |
| **ATF probe** | Engine bay, in the hot ATF cooler hose | See below |
| **Fuse tap** | Cabin fuse box, an IG (ignition-switched) fuse | §5 |

### The gyro

- It sits **inside the TCU box**. The box's floor has two standoffs for a GY-521 board and the
  words "GYRO: CHIP UP, FLAT". It shares the I²C bus with the OLED, so it needs no extra cable.
- **Flat matters; where doesn't.** The firmware only uses the yaw rate (how fast the car is
  turning), and every point of a rigid car turns at the same rate. Behind the glovebox reads the
  same as the middle of the car. A tilt only scales the reading by cos(tilt): 5° costs
  0.4 %, 15° costs 3.4 %. Check the box with a phone level app as you bolt it in.
- **Rigid matters.** If the box can rock on its mount, the gyro reads the rocking.
- **Which way it points doesn't matter.** Any rotation flat on the floor works.
- Keep it away from the heatsink and heater ducts. The zero offset drifts with temperature.
  The firmware re-learns it at every stop, so this only reduces noise.
- **Test:** the SENSORS page shows lateral g (LAT). A brisk roundabout or on-ramp reads about
  0.2–0.5 g; driving straight it stays near 0.00. If it jumps around on a straight, bumpy road, the box
  isn't mounted rigidly.

### The ATF temperature probe

The probe goes in a barbed tee spliced into the **hot** ATF cooler hose: the one carrying fluid
*from* the transaxle *to* the cooler in the radiator's tank. Parts: README §6. Background:
spec §3.8.

1. **Find the two cooler lines.** On the RAV4 they are steel pipes from the transaxle to
   the radiator, joined by short rubber hoses near the radiator. Your RM's ATF-cooler section
   shows them.
2. **Find the hot one.** Start the cold engine, idle in P, and IR-check or carefully feel
   both hoses. The one that warms first is the outlet: that's the hot line. Stop the engine.
3. **Pick the spot:** a **straight rubber section at least 50 mm long**, on the hot line near
   the radiator end. That's easy to reach and away from the exhaust.
   - At least 150 mm from the exhaust manifold and its heat shield.
   - Clear of the fan, belts and anything that moves.
   - Before any auxiliary cooler, if one is fitted, so it reads transaxle-out temperature.
4. **Fit it:**
   - Clamp the hose or have a drain pan ready, then cut the hose square.
   - Fit the tee with fuel-injection clamps on both barbs.
   - Point the probe's port **sideways**: pointing down collects debris, and pointing up
     traps air.
   - Seal the probe's thread with ATF-resistant sealant.
5. **Route the cable** (3-core shielded):
   - Run it along the existing engine harness with zip ties every 150–200 mm, well clear of
     the exhaust, the steering shaft and the 4WD transfer/driveshaft area below.
   - Go through a **firewall grommet**: an existing one on the glovebox side if you can (pierce
     it and seal it with RTV), or a new grommet in a drilled hole with the edge painted.
   - Inside, run it to the TCU box's **ATF** gland (X6).
   - Ground the shield at the TCU end only.
6. **Top up and check:** run the engine, shift through every gear, check for leaks, then check
   the ATF level **hot** on the dipstick as the owner's manual describes. Top up with the fluid
   it specifies.
7. **Test:** the SENSORS page should show the ATF temperature climbing with the engine warm,
   within a few degrees of an IR thermometer on the tee.

### 4WD notes (SXA11)

- **Solenoid connector pins:** on the 4WD transaxle, S1 / S2 / SL are terminals
  **B3 / B6 / B5** (2WD: A3 / A1 / A2). This is Toyota service text; **confirm it** with the
  continuity test in §5 before cutting anything.
- **Lift all four wheels** for any in-gear testing. The full-time 4WD drives both axles through
  the centre differential. With only the front wheels up, the rear wheels can drive the car
  off the stands, and the centre diff is stressed.
- **Transfer and centre-diff circuits stay on the factory ECU.** The TCU only intercepts S1,
  S2 and SL.
- **Speed signal:** use the same SPD wire at the ECM as on the 2WD (§5). Tyre size changes it
  equally on both, which calibration (spec §8) corrects.
- **Final drive:** the firmware's starting rpm-per-mph table uses ~4.40, the figure listed for
  the 4WD automatic. Calibrate it anyway (spec §8): sources disagree.
