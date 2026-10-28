# 1999 RAV4 Transmission Controller

A standalone transmission controller for a **1999 Toyota RAV4** (first generation, 4-speed
automatic, 3S-FE engine). An Arduino Mega takes over the transmission's three solenoids and adds
drive modes, manual shifting and an aircraft-style self-test, while the engine and its computer
stay 100 % stock.

**The factory computer is always the backup.** Flip the ON/OFF switch off, lose power, or let
anything go wrong, and relays hand the transmission straight back to the factory computer. The
car then drives exactly as it did from the factory. The ON/OFF switch cuts the relays' power
**directly, in hardware**, so OFF works even if the Arduino itself has failed. How every failure
is handled, and how each one is tested, is in the [system review](docs/06-system-review.md).

![Dash pod screen](docs/oled-screens.png)

## What it does

All speeds are in **mph**; the screen shows temperatures in **°F**.

Three mode switches sit on the dash: **ECO, SPORT and MANUAL. Exactly one is on at a time.** If
none is on, or two or more are, the screen says so in plain words (MODE ERROR / PICK MODE) and
the factory computer shifts until you fix it; then the box takes back over by itself. There is
no NORMAL mode: with the ON/OFF switch off, you have stock.

| Mode | Feel | What changes |
|---|---|---|
| **ECO** | Calm, economical | Early upshifts; torque-converter lockup as early as the engine can take it (from ~23 mph in 3rd), kept on when you lift for longer fuel cut, released if the engine would labour; pedal stabs ignored (0.8 s throttle smoothing). Flooring it still kicks down instantly. |
| **Highway (all modes)** | Locked and steady | In 4th above ~51 mph the torque converter stays locked up to 95 % throttle, so a long 90 mph run (~3,550 rpm) or a highway grade doesn't let it slip: lower rpm, less fuel, much less transmission heat. Only flooring it releases lockup (in SPORT, a quick stab does too). |
| **SPORT** | Sharp | Holds each gear to just under the redline at full throttle (the fastest way through these gears), kicks down from 60 % throttle, holds the gear when you snap off the throttle between corners, downshifts while braking so you're in the right gear for the exit, and releases lockup on a closed throttle or a fast pedal stab. |
| **MANUAL** | SPORT, you shift | Turn on **MANUAL** (with ECO and SPORT off), then pull or push the sequential shifter lever: one click, one gear. Same lockup and pedal response as SPORT, but the box never shifts for you, except to protect the engine (upshift at 6,200 rpm, downshift when slowing to a stop). In ECO or SPORT the lever does nothing. |
| **STOCK** | Factory | ON/OFF off, the lever not in D, a fault, or a mode-switch mistake: the factory computer shifts, exactly as before. |
| **All automatic modes** | | No automatic shifts mid-corner (gyro), lockup off when braking. |

**Protection, always on:**
- **No over-rev:** every downshift is checked against engine rpm (refused above 5,800), 1st is
  never allowed above 28 mph, and the box upshifts automatically at 6,200 rpm.
- **No stalling:** lockup releases when you brake and at low speed.
- **Fluid temperature:** lockup is blocked while the fluid is cold, and engages earlier when it's
  hot (110 °C+). Locking up is what cools the fluid.
- **Faults hand back to the factory computer:** a lost speed signal, a failed throttle sensor,
  bad voltage, the wrong gear engaged, a broken solenoid wire, a stuck relay, or the ON/OFF
  switch's two halves disagreeing (a broken wire in the dash cable).
- **Safe takeover:** the box only takes over after a deliberate OFF→ON flip (never by itself at
  key-on or after a reset), and only when the gear it senses from the factory computer agrees
  with the engine rpm; otherwise it waits until you stop.
- **Built-in self-test:** hold TEST for 2 s with the car parked, the engine off and ON/OFF on.
  It checks the screen, each solenoid's current, the relays, the shifter and every dash switch,
  that ON/OFF really cuts the relays by itself, and the watchdog. It refuses to run unless the
  car is parked with the engine off. A shorter relay check also runs at every key-on.
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

**The whole system is 8 cables**, each into its own labelled hole in the box:

![Cable map: 8 cables, one per labelled hole](docs/cable-map.png)

Only three factory wires are cut (S1, S2, SL); everything else is a tap. A **bypass plug**
restores stock driving with the box removed.

**Where everything goes in the car** (TCU box, gyro, heatsink, dash pod, shifter base, ATF probe, plus the
4WD-specific notes) is [guide §9](docs/03-wiring-guide.md#9-where-everything-goes-1999-rav4-4wd).

## 3D-printed housings

![Dash pod, shifter base and TCU box](hardware/preview.png)

Three separate pieces, each with its own cables to the TCU box. They're parametric OpenSCAD
models with ready-to-print STLs in [`hardware/`](hardware/README.md):

1. **Dash pod**, on top of the dash (driver's side): the 2.42" screen on the left, then one row of
   switches, **ECO, SPORT, MANUAL, TEST, ON/OFF** (with a flip-up safety cover). It sits on a
   small hinged mount and tilts to face you.
2. **Shifter base**, on the centre console next to the gear lever: the real
   [Beavis Motorsport sequential shifter](hardware/beavis_shifter/README.md) (by bbeavis,
   CC BY-NC) bolts on top, with its own cable to the box.
3. **TCU box**, under the passenger side of the dash where you can reach it: the Mega with its
   hub shield, the power board and the logic board (which carries the gyro). It has no switches:
   the round things on its front are eight labelled cable glands, one per cable.

A **fit check** in each model confirms that every part fits: switch bodies, terminals, wire
bends, board parts at their real heights, glands and bolts. It runs every time the STLs are
rebuilt.

![Fit check: TCU box with boards](hardware/fit-tcu.png)

Print them in **PETG or ASA, not PLA** (PLA softens in a hot car). The dummy-load resistors stay
outside on a finned heatsink. Print settings, hardware and assembly:
[`hardware/README.md`](hardware/README.md).

## Status

- **Done:** full design, firmware and documentation.
  - `make check` runs it all: 1,256 unit-test checks, 25 whole-system scenarios (the real
    firmware in a simulated car, with faults injected, plus the build book's jack-stand test step
    by step), the calibration helper on a simulated drive, and about 500 cross-checks that the
    boards, pin map, firmware, screen text and 3D models agree. All pass
    ([system review](docs/06-system-review.md)).
  - The firmware compiles for the Arduino Mega 2560 without warnings (15 % flash, 51 % RAM).
  - The screens above come from the real firmware running in a simulator.
- **Not done yet:** nothing has been built or run on real hardware or in the car. The gear
  ratios, speed-sensor scaling and throttle-sensor voltages are best estimates until you
  calibrate them (spec §8).

---

## Parts and cost

**Full shopping list, with a price estimate for every item:
[`docs/04-shopping-list.md`](docs/04-shopping-list.md)** (also as a spreadsheet:
[`shopping-list.csv`](docs/shopping-list.csv)). It lists what to buy, which store, and what to
measure on the car first so you order the right sizes once.

| | Estimate (US$, late 2026) |
|---|---|
| Brain and power (Mega, hub shield, 5 V supply, fuses) | $66 |
| Solenoid control (relays, MOSFETs, current sensors, dummy loads + heatsink) | $82 |
| Signal inputs (optos, comparator, resistors, diodes) | $42 |
| Boards and inside wiring | $66 |
| Car harness and connectors (Deutsch kit with crimper, wire, shielded cable, glands) | $147 |
| Dash pod controls, OLED, gyro | $61 |
| ATF temperature probe and fittings | $59 |
| 3D printing (filament, inserts, screws) | $65 |
| Sequential shifter (Beavis Motorsport DIY) | $59 |
| **Total parts** | **≈ $650** (±25 %) |
| Tools, if you own none (your 3D printer and jack stands aren't counted) | ≈ $200 |

Many items are packs and kits, so you'll have spares. The big-ticket items are the connector kit
with its crimper, the wire and cable, and the filament.

The key parts:

- **Arduino Mega 2560** + a **Mega prototype shield** (the hub the two ribbon cables plug into).
- **3 × Omron G2RL-2-DC12 DPDT relays**: the factory/box changeover. Coil off = factory computer.
- **3 × IRF4905 P-MOSFETs + 3 × ACS712-05B current sensors**: switch +12 V to each solenoid
  and measure its current. (The RAV4's computer switches +12 V, so low-side MOSFET boards
  won't work.)
- **3 × 15 Ω 50 W resistors on a finned heatsink**: dummy loads, so the factory computer sees a
  "solenoid" while the box is in control. Outside the box: they make ~14 W each.
- **LM2596HV buck converter** (60 V input), PC817 optos, LM393, ULN2803A.
- **Dash pod:** ON/OFF toggle with a flip-up cover, ECO, SPORT and MANUAL mini toggles, TEST
  button, and a **2.42" OLED** (the gear shows 18 mm tall, everything else in plain words).
- **Shifter base:** the **Beavis sequential shifter** (two V-155-1C25 switches).
- **MPU-6050 gyro (GY-521)** on the logic board, and a **DS18B20 ATF probe** in a brass tee.

## Tools and equipment

| Needed | Why |
|---|---|
| **Factory repair manual + electrical wiring diagram for your VIN** | To find the computer's S1/S2/SL, speed, tach, brake and lever-in-D wires, and to confirm the items in spec §10 |
| **Multimeter** with continuity beep | Every test in the build book uses it |
| **Temperature-controlled soldering iron**, solder, flux | Two perfboards and the hub |
| Wire stripper, heat gun, hot-glue gun | Harness and boards (the Deutsch kit includes its crimper) |
| **12 V 5 A DC adapter** | Bench power for testing |
| **Laptop + USB A-B cable** | Flashing the Mega and logging calibration data |
| **OBD-II scan tool** | Confirm no trouble codes are set with the box installed |
| **Four jack stands** (you have them) | Your RAV4 is full-time 4WD: **all four wheels** must be off the ground for in-gear testing |
| **3D printer** (you have one) | The housings and the shifter. 250 mm bed, or 220 mm with the no-ears TCU box |
| Drain pan, funnel, IR thermometer, phone level and GPS apps | Probe install, mounting the box flat, calibration |

## First steps

**Follow the [build book](docs/05-build-book.md)**: every step in order, from measuring the car
to the first drive, with a picture for each board step. In outline:

1. **Check the facts against your car.** Read spec [§0](docs/02-revised-spec.md) (what is
   confirmed for the 1999 RAV4) and confirm every **VERIFY** item in §10 against your repair
   manual. In particular, read the transaxle code on its ID plate.
2. **Bench test.** Build the box, connect 15 Ω resistors in place of the solenoids, and flash the
   firmware. Hold TEST to run the self-test; every line should pass. Build book part 10 walks
   through it, with a table of what to check for each failed line.
3. **Install it with the switch OFF.** In OFF, the car runs entirely on the factory computer while
   the box watches and logs over USB.
4. **Calibrate with the helper.** `tools/calibrate.py` does the maths: a 5-minute parked step
   (gas pedal, battery) and one drive with a passenger at the laptop (speed signal, gear ratios).
   It writes the numbers into `config.h`, and `calibrate.py unlock` unlocks the box once every
   step is done. Until then the ON/OFF switch is ignored and the screen shows `NOT CALIBRATED`.
5. **Test in stages** (build book part 13): jack stands with all four wheels up, then an empty
   car park, then the road.

## Documents

| Document | Contents |
|---|---|
| [`docs/03-wiring-guide.md`](docs/03-wiring-guide.md) | **Wiring guide:** connectors, wire list, where each signal is on the RAV4 and how to confirm it, circuits, build order, shifter lever |
| [`docs/wiring-diagram.svg`](docs/wiring-diagram.svg) / [`wiring-detail-a.svg`](docs/wiring-detail-a.svg) | System wiring diagram; solenoid channel close-up (regenerate with `python3 tools/wiring_diagram.py`) |
| [`docs/06-system-review.md`](docs/06-system-review.md) | **System review:** how it all fits together, every failure and what happens, the safety chain, test results, what's left |
| [`docs/02-revised-spec.md`](docs/02-revised-spec.md) | The full design: wiring, pin map, solenoid logic, every control rule, self-test, calibration, test plan |
| [`docs/oled-screens.png`](docs/oled-screens.png) | What each screen looks like |
| [`docs/05-build-book.md`](docs/05-build-book.md) | **Build book:** every step in order, Lego-instruction style, with a picture for each board step |
| [`docs/04-shopping-list.md`](docs/04-shopping-list.md) | **Shopping list** with cost estimates, by store |
| [`hardware/README.md`](hardware/README.md) | 3D-printed dash pod, shifter base and TCU enclosure: print settings, hardware, assembly, mounting |
| [`docs/01-audit.md`](docs/01-audit.md) | Review of the original spec this project started from (rev 0, which still had an oil sender and a Nextion dashboard) and what was fixed |

## Repository layout

```
firmware/tcu/tcu.ino        hardware layer: pins, inputs, relays, watchdog, screen pages, USB log
firmware/tcu/tcu_logic.*    every driving decision (no Arduino code; unit-tested on a PC)
firmware/tcu/self_test.*    built-in test: power-up, continuous and press-to-test (unit-tested)
firmware/tcu/config.h       every tunable number: shift maps, mode features, calibration
firmware/tcu/oled.*         screen driver (SSD1309 / SSD1306 / SH1106), never stalls the control loop
firmware/tcu/ds18b20.*      ATF probe driver + tested decoding
firmware/tcu/imu.*          gyro (MPU-6050) yaw-rate reader
test/                       unit tests; test/system/: whole-firmware simulation with fault injection
tools/system_check.py       cross-checks boards, pins, firmware, screens and 3D models agree
tools/calibrate.py          calibration helper: records the USB log, works out the numbers
tools/avr_build.sh          compile for the Mega without the Arduino IDE
tools/wiring_diagram.py     regenerates the wiring diagrams in docs/
tools/build_stl.sh          fit-checks the housings, exports the STLs and previews in hardware/
tools/boards.py             board layouts: checks them, draws the build-book pictures and templates
tools/shopping_list.py      writes the shopping list and its cost totals
docs/build/                 build-book pictures and 1:1 board templates (generated)
hardware/*.scad             3D-printable dash pod, shifter base and TCU enclosure (OpenSCAD)
hardware/stl/               ready-to-print STL files
```

## Build and flash

```sh
make check  # everything: unit tests, system simulation, cross-checks
make test   # just the unit tests on a PC (g++)
make avr    # compile for the Mega 2560 (needs: apt install gcc-avr avr-libc arduino-core-avr)
```

To flash, open `firmware/tcu/tcu.ino` in the Arduino IDE (board: **Arduino Mega 2560**) or run
`pio run -t upload` with PlatformIO. No extra libraries to install (only `Wire`, which ships with the Arduino core).

---

**Safety:** this controls the transmission of a road car. Do all early testing off public roads,
keep the ON/OFF switch within reach, and check your local regulations and insurance.
