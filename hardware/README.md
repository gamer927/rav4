# 3D-printed housings

Two parts to print, both parametric OpenSCAD models. Every hole size is a named variable at the
top of the file, so you can measure your switches and boards, change the numbers, and re-export.

![Console pod and TCU enclosure](preview.png)

| Model | What it holds | Print files (`stl/`) |
|---|---|---|
| [`console_pod.scad`](console_pod.scad) | A wedge pod for the centre console. **Sloped front panel:** ON/OFF, ECO and SPORT toggles, TEST button, status LED, OLED window (optional [+]/[−] buttons). **Flat top deck:** bolt pattern for the Beavis Motorsport sequential shifter. Two rear cable glands | `console_pod_shell.stl`, `console_pod_bottom.stl` |
| [`tcu_enclosure.scad`](tcu_enclosure.scad) | The controller box behind the glovebox: Arduino Mega, main board, buck converter, **gyro**, seven labelled cable glands, USB access, screw-on lid | `tcu_enclosure_box.stl` (or `tcu_enclosure_box_no_ears.stl`), `tcu_enclosure_lid.stl` |

Regenerate the STLs and the preview with `tools/build_stl.sh` (needs OpenSCAD 2021.01 or later).
To change a size, open the `.scad` file in OpenSCAD, edit the variable (or use the Customizer
panel), press F6, and export the STL.

## Material and print settings

**Print in PETG or ASA (ABS also works). Not PLA.** PLA softens at about 55 °C, and the inside
of a car parked in the sun gets hotter than that. A PLA pod will sag under the shifter, and a
PLA box will let its standoffs creep.

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
| Pod shell | Upside down, top deck on the bed. The sloped panel is then a 30° overhang | 150 × 130 × 70 mm |
| Pod bottom plate | Flat | 143 × 123 × 3 mm |
| TCU box | Floor on the bed | 244 × 148 × 48 mm with ears; **212** × 148 without |
| TCU lid | Upside down, lettering on the bed, locating lip up | 212 × 148 × 5 mm |

**Bed size:** the TCU box with mounting ears needs a 250 mm bed (Prusa MK3/MK4, Bambu, Voron
and similar). On a 220 × 220 mm bed (Ender 3 class), print `tcu_enclosure_box_no_ears.stl`
(or set `ears = false`): it is 212 mm wide and bolts down through four M5 holes in its floor.

## Hardware for the housings

| Qty | Part | For |
|---|---|---|
| 8 | **M3 heat-set inserts**, about 4 mm OD × 5–6 mm long (fits the 4.2 mm holes) | 4 in the pod's corner bosses, 4 in the TCU box corners |
| 4 | M3 × 8 countersunk screws | Pod bottom plate |
| 4 | M3 × 10 countersunk screws | TCU lid |
| ~12 | M3 × 6 screws (self-tapping into the 2.6 mm standoff holes) | Mega (6), main board (4), gyro (2) |
| 4 | M2 × 8 screws + nuts | OLED module behind its window |
| 4 | M6 bolts + **M6 hex nuts** (length = shifter base thickness + 6 mm) | Shifter to the pod deck. The nuts drop into the hex traps under the deck |
| 4 | M5 or M6 bolts, large washers | Pod bottom plate to the console (slots), TCU ears or floor holes to a bracket |
| 4 | **PG9 cable glands** (cable 4–8 mm) | TCU: PWR (X1), SOL (X2), SIG (X3), LOAD (heatsink) |
| 5 | **PG7 cable glands** (cable 3–6.5 mm) | TCU: CON (X4), OLED (X5), ATF (X6). Pod: console cable, OLED cable |
| 2 | Zip ties | Buck converter in its cradle |

Measure your glands: PG9 needs a 15.2 mm hole and PG7 a 12.5 mm hole, but some brands differ by
half a millimetre. Fix it with `glands` (TCU) or `gland_d` (pod).

Heat-set inserts: push each one in with a soldering iron at about 220–240 °C (PETG) or
250 °C (ASA/ABS) until it sits flush, and keep it square.

## Console pod

### Front panel controls

Panel holes, as set in `console_pod.scad`. **Check your switches before printing:** sizes vary.

| Control | Part | Hole | Variable |
|---|---|---|---|
| ON/OFF | 12 mm toggle (SPST) with a **flip-up safety cover** (aircraft-style "missile switch" cover) | 12.2 mm | `toggle_big_d` |
| ECO, SPORT | 6 mm mini toggles (MTS-101 / MTS-102 type) | 6.3 mm | `toggle_small_d` |
| TEST | 12 mm momentary push button | 12.2 mm | `button_d` |
| [+] / [−] (optional) | 12 mm momentary push buttons. Set `with_shift_buttons = true` | 12.2 mm | `button_d` |
| Status LED | 5 mm LED in an 8 mm panel bezel. The 330 Ω resistor is already at the TCU end | 8.0 mm | `led_d` |
| OLED | 0.96" SSD1306 I²C module behind a 25 × 14 mm window, 4 × M2 | — | `oled_*` |

The panel is 3 mm thick, which suits most panel-mount switches (they usually take 1–6 mm).
Labels are engraved 0.6 mm deep. Fill them with paint or a paint pen for contrast.

The OLED cable runs from the pod to the TCU box. Keep it within about 1–1.5 m (I²C isn't
designed for long cables). If the box is further away, set `with_oled = false` and mount the
screen in the dash instead (spec §3.7).

### Mounting the sequential shifter on the deck

The flat top deck has the Beavis Motorsport shifter's mounting pattern: 4 × M6, 116 × 74 mm,
with hex-nut traps underneath and a 26 × 14 mm slot for its switch wires.

1. Print the shifter parts from Thingiverse
   [4557771](https://www.thingiverse.com/thing:4557771) (the Beavis guide:
   [beavismotorsport.com/sim-racing/diy-sequential-shifter](https://www.beavismotorsport.com/sim-racing/diy-sequential-shifter/)).
   The guide says PLA, but **in a car print them in PETG or ASA too**, with the same 4 walls
   and 30–40 % infill.
2. Build the lever as the guide describes: 12 mm pivot tube (or an M12 bolt), M3 × 70 bolts,
   M12 ball plungers, springs and an M10 × 1.5 knob.
3. Use only the two **V-155-1C25 roller limit switches**. **You don't need the Arduino Pro
   Micro**: the TCU reads the switches directly.
   - Upshift switch: COM → console C7 (ground), NO → C1 ([+]).
   - Downshift switch: COM → C7, NO → C2 ([−]).
   - Run the wires down through the deck slot and join them to the console cable inside the pod
     (a small lever-nut or terminal strip works).
   - Rally style (pull = upshift) or push = upshift is `SWAP_SHIFT_INPUTS` in `config.h`.
     You don't need to rewire.
4. Drop four M6 nuts into the traps under the deck **before** you close the pod. Bolt the
   shifter down from the top.

**Rigid mounting matters.** Every shift pushes on the lever. Bolt the bottom plate to the
console or to a steel bracket through its four slots, using large washers. Do that first, then
screw the shell onto the plate. Check that the lever can't touch the factory gear lever, the
handbrake, your knee or the dash at either end of its travel.

### Assembly order

1. Fit the four heat-set inserts in the corner bosses.
2. Fit the switches, the TEST button, the LED bezel and the OLED (M2 screws) to the front panel.
3. Drop the M6 nuts into the deck traps.
4. Fit the two PG7 glands in the rear wall: the console cable (X4) and the OLED cable (X5).
5. Wire each control to its core: [wiring guide §3](../docs/03-wiring-guide.md), X4 C1–C8.
   Every switch's other side goes to C7.
6. Bolt the bottom plate to the console, then screw the shell onto it (4 × M3 countersunk,
   from underneath).
7. Bolt the shifter to the deck.

## TCU enclosure

### What goes where (inside, looking down with the gland wall towards you)

| Area | Part | Notes |
|---|---|---|
| Back left | **Arduino Mega 2560** on 6 standoffs (official hole pattern) | USB-B lines up with the cut-out in the left wall, for flashing and the USB log |
| Right | **Main board**, 80 × 120 mm perfboard on 4 standoffs (holes 4 mm in from each corner) | PROFETs, relays, ULN2803, optos, LM393, screw terminals. Relays and PROFETs go towards the right-wall vents |
| Front left | **Buck converter** in a cradle with zip-tie slots (`buck_size`) | |
| Front centre | **MPU-6050 gyro (GY-521)** on 2 standoffs | Chip up. "GYRO: CHIP UP, FLAT" is engraved on the floor beside it. **Check `gyro_hole_spacing` against your module** |
| Front wall | 7 glands, engraved **PWR SOL SIG LOAD CON OLED ATF** | Fit the inline connectors (X1–X6) on the cable tails outside the box |

| Gland | Connector / cable | Gland |
|---|---|---|
| PWR | X1: +12 V drive, +12 V logic, ground | PG9 |
| SOL | X2: S1/S2/SL from the ECM and to the solenoids (6 cores) | PG9 |
| SIG | X3: D, STP, E1, SPD, TAC, VTA, E2 | PG9 |
| LOAD | Dummy-load heatsink: 3 feeds + ground, 18 AWG silicone wire | PG9 |
| CON | X4: console pod, 8-core shielded | PG7 |
| OLED | X5: screen, 4-core shielded | PG7 |
| ATF | X6: ATF probe, 3-core shielded | PG7 |

The box is 212 × 148 × 48 mm (51 mm with the lid).

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

- **Mount the box flat, on its floor**, so the gyro's chip lies level. Check with a phone's
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

1. Fit the four heat-set inserts in the corner bosses.
2. Fit the glands.
3. Screw down the gyro, the Mega and the main board, and zip-tie the buck converter into its
   cradle.
4. Wire the inside, then feed each cable through its gland and tighten the gland nut.
5. Screw the lid on (4 × M3 countersunk).
