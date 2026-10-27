# 1999 RAV4 Transmission Controller

A standalone transmission controller for a **1999 Toyota RAV4** (first generation, 4-speed
automatic, 3S-FE engine). An Arduino Mega takes over the transmission's three solenoids and adds
drive modes, manual shifting and an aircraft-style self-test, while the engine and its computer
stay 100 % stock.

**The factory computer is always the backup.** Flip the ON/OFF switch off, lose power, or let
anything go wrong, and relays hand the transmission straight back to the factory computer. The
car then drives exactly as it did from the factory.

![Console screen](docs/oled-screens.png)

## What it does

All speeds (screen, logs, settings, docs) are in **mph**.

| Mode | Feel | What changes |
|---|---|---|
| **ECO** | Calm, economical | Early upshifts; torque-converter lockup as early as the engine can take it (from ~23 mph in 3rd), kept on when you lift for longer fuel cut, released if the engine would labour; pedal stabs ignored (0.8 s throttle smoothing). Flooring it still kicks down instantly. |
| **Highway (all modes)** | Locked and steady | In 4th above ~51 mph the torque converter stays locked up to 95 % throttle, so a long 90 mph run (~3,550 rpm) or a highway grade doesn't let it slip: lower rpm, less fuel, much less transmission heat. Only flooring it releases lockup (in SPORT, a quick stab does too). |
| **NORMAL** | Like stock, plus the extras | Shift points close to a factory-style schedule (tune them to your car's logged factory shifts), light throttle smoothing, plus everything stock doesn't have: corner hold, the over-rev guards and the fault hand-back. |
| **SPORT** | Sharp | Holds each gear to just under the redline at full throttle (the fastest way through these gears), kicks down from 60 % throttle, holds the gear when you snap off the throttle between corners, downshifts while braking so you're in the right gear for the exit, and releases lockup on a closed throttle or a fast pedal stab. |
| **MANUAL** | SPORT, you shift | Tap [+] or [−] from any mode. Same lockup and pedal response as SPORT, but the box never shifts for you, except to protect the engine (upshift at 6,200 rpm, downshift when slowing to a stop). Hold [+] for 1 s to go back to automatic in the switch's mode. |
| **All automatic modes** | | No automatic shifts mid-corner (gyro), lockup off when braking. |

**Protection, always on:**
- **No over-rev:** every downshift is checked against engine rpm (refused above 5,800), 1st is
  never allowed above 28 mph, and the box upshifts automatically at 6,200 rpm.
- **No stalling:** lockup releases when you brake and at low speed.
- **Fluid temperature:** lockup is blocked while the fluid is cold, and engages earlier when it's
  hot (110 °C+). Locking up is what cools the fluid.
- **Faults hand back to the factory computer:** a lost speed signal, a failed throttle sensor,
  bad voltage, the wrong gear engaged, or a broken solenoid wire.
- **Built-in self-test:** hold TEST for 2 s with the car parked and the engine off. It checks
  the lamps, each solenoid's current, the relays, every console switch, and the watchdog. It
  refuses to run unless the car is parked with the engine off.
- **Locked until calibrated:** the box can't take control until you've calibrated it on your car
  (see [First steps](#first-steps)).

**Not touched:** the engine computer, the throttle, and the transmission's throttle cable.

### What about rev matching on downshifts?

True rev matching (a throttle "blip" before each downshift) needs something that can open the
throttle. The 1999 RAV4 has a cable throttle, so the box can't do it without adding a throttle
actuator. That would be a separate, safety-critical system that can accelerate the car on its
own, so this project deliberately doesn't do it.

An automatic gets the same result a different way, and the box already does it:
- **The torque converter is the rev matcher.** Before every downshift the box unlocks the
  converter clutch (200 ms ahead of the shift). The fluid coupling then absorbs the speed
  difference, which is the job a blip does in a manual gearbox.
- **Lockup stays off for 1.5 s after each shift**, so the engine settles to its new speed
  smoothly before the connection goes solid again.
- **Over-rev guard:** downshifts that would land above 5,800 rpm are refused.
- **On this transaxle, 1st and 2nd freewheel when you lift off** (one-way clutches in D). Only
  the 4→3 downshift can pull the car back with engine braking, so a downshift into 2nd or 1st
  can't jolt the car.

## Wiring

![System wiring diagram](docs/wiring-diagram.png)

The full step-by-step guide is in **[`docs/03-wiring-guide.md`](docs/03-wiring-guide.md)**. It
covers every connector pin-out, a numbered wire list (W1–W21) matching the diagram, where each
signal is on the 1999 RAV4 with a multimeter test to confirm it, the circuit for every input,
and the build order. [`docs/wiring-detail-a.svg`](docs/wiring-detail-a.svg) is a close-up of
the solenoid changeover, which is built three times.

Only three factory wires are cut (S1, S2, SL); everything else is a tap. A **bypass plug**
restores stock driving with the box removed.

**Where everything goes in the car** (TCU box, gyro, heatsink, console pod, ATF probe, plus the
4WD-specific notes) is [guide §9](docs/03-wiring-guide.md#9-where-everything-goes-1999-rav4-4wd).

## 3D-printed housings

![Console pod and TCU enclosure](hardware/preview.png)

Two printable housings, as parametric OpenSCAD models with ready-to-print STLs in
[`hardware/`](hardware/README.md):

- **Console pod:** the ON/OFF (with safety cover), ECO, SPORT and TEST controls, status LED and
  OLED on a sloped panel facing the driver. The Beavis sequential shifter bolts to its top deck.
- **TCU enclosure:** holds the Mega, the main board, the buck converter and the gyro. It has seven
  labelled cable glands and USB access.

Print them in **PETG or ASA, not PLA** (PLA softens in a hot car). The dummy-load resistors stay
outside on a finned heatsink. Print settings, hardware and assembly:
[`hardware/README.md`](hardware/README.md).

## Status

- **Done:** full design, firmware and documentation.
  - 1,400 automated test checks pass.
  - The firmware compiles for the Arduino Mega 2560 without warnings (12 % flash, 39 % RAM).
  - The screens above come from the real firmware running in a simulator.
- **Not done yet:** nothing has been built or run on real hardware or in the car. The gear
  ratios, speed-sensor scaling and throttle-sensor voltages are best estimates until you
  calibrate them (spec §8).

---

## Parts list

Quantities are for one car. Part numbers are examples; equivalents are fine if the ratings match.
**Rough total: US$250–400 in parts**, depending on enclosures, connectors and where you buy.

### 1. Controller and power

| Qty | Part | Notes |
|---|---|---|
| 1 | **Arduino Mega 2560** | Genuine, or a good clone with the ATmega16U2 USB chip. 5 V I/O is required (no Pico/ESP32: their pins are 3.3 V only) |
| 1 | **12 V → 5 V, 3 A automotive buck converter** | Rated for **40 V+ input**. Powers the Mega (into its 5 V pin, not VIN), screen and sensors |
| 2 | Inline blade-fuse holders + fuses | 5 A for the solenoid drivers and relays, 2 A for the logic |
| 1 | Add-a-fuse (fuse tap) | To pick up **ignition-switched** 12 V at the fuse box |
| 1 | TVS diode, SMCJ24A (or 1.5KE27A) | Load-dump protection at the box's 12 V input |
| 1 | Reverse-polarity protection | P-channel MOSFET "ideal diode" module, or a 5 A Schottky diode |

### 2. Transmission solenoid control

| Qty | Part | Notes |
|---|---|---|
| 3 | **High-side smart switch with current sense** (PROFET class, e.g. Infineon BTS6143D or BTS50085) | One each for S1, S2 and SL. The RAV4's computer switches **+12 V** to the solenoids, so low-side MOSFET boards (IRLZ44N etc.) **won't work**. These are surface-mount: use breakout boards or a small PCB |
| 3 | Current-sense resistor for each switch (value from its datasheet), 1 % | Lets the box measure each solenoid's current (self-test and live fault detection) |
| 3 | **DPDT relay, 12 V coil, ≥ 5 A contacts** (e.g. Omron G2RL-2-DC12) | The factory/box changeover, one per solenoid. Coil off = factory control |
| 1 | ULN2803A (or a logic-level MOSFET) + flyback diode | Drives the three relay coils from pin D6. Never drive a coil straight from an Arduino pin |
| 3 | **15 Ω, 50 W aluminium-clad resistor** | Dummy loads, so the factory computer sees a "solenoid" while the box is in control. Each makes **~14 W of heat** continuously |
| 1 | **Finned aluminium heatsink**, ≥ 100 × 100 × 25 mm (~1–1.5 °C/W), + thermal paste | For the three dummy resistors. **Outside** the TCU box, in moving air, ≥ 50 mm from plastic and wiring ([hardware](hardware/README.md#the-dummy-resistors-do-not-go-in-this-box)) |

### 3. Signal inputs (tapped from the car's existing wiring)

| Qty | Part | Used for |
|---|---|---|
| 6 | PC817 optocoupler | Brake switch, lever-in-D signal, the factory computer's S1/S2/SL outputs, tach |
| 1 | LM393 comparator | Vehicle speed signal (high-impedance tap; doesn't load the car's line) |
| 8 | BAT54S dual clamp diode | Input protection: the 6 console controls + throttle signal (VTA) + throttle ground (E2) |
| 2 | 5.1 V zener (or TVS) diode | Battery-sense and ATF-probe line protection |
| — | Resistor kit, 1/4 W, 1 % | Including 100 Ω, 330 Ω, 1 kΩ, 2.2 kΩ, 3.3 kΩ, 4.7 kΩ, 10 kΩ, 47 kΩ, 1 MΩ |
| 6 | 1N4148 diode | Reverse protection across each optocoupler LED |
| — | 100 nF ceramic capacitors (~20) | Filtering on every input |

### 4. Console (overhead or centre console)

| Qty | Part | Notes |
|---|---|---|
| 1 | **ON/OFF toggle switch** (SPST) | Factory ↔ box. A flip-up safety cover is a nice touch |
| 1 | **ECO toggle switch** (SPST) | |
| 1 | **SPORT toggle switch** (SPST) | Both ECO and SPORT off = NORMAL |
| 2 | **Momentary push buttons**: [+] and [−] | Manual shifting. **Or** a sequential shifter lever with two microswitches (next row) |
| (1) | *Option:* **sequential shifter lever** with two sealed microswitches | The DIY build in [Beavis Motorsport's DIY sequential shifter guide](https://www.beavismotorsport.com/sim-racing/diy-sequential-shifter/) (parts: [Thingiverse 4557771](https://www.thingiverse.com/thing:4557771), **printed in PETG/ASA**, two V-155-1C25 roller switches, 12 mm pivot tube, M12 ball plungers, springs, M10 knob). Skip its Arduino Pro Micro. It bolts to the console pod's top deck. Wiring: guide §8. `SWAP_SHIFT_INPUTS` picks push or pull for upshift |
| 1 | **Momentary push button**: TEST | Tap = next screen, hold 2 s = self-test |
| 1 | Panel LED (plain 5 V LED) + 330 Ω resistor | Status: solid = box in control, fast blink = fault |
| 1 | **3D-printed console pod** ([`hardware/`](hardware/README.md#console-pod)) | Or any small panel. Pod switch sizes: 12 mm ON/OFF toggle + flip-up safety cover, 6 mm mini toggles for ECO/SPORT, 12 mm TEST button, 8 mm LED bezel |
| ~3 m | **8-core shielded cable** (24 AWG / 0.22 mm²) | Console to box: 6 controls + ground + LED |

### 5. Screen and gyro

| Qty | Part | Notes |
|---|---|---|
| 1 | **0.96" or 1.3" I²C OLED, 128×64** | SSD1306 (0.96") or SH1106 (1.3"); set `OLED_SH1106` to match. The console pod has a window for the 0.96"; or mount it in the dash |
| 1 | **MPU-6050 gyro module (GY-521)** | Corner detection. Goes **inside the TCU box** on its standoffs, chip up, with the box bolted flat ([guide §9](docs/03-wiring-guide.md#the-gyro)) |
| ~1 m | 4-core shielded cable | Shared I²C link (5 V, GND, SDA, SCL) to the screen. Keep it short: I²C isn't built for long runs |

### 6. Transmission fluid temperature probe

Goes in the **hot** cooler hose (transaxle → radiator), on a straight rubber section near the
radiator. Step by step: [guide §9](docs/03-wiring-guide.md#the-atf-temperature-probe).

| Qty | Part | Notes |
|---|---|---|
| 1 | **DS18B20 probe, high-temperature version** | Threaded stainless housing (1/8" NPT or M10), **PTFE or silicone cable rated 125 °C+**. Cheap PVC-cable probes go soft on a hot cooler line |
| 1 | **Barbed tee with a threaded side port** (brass or aluminium) | Sized to the cooler hose (measure the inside diameter; 5/16" or 3/8" is common) |
| 2–4 | Fuel-injection hose clamps | |
| 1 | ATF-resistant thread sealant | |
| ~0.5 m | Transmission-cooler hose, SAE J1532 | Only if you replace any hose (not heater hose) |
| ~3 m | 3-core shielded cable + firewall grommet | Probe to box. 4.7 kΩ pull-up at the box end (2.2 kΩ if readings drop out) |
| — | ATF to top up | The type listed in your owner's manual |

### 7. Enclosure, wiring and connectors

| Qty | Part | Notes |
|---|---|---|
| 1 | **3D-printed TCU enclosure** ([`hardware/`](hardware/README.md#tcu-enclosure)), 212 × 148 × 51 mm | Or a bought enclosure of about that size. Behind the glovebox, bolted flat (the gyro is inside) |
| 1 | Perfboard or a custom PCB + screw terminals | |
| 6 | Automotive connector pairs (Deutsch DT or Superseal): 3-pin (X1, X6), 4-pin (X5), 6-pin (X2), 8-pin (X3, X4) | So the box unplugs cleanly from the car (pin-outs: wiring guide §3) |
| 1 | Extra 6-pin plug for X2 + 3 short jumpers | **Bypass plug**: jumpers 1–4, 2–5, 3–6; makes the car stock with the box removed |
| — | Automotive wire (TXL/GXL): 18 AWG for the solenoid circuits, 22 AWG for signals | |
| — | Heat-shrink, loom, zip ties | Solder and heat-shrink every tap into the car's harness (no Scotch-lock taps) |

### 8. 3D printing (housings and shifter)

| Qty | Part | Notes |
|---|---|---|
| ~1 kg | **PETG or ASA filament** | About 0.5 kg for the two housings and 0.5 kg for the shifter. Not PLA |
| 8 | M3 heat-set inserts (~4 mm OD) | Pod and TCU box corners |
| 4 + 4 | M3 countersunk screws, 8 mm and 10 mm | Pod bottom plate, TCU lid |
| ~12 | M3 × 6 screws | Mega, main board and gyro standoffs |
| 4 | M2 × 8 screws + nuts | OLED |
| 4 | M6 bolts + nuts | Shifter to the pod deck |
| 4 + 5 | PG9 + PG7 cable glands | TCU box (4 × PG9, 3 × PG7) and pod (2 × PG7) |

Full list: [`hardware/README.md`](hardware/README.md#hardware-for-the-housings).

## Tools and equipment

| Needed | Why |
|---|---|
| **Factory repair manual + electrical wiring diagram for your VIN** | To find the computer's S1/S2/SL, speed, tach, brake and lever-in-D wires, and to confirm the items in spec §10 |
| **Multimeter** | Measuring solenoid resistance, confirming +12 V drive, checking every tap |
| **Laptop + USB A-B cable** | Flashing the Mega (Arduino IDE or PlatformIO) and logging calibration data |
| **OBD-II scan tool** | Confirm no trouble codes are set with the box installed |
| Soldering iron, wire strippers, crimpers, heat gun | Building the box and harness |
| 12 V bench power supply (3 A+) and a spare 15 Ω power resistor | Bench testing before it goes in the car |
| **Lift, or four jack stands** | Your RAV4 is full-time 4WD: **all four wheels** must be off the ground for in-gear testing |
| Drain pan, funnel, hose cutter | Installing the temperature probe |
| Phone GPS speed app | Calibrating the speed signal |
| IR thermometer | Checking the ATF probe's reading, and finding the hot cooler line |
| **3D printer** (250 mm bed; 220 mm works with the no-ears TCU box) | The housings and the shifter. Or use a print service |
| Phone level app | Mounting the TCU box (and gyro) flat |
| Optional: cheap oscilloscope or logic analyser | Checking the speed and tach signals, and whether SL is on/off or pulsed |

## First steps

1. **Check the facts against your car.** Read spec [§0](docs/02-revised-spec.md) (what is
   confirmed for the 1999 RAV4) and confirm every **VERIFY** item in §10 against your repair
   manual. In particular, read the transaxle code on its ID plate.
2. **Bench test.** Build the box, connect 15 Ω resistors in place of the solenoids, and flash the
   firmware. Hold TEST to run the self-test; every line should pass.
3. **Install it with the switch OFF.** In OFF, the car runs entirely on the factory computer while
   the box watches and logs over USB. Drive normally and calibrate the throttle sensor, speed
   signal and gear ratios (spec §8).
4. **Unlock it.** Set `CALIBRATED = true` in `firmware/tcu/config.h` and reflash. Until then the
   ON/OFF switch is ignored and the screen shows `NOT CALIBRATED`.
5. **Test in stages** (spec §9): self-test in the car, then on a lift with all four wheels up,
   then a closed lot, then the road.

## Documents

| Document | Contents |
|---|---|
| [`docs/03-wiring-guide.md`](docs/03-wiring-guide.md) | **Wiring guide:** connectors, wire list, where each signal is on the RAV4 and how to confirm it, circuits, build order, shifter lever |
| [`docs/wiring-diagram.svg`](docs/wiring-diagram.svg) / [`wiring-detail-a.svg`](docs/wiring-detail-a.svg) | System wiring diagram; solenoid channel close-up (regenerate with `python3 tools/wiring_diagram.py`) |
| [`docs/02-revised-spec.md`](docs/02-revised-spec.md) | The full design: wiring, pin map, solenoid logic, every control rule, self-test, calibration, test plan |
| [`docs/oled-screens.png`](docs/oled-screens.png) | What each screen looks like |
| [`hardware/README.md`](hardware/README.md) | 3D-printed console pod and TCU enclosure: print settings, hardware, assembly, mounting |
| [`docs/01-audit.md`](docs/01-audit.md) | Review of the original spec this project started from (rev 0, which still had an oil sender and a Nextion dashboard) and what was fixed |

## Repository layout

```
firmware/tcu/tcu.ino        hardware layer: pins, inputs, relays, watchdog, screen pages, USB log
firmware/tcu/tcu_logic.*    every driving decision (no Arduino code; unit-tested on a PC)
firmware/tcu/self_test.*    built-in test: power-up, continuous and press-to-test (unit-tested)
firmware/tcu/config.h       every tunable number: shift maps, mode features, calibration
firmware/tcu/oled.*         screen driver (SSD1306 / SH1106), never stalls the control loop
firmware/tcu/ds18b20.*      ATF probe driver + tested decoding
firmware/tcu/imu.*          gyro (MPU-6050) yaw-rate reader
test/                       unit tests
tools/avr_build.sh          compile for the Mega without the Arduino IDE
tools/wiring_diagram.py     regenerates the wiring diagrams in docs/
tools/build_stl.sh          exports the STLs and preview in hardware/
hardware/*.scad             3D-printable console pod and TCU enclosure (OpenSCAD)
hardware/stl/               ready-to-print STL files
```

## Build and flash

```sh
make test   # run the unit tests on a PC (g++)
make avr    # compile for the Mega 2560 (needs: apt install gcc-avr avr-libc arduino-core-avr)
```

To flash, open `firmware/tcu/tcu.ino` in the Arduino IDE (board: **Arduino Mega 2560**) or run
`pio run -t upload` with PlatformIO. No extra libraries to install (only `Wire`, which ships with the Arduino core).

---

**Safety:** this controls the transmission of a road car. Do all early testing off public roads,
keep the ON/OFF switch within reach, and check your local regulations and insurance.
