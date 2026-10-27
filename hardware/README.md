# 3D-printed housings

The system is **three separate pieces**, each with its own housing and its own cables to the TCU
box. All three are parametric OpenSCAD models: every hole size is a named variable at the top of
the file, so you can measure your parts, change the numbers, and re-export.

![Dash pod, shifter base and TCU box](preview.png)

| # | Model | Where it goes | What it holds | Print files (`stl/`) |
|---|---|---|---|---|
| 1 | [`dash_pod.scad`](dash_pod.scad) | Dash top (driver's side) or ceiling | **The screen and every switch, in one row:** 2.42" OLED on the left, then the status LED, ECO, SPORT, MANUAL, TEST and ON/OFF. Two cable glands in the back cover. Hangs on a hinge from a small mount, so it stays upright and tilts to face you on the dash or on the ceiling | `dash_pod_shell.stl`, `dash_pod_back.stl`, `dash_pod_mount.stl`, `dash_pod_panel_test.stl` |
| 2 | [`shifter_base.scad`](shifter_base.scad) | Centre console, next to the gear lever | The Beavis Motorsport sequential shifter bolts on top (4 × M6). Inside, lever connectors join its two switches to a 3-core cable. One gland | `shifter_base_box.stl`, `shifter_base_bottom.stl` |
| 3 | [`tcu_enclosure.scad`](tcu_enclosure.scad) | Behind the glovebox | The electronics: Arduino Mega + hub shield, power board, logic board (with the **gyro**). No switches: the round things on its front wall are eight labelled cable glands. USB access, screw-on lid | `tcu_enclosure_box.stl` (or `tcu_enclosure_box_no_ears.stl`), `tcu_enclosure_lid.stl` |

Regenerate the STLs and the previews with `tools/build_stl.sh` (needs OpenSCAD 2021.01 or later).
It runs the **fit check** first and stops if anything collides.
To change a size, open the `.scad` file in OpenSCAD, edit the variable (or use the Customizer
panel), press F6, and export the STL.

## Material and print settings

**Print in PETG or ASA (ABS also works). Not PLA.** PLA softens at about 55 °C, and the inside
of a car parked in the sun gets hotter than that, the dash top most of all. A PLA pod will sag,
and a PLA box will let its standoffs creep.

| Setting | Value |
|---|---|
| Layer height | 0.2 mm |
| Walls / perimeters | 4 (about 1.6 mm), so screws and inserts have material to bite |
| Top / bottom layers | 5 |
| Infill | 30–40 %, gyroid or cubic |
| Supports | **None needed** in the orientations below |
| Brim | Helps ASA/ABS on the big flat parts |

| Part | Orientation on the bed (the STLs are already oriented) | Approx. size |
|---|---|---|
| **Panel test** (print first!) | Flat, outer face on the bed. A copy of the dash pod's front: check every switch and the screen fit before printing the shell | 210 × 64 × 6 mm, ~1 h |
| Dash pod shell | Front face on the bed (the engraved labels print crisp) | 210 × 64 × 60 mm |
| Dash pod back cover | Flat, hinge lugs pointing up | 210 × 64 × 4 mm + lugs |
| Dash pod mount | Base on the bed, arm up | 70 × 44 × 59 mm |
| Shifter base | Upside down, top deck on the bed | 150 × 100 × 26 mm |
| Shifter base bottom plate | Flat | 150 × 100 × 3 mm |
| TCU box | Floor on the bed | 244 × 178 × 48 mm with ears; **216** × 178 without |
| TCU lid | Upside down, lettering on the bed, locating lip up | 216 × 178 × 5 mm |

**Bed size:** everything except the TCU box with ears fits a 220 × 220 mm bed (Ender 3 class).
That box needs a 250 mm bed (Prusa MK3/MK4, Bambu, Voron and similar). On a 220 mm bed, print
`tcu_enclosure_box_no_ears.stl` (or set `ears = false`): it is 216 mm wide (no brim: there's only
2 mm to spare) and bolts down through four M5 holes in its floor.

## Does everything fit? (the fit check)

Yes. Every model has a `fit_check` part that models everything that goes inside as solid
blocks and intersects them with the housing and with each other. An empty result means nothing
collides. `tools/build_stl.sh` runs it every time.

| Housing | What the check includes |
|---|---|
| TCU box | Every part on both boards at its real height (relays 15.7 mm, MOSFETs 19 mm standing, current sensors and buck module on foam tape, plugged ribbon headers), 3 mm under each board for solder joints and wires, the Mega + hub shield stack with its plugged ribbons (34 mm), the USB jack and its plug's moulding, every gland's locknut and cable, the lid-screw bosses and the lid's locating lip |
| Dash pod | Each switch and button body with its terminals and a wire bend (ON/OFF 50 mm deep, mini toggles 32 mm, button 38 mm), the LED and bezel (30 mm), the 2.42" OLED module with its wires (14 mm), the glands' locknuts and cables, a row of lever-nut connectors, the ON/OFF switch's flip cover outside, and **the pod tilting on its hinge** in 5° steps from 35° towards you to 5° away without touching the mount |
| Shifter base | The shifter's M6 bolt tips, the mounting-bolt heads, the gland's locknut and cable, and the lever-nut connectors |

![Fit check: TCU box](fit-tcu.png)

What the check found and fixed:

- The original single 80 × 120 mm main board couldn't hold everything (its parts alone covered
  about 80 % of it, with no room for wiring). It's now a 90 × 150 mm **power board** and a
  90 × 70 mm **logic board**, and the box grew to 216 × 178 mm.
- The USB cut-out was too small for a USB-B plug's moulding to reach the jack: now 18 × 15 mm.
- One lid-screw boss clipped the power board's corner: the box is 2 mm deeper.
- The gyro's header pins would have hit the floor on its old 5 mm standoffs. It now sits on
  the logic board.
- In the dash pod, the pod's back edge hit the mount's arm when tilted: the arm is now slimmer
  and taller.
- In the shifter base, the gland's locknut hit the underside of the deck: the gland moved down.

## Hardware for the housings

| Qty | Part | For |
|---|---|---|
| 12 | **M3 heat-set inserts**, about 4 mm OD × 5–6 mm long (fits the 4.2 mm holes) | 4 each in the dash pod, the shifter base and the TCU box |
| 4 | M3 × 10 countersunk screws | Dash pod back cover |
| 4 | M3 × 8 countersunk screws | Shifter base bottom plate |
| 4 | M3 × 10 countersunk screws | TCU lid |
| 14 | M3 × 6 screws (self-tapping into the 2.6 mm standoff holes) | Mega (6), power board (4), logic board (4) |
| 1 | **M5 × 25 bolt + M5 nyloc nut** + washer | Dash pod hinge. The nut sits in the hex pocket of the right-hand lug |
| 2 | Countersunk wood/plastic screws, about 4 mm, or 3M VHB tape | Dash pod mount to the dash or ceiling |
| 4 | M6 bolts + **M6 hex nuts** (length = shifter's base thickness + 6 mm) | Shifter to the shifter base. The nuts drop into the hex traps under the deck |
| 8 | M5 or M6 bolts, large washers | Shifter base bottom plate to the console (slots), TCU ears or floor holes to a bracket |
| 4 | **PG9 cable glands** (cable 4–8 mm) | TCU: PWR (X1), SOL (X2), SIG (X3), LOAD (heatsink) |
| 7 | **PG7 cable glands** (cable 3–6.5 mm) | TCU: SHIFT (X7), DASH (X4), SCREEN (X5), ATF (X6). Dash pod: SWITCHES, SCREEN. Shifter base: TO TCU |
| 2 | Zip ties | Buck converter in its cradle |

Measure your glands: PG9 needs a 15.2 mm hole and PG7 a 12.5 mm hole, but some brands differ by
half a millimetre. Fix it with `glands` (TCU, dash pod) or `gland_d` (shifter base).

Heat-set inserts: push each one in with a soldering iron at about 220–240 °C (PETG) or
250 °C (ASA/ABS) until it sits flush, and keep it square.

## Dash pod

### Front controls

Left to right, as you sit in the driver's seat. The holes are set in `dash_pod.scad`'s `controls`
list; change a control's `u` value there to move it. **Check your switches before printing:**
sizes vary.

| Control | Part | Hole | Variable |
|---|---|---|---|
| Screen | 2.42" SSD1309 module (61.5 × 39.5 mm) in a pocket of ribs behind a 58 × 30.5 mm window, held by double-sided foam tape | — | `oled_*` |
| TCU (status LED) | 5 mm LED in an 8 mm panel bezel. Lit = the TCU is shifting. The 330 Ω resistor is already at the TCU end | 8.0 mm | `led_d` |
| ECO, SPORT, MANUAL | 6 mm mini toggles (MTS-101 / MTS-102 type). Lever up = on | 6.3 mm | `toggle_small_d` |
| TEST | 12 mm momentary push button | 12.2 mm | `button_d` |
| ON/OFF | 12 mm toggle (SPST) with a **flip-up safety cover** (aircraft-style "missile switch" cover). Closing the cover switches it off | 12.2 mm | `toggle_big_d` |

What each switch does: **ECO** and **SPORT** pick the shift map (both off = NORMAL). **MANUAL**
on = the shifter lever shifts (with SPORT's lockup and pedal response); off = the TCU shifts by
itself. **ON/OFF** off = the factory computer drives, whatever the others say. Hold **TEST**
for 2 s (parked, ON/OFF off) for the self-test; tap it to page through the screens.

The front is 3 mm thick, which suits most panel-mount switches (they usually take 1–6 mm).
Labels are engraved 0.6 mm deep under each control. Fill them with paint or a paint pen for
contrast. The ON/OFF flip cover's size varies by brand: it sits over the hole and flips up, and
the pod leaves 26 mm above the hole for it. Check yours on the panel test.

### Mount and hinge

The pod's back cover has two lugs; the mount's arm has a knuckle that fits between them. One
M5 × 25 bolt goes through all three, with a nyloc nut in the hex pocket on the right-hand lug.
Tighten it until the pod holds its angle over bumps.

- **Dash top:** base down, arm up. Two countersunk screws into the dash pad's frame, or 3M VHB
  tape on a cleaned flat spot. Driver's side only, **never over the passenger airbag**.
- **Ceiling:** turn the mount over, base up, arm hanging down. The pod still hangs upright on
  its hinge, so the screen reads the right way up, and it tilts down towards you. Screw the base
  through the headliner into something solid (a sun-visor screw point with longer screws, or
  the roof bow). The headliner alone won't hold it.

The screen cable can be up to **2.5 m** long: the firmware runs I²C at the standard 100 kHz, and
the two modules' pull-ups handle a shielded cable that long. Both cables leave from the back.

### Assembly order

1. Fit the four heat-set inserts in the ends of the corner posts.
2. Drop the screen into its pocket behind the window and fix it with double-sided foam tape on
   its glass border. Fit the LED bezel, the three toggles, the TEST button and ON/OFF.
3. Fit the two PG7 glands in the back cover. Wire each control to its core:
   [wiring guide §3](../docs/03-wiring-guide.md), X4 C1–C7. Every switch's other side goes to
   C7.
4. Screw the back cover on (4 × M3 countersunk).
5. In the car: fix the mount, then hang the pod on it with the M5 bolt.

## Shifter base

A low box that the Beavis Motorsport DIY sequential shifter bolts onto, with its own cable to
the TCU. The top deck has the shifter's mounting pattern: 4 × M6, 116 × 74 mm, with hex-nut
traps underneath and a 26 × 14 mm hole for its switch wires. Move the shifter on the deck with
`shifter_offset` if you need it closer to the driver.

1. Print the shifter parts from Thingiverse
   [4557771](https://www.thingiverse.com/thing:4557771) (the Beavis guide:
   [beavismotorsport.com/sim-racing/diy-sequential-shifter](https://www.beavismotorsport.com/sim-racing/diy-sequential-shifter/)).
   The guide says PLA, but **in a car print them in PETG or ASA too**, with the same 4 walls
   and 30–40 % infill.
2. Build the lever as the guide describes: 12 mm pivot tube (or an M12 bolt), M3 × 70 bolts,
   M12 ball plungers, springs and an M10 × 1.5 knob.
3. Use only the two **V-155-1C25 roller limit switches**. **You don't need the Arduino Pro
   Micro**: the TCU reads the switches directly.
   - Upshift switch: NO → the cable's UP core, COM → GND.
   - Downshift switch: NO → DOWN, COM → GND.
   - Feed the wires down through the deck hole and join them to the 3-core cable inside the
     base with three lever-nut connectors.
   - Rally style (pull = upshift) or push = upshift is `SWAP_SHIFT_INPUTS` in `config.h`.
     You don't need to rewire.
4. Drop four M6 nuts into the traps under the deck, and bolt the shifter down from the top.

**Rigid mounting matters.** Every shift pushes on the lever. Bolt the bottom plate to the
console or to a steel bracket through its four slots, using large washers. Do that first, then
screw the base onto the plate (4 × M3 countersunk, from underneath). Check that the lever can't
touch the factory gear lever, the handbrake, the cup holder or your knee at either end of its
travel.

## TCU enclosure

### What goes where (inside, looking down with the gland wall towards you)

| Area | Part | Notes |
|---|---|---|
| Back left | **Arduino Mega 2560 + hub shield** on 6 standoffs (official hole pattern) | USB-B lines up with the cut-out in the left wall, for flashing and the USB log. "MEGA (USB TO THE LEFT)" is engraved under it |
| Right | **Power board**, 90 × 150 mm perfboard on 4 standoffs (3 mm in from each corner) | Everything that touches the car's 12 V wiring. Faces the SIG, PWR, SOL and LOAD glands |
| Front left | **Logic board**, 90 × 70 mm perfboard on 4 standoffs | Shifter, dash, screen and ATF terminals, and the **gyro**, flat on the board. Faces the SHIFT, DASH, SCREEN and ATF glands |
| Front wall | 8 glands (round holes, not switches), engraved **SHIFT DASH SCREEN ATF · SIG PWR SOL LOAD** | Fit the inline connectors (X1–X3, X6) on the cable tails outside the box |

The board names are engraved on the floor under each board. If your perfboards' corner holes
aren't 3 mm in from the edges, change `pb_hole_inset` or drill new holes using the 1:1
templates in [`docs/build/`](../docs/build/).

| Gland | Connector / cable | Gland |
|---|---|---|
| PWR | X1: +12 V drive, +12 V logic, ground | PG9 |
| SOL | X2: S1/S2/SL from the ECM and to the solenoids (6 cores) | PG9 |
| SIG | X3: D, STP, E1, SPD, TAC, VTA, E2 | PG9 |
| LOAD | Dummy-load heatsink: 3 feeds + ground, 18 AWG silicone wire | PG9 |
| SHIFT | X7: shifter base, 3-core shielded | PG7 |
| DASH | X4: dash pod switches and LED, 8-core shielded | PG7 |
| SCREEN | X5: dash pod screen, 4-core shielded | PG7 |
| ATF | X6: ATF probe, 3-core shielded | PG7 |

The box is 216 × 178 × 48 mm (51 mm with the lid; 244 mm wide with its mounting ears).

### The dummy resistors do NOT go in this box

The three 15 Ω / 50 W resistors make about **14 W each** whenever their channel is under the
box's control, and up to ~28 W in total. That would cook a plastic box. Mount them outside:

- On a **finned aluminium heatsink**, at least 100 × 100 × 25 mm (about 1–1.5 °C/W). Use
  thermal paste and 2 × M3 screws per resistor.
- At 28 W that runs roughly 40 °C above the air around it. Mount it where air moves (under the
  dash, not wrapped in carpet or insulation), on metal, at least 50 mm from any plastic,
  wiring or the TCU box.
- Wire it through the **LOAD** gland with high-temperature (silicone or PTFE) 18 AWG wire.

### Mounting

- **Mount the box flat, on its floor**, so the gyro (on the logic board) lies level. Check with a phone's
  level app: within about 5° is plenty, since a 5° tilt changes the reading by less than 0.5 %.
- **Bolt it rigidly** to metal: the dash support structure behind the glovebox, or a steel
  bracket screwed to it. Use the ears (M6 slots) or, on the no-ears version, the four M5 floor
  holes. **Don't use Velcro, foam tape or zip ties alone.** A box that can wobble feeds the
  gyro vibration and its own rocking.
- Where in the car doesn't matter to the gyro. Every point of a rigid car turns at the same
  yaw rate, so it reads the same behind the glovebox as at the centre of the car.
- Keep the box away from the heatsink and the heater ducts. The gyro's zero drifts with
  temperature; the firmware re-learns it at every stop, but there's no point making it work
  harder.

### Assembly order

The [build book](../docs/05-build-book.md) has the full sequence (part 11). In short:

1. Fit the four heat-set inserts in the corner bosses.
2. Fit the glands.
3. Screw down the Mega, the power board and the logic board, and plug the hub shield onto
   the Mega.
4. Plug in both ribbon cables, feed each cable through its gland, wire it to its terminals, and
   tighten the gland nut.
5. Screw the lid on (4 × M3 countersunk).
