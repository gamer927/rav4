#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""Shopping list with cost estimates.

Writes docs/04-shopping-list.md and docs/shopping-list.csv from the table
below, so the totals always add up. Prices are rough US estimates (late
2026) for what you actually have to buy (often a pack), not unit prices.

  python3 tools/shopping_list.py
"""
import csv
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

# Stores: one order each keeps shipping down.
DK = "DigiKey / Mouser"
AMZ = "Amazon (or AliExpress, cheaper but slower)"
HW = "Hardware store"
FAB = "Board maker (JLCPCB, PCBWay, ...)"
AUTO = "Auto-parts store"

# (section, item, need, buy, store, est. US$, notes)
ITEMS = [
    # ---------------------------------------------------------------- 1
    ("1. Brain and power", "Arduino Mega 2560 R3", "1", "1 (Elegoo or similar clone with the 16U2 USB chip)", AMZ, 22,
     "Genuine Arduino is ~$48. Comes with a USB cable"),
    ("1. Brain and power", "The TCU circuit board (upload hardware/pcb/rav4-tcu-gerbers.zip)", "1",
     "5 (the usual minimum)", FAB, 30,
     "2 layers, 1.6 mm, 1 oz copper, lead-free HASL. Bare board: you solder the parts. See hardware/pcb/README.md"),
    ("1. Brain and power", "RECOM R-78HB5.0-0.5 regulator, 5 V 0.5 A, 9–72 V in", "1", "1", DK, 8,
     "The 5 V supply for the Mega. Fixed 5 V: nothing to adjust"),
    ("1. Brain and power", "47 µF 16 V electrolytic capacitor (radial, 6.3 mm, 2.5 mm legs)", "1", "pack of 10", DK, 3,
     "On the regulator's output"),
    ("1. Brain and power", "1.5KE27A TVS diode", "2", "pack of 10", DK, 4, "Littelfuse 1.5KE27A"),
    ("1. Brain and power", "P6KE6.8A TVS diode", "1", "pack of 5", DK, 3,
     "Across the 5 V rail: if the regulator fails shorted, it blows the 2 A fuse"),
    ("1. Brain and power", "Resettable fuse (PTC), 0.25 A hold, radial, 5.1 mm leg spacing", "1", "pack of 5", DK, 3,
     "e.g. Bourns MF-R025. Protects the relay-power wires to the ON/OFF switch"),
    ("1. Brain and power", "1N5822 Schottky diode, 3 A", "1", "pack of 10", DK, 3, "Reverse-polarity protection"),
    ("1. Brain and power", "100 µF 50 V electrolytic capacitor, 8 mm wide, 3.5 mm leg spacing", "1", "pack of 10", DK, 3,
     "e.g. Panasonic EEU-FR1H101. Up to 10 mm wide still fits"),
    ("1. Brain and power", "Inline blade-fuse holders (16 AWG) + 5 A and 2 A fuses", "2 + fuses", "kit", AUTO, 10,
     "Match your fuse box's blade size (step 0 checks it)"),
    ("1. Brain and power", "Add-a-circuit fuse tap", "1", "1", AUTO, 8, "Same blade size as the fuse box"),
    # ---------------------------------------------------------------- 2
    ("2. Solenoid control", "Omron G2RL-2-DC12 relay (DPDT, 12 V)", "3", "4 (one spare)", DK, 10,
     "The factory/box changeover. Don't substitute a single-pole relay"),
    ("2. Solenoid control", "IRF4905 P-channel MOSFET, TO-220", "3", "pack of 5", DK, 9,
     "Infineon IRF4905PBF. They lie flat, bolted to the board's copper (M3 × 8 screw and nut each)"),
    ("2. Solenoid control", "ACS712-05B current sensor module (5 A)", "3", "pack of 3–5", AMZ, 11,
     "Measures each solenoid's current for the self-test. Must be the **5 A** version, "
     "about 31 × 13 mm (the board fits up to 33 × 15): a 2-way screw terminal and 3 pins (VCC, OUT, GND)"),
    ("2. Solenoid control", "ULN2803A driver, DIP-18", "1", "2 (one spare)", DK, 3,
     "Through-hole DIP: e.g. Toshiba ULN2803APG. Not the SOIC version"),
    ("2. Solenoid control", "1 W zener diode kit (1N4728A–1N4764A)", "10", "kit", AMZ, 11,
     "Uses 1N4733A 5.1 V ×4, 1N4744A 15 V ×3, 1N4749A 24 V ×3"),
    ("2. Solenoid control", "UF4007 fast diode", "4", "pack of 10", DK, 3, "1N4007 also works"),
    ("2. Solenoid control", "15 Ω 50 W aluminium-housed resistor", "3", "3", DK, 15,
     "Dummy loads, e.g. TE HSA5015RJ or Arcol HS50 15R"),
    ("2. Solenoid control", "Finned aluminium heatsink, ≥ 100 × 100 × 25 mm", "1", "1", AMZ, 14,
     "For the dummy resistors, outside the box"),
    ("2. Solenoid control", "Thermal paste", "1", "small tube", AMZ, 6, "Skip if you have some"),
    # ---------------------------------------------------------------- 3
    ("3. Signal inputs", "PC817 optocoupler, DIP-4", "6", "pack of 20", AMZ, 6, ""),
    ("3. Signal inputs", "LM393 comparator, DIP-8", "1", "pack of 10", AMZ, 5, ""),
    ("3. Signal inputs", "DIP sockets, 8-pin and 18-pin (0.3\" wide)", "1 + 1", "assorted pack", AMZ, 6,
     "For the LM393 and the ULN2803A: no soldering iron on the chips, and easy to swap"),
    ("3. Signal inputs", "BAT85 Schottky diode (DO-35)", "18", "pack of 25", DK, 5, "Input clamps"),
    ("3. Signal inputs", "1N4148 diode", "6", "pack of 100", AMZ, 5, ""),
    ("3. Signal inputs", "Resistor kit, 1/4 W 1 % metal film (600+ pcs), bodies about 6 mm long", "≈ 50", "kit", AMZ, 12,
     "Needs 100, 1k, 1.8k, 2.2k, 3.3k, 4.7k, 10k, 22k, 47k and 1M"),
    ("3. Signal inputs", "Extra 10 kΩ resistors", "20 total", "pack of 50", AMZ, 4,
     "Most kits only have 10–20 of each value"),
    ("3. Signal inputs", "100 nF ceramic capacitors, 50 V, 5 mm leg spacing", "12", "pack of 50", AMZ, 5, ""),
    # ---------------------------------------------------------------- 4
    ("4. Board connectors and wire", "Screw terminals, 3.5 mm pitch, 2- and 3-way, interlocking (DG350 / KF350 type)",
     "25 positions", "pack of ~20 pieces", AMZ, 8,
     "SHIFT 3, DASH 8, SCREEN 4, ATF 3, SIG 7: e.g. 8 = 3 + 3 + 2. Or Phoenix PT 1,5/n-3,5-H in one piece"),
    ("4. Board connectors and wire", "Screw terminals, 5.08 mm pitch, 2- and 3-way, interlocking (KF301 / DG301 type)",
     "13 positions", "pack of ~20 pieces", AMZ, 7,
     "PWR 3, SOL 6, LOAD 4. Or Phoenix MKDS 1,5/n-5,08 in one piece"),
    ("4. Board connectors and wire", "Male pin header strips, 2.54 mm, 40-pin, breakable", "3", "pack of 10", AMZ, 5,
     "The Mega's pins through the board (86) and the gyro (8)"),
    ("4. Board connectors and wire", "Silicone hook-up wire 22 AWG, 6 colours (the book calls it \"thin wire\")", "~2 m", "kit", AMZ, 15,
     "Bench test wires and the dash pod's inside wiring"),
    ("4. Board connectors and wire", "Silicone wire 18 AWG, 6 colours (the book calls it \"thick wire\")", "~2 m", "kit", AMZ, 16,
     "The dummy-load cable (heat-proof)"),
    # ---------------------------------------------------------------- 5
    ("5. Car harness and connectors", "Deutsch DT connector kit, 2–12 way, with crimper", "X1–X6 + bypass plug",
     "kit (~200 pcs)", AMZ, 40, "Needs 3-, 4-, 6- and 8-way pairs"),
    ("5. Car harness and connectors", "16 AWG automotive wire, red and black", "~4 m", "25 ft each", AUTO, 15, "W1–W3"),
    ("5. Car harness and connectors", "18 AWG automotive wire, multi-colour kit", "~15 m", "kit", AMZ, 20,
     "Solenoid wires W4–W9 and taps"),
    ("5. Car harness and connectors", "Shielded cable: 8-core 24 AWG, 4-core, 3-core", "3 m + 2.5 m + 4.5 m",
     "5 m of each", AMZ, 26, "Dash pod switches (8-core), screen (4-core, ≤ 2.5 m), ATF probe and shifter (3-core)"),
    ("5. Car harness and connectors", "Heat-shrink assortment incl. adhesive-lined", "1", "kit", AMZ, 12, ""),
    ("5. Car harness and connectors", "Split loom + zip ties", "1", "pack", AUTO, 12, ""),
    ("5. Car harness and connectors", "Wago 221 lever-nut connectors, assorted", "~9", "pack", HW, 12,
     "Joins the cables to the switch wires inside the dash pod and the shifter base"),
    ("5. Car harness and connectors", "Cable glands PG7 + PG9, nylon, with locknuts", "7 + 4", "assortment", AMZ, 10,
     "TCU box 4 × PG7 + 4 × PG9; dash pod 2 × PG7; shifter base 1 × PG7"),
    # ---------------------------------------------------------------- 6
    ("6. Dash pod controls and screen", "12 mm ON/OFF toggle, **2-pole** (DPDT or DPST), with flip-up safety cover", "1", "1", AMZ, 12,
     "Must be 2-pole: one pole tells the TCU, the other cuts the relays' power in hardware"),
    ("6. Dash pod controls and screen", "6 mm mini toggle switch, MTS-101/102", "3", "pack of 5", AMZ, 8,
     "ECO, SPORT and MANUAL"),
    ("6. Dash pod controls and screen", "12 mm momentary push button, normally open", "1",
     "pack of 5", AMZ, 10, "TEST"),
    ("6. Dash pod controls and screen", "2.42\" OLED module, 128 × 64, SSD1309, I²C-capable", "1", "1", AMZ, 20,
     "e.g. Waveshare 2.42inch OLED Module. 55 × 27.5 mm screen, readable from the driver's seat"),
    ("6. Dash pod controls and screen", "1 µF capacitor (ceramic or electrolytic, 16 V+)", "1", "pack", AMZ, 4,
     "Power-on reset for the screen, with a 10k from the resistor kit"),
    ("6. Dash pod controls and screen", "GY-521 (MPU-6050) gyro module", "1", "pack of 3", AMZ, 9, ""),
    # ---------------------------------------------------------------- 7
    ("7. ATF temperature probe", "DS18B20 stainless probe, 6 mm, PTFE or silicone cable (125 °C)", "1", "1", AMZ, 10,
     "Not the PVC-cable kind"),
    ("7. ATF temperature probe", "Brass barbed tee with a 1/8\" NPT female side port", "1", "1", HW, 10,
     "Barb size = your cooler hose's inside diameter (step 0 measures it)"),
    ("7. ATF temperature probe", "1/8\" NPT male × 6 mm compression fitting, brass", "1", "1", HW, 7,
     "Seals around the probe's 6 mm tube"),
    ("7. ATF temperature probe", "Fuel-injection hose clamps", "2", "2", AUTO, 8, ""),
    ("7. ATF temperature probe", "ATF-safe thread sealant (PTFE paste)", "1", "1", HW, 7, ""),
    ("7. ATF temperature probe", "Firewall grommet assortment", "1", "kit", AUTO, 7, ""),
    ("7. ATF temperature probe", "ATF to top up (Toyota Type T-IV, or as your owner's manual says)", "~1 qt",
     "1 qt", AUTO, 10, ""),
    # ---------------------------------------------------------------- 8
    ("8. 3D printing", "PETG filament, 1 kg", "~1 kg", "2 rolls (don't run out mid-print)", AMZ, 36,
     "Skip if filament comes with the printer. Not PLA"),
    ("8. 3D printing", "M3 heat-set inserts (~4 mm OD)", "12", "pack of 50", AMZ, 8, ""),
    ("8. 3D printing", "M3 screw kit with countersunk screws and nuts", "~35", "kit", AMZ, 12,
     "8 and 10 mm countersunk; 6 mm pan head (Mega and board posts); 8 mm pan head + nuts (MOSFET tabs)"),
    ("8. 3D printing", "M6 bolts + nuts (shifter); M5 bolts + large washers (mounting); M5 × 25 bolt + nyloc nut (pod hinge)",
     "4 + 8 + 1", "loose", HW, 9, ""),
    # ---------------------------------------------------------------- 9
    ("9. Sequential shifter (Beavis Motorsport DIY)", "V-155-1C25 roller-lever microswitch", "2", "pack of 5", AMZ, 9,
     "Genuine Omron is $4–6 each"),
    ("9. Sequential shifter (Beavis Motorsport DIY)", "Pivot: 12 mm OD tube, 64 mm long, 1.6 mm wall; or an M12 × 75 bolt + nyloc nut + 2 washers", "1",
     "1", HW, 6, "The tube needs the printed pivot pin and cap"),
    ("9. Sequential shifter (Beavis Motorsport DIY)", "M3 × 70 hex-head bolts + washers + nyloc nuts", "4 each", "pack", HW, 5,
     "Hold the two switches and their spacers"),
    ("9. Sequential shifter (Beavis Motorsport DIY)", "M10 × 1.5 bolts: 2 × 30–35 mm (travel stops) + 1 × ~65 mm (cut into the knob stud)",
     "3", "loose", HW, 5, "Cut the head off the long one: 60–70 mm of thread"),
    ("9. Sequential shifter (Beavis Motorsport DIY)", "M12 × 1.75 spring ball plungers (Kipp K0309 type)", "2", "2", AMZ, 14,
     "Optional: the detent 'click'"),
    ("9. Sequential shifter (Beavis Motorsport DIY)", "Compression springs, 1.8 mm wire, 14 mm OD, 40 mm long", "2", "pack", AMZ, 8,
     "Centre the lever; they sit over the travel-stop bolts"),
    ("9. Sequential shifter (Beavis Motorsport DIY)", "Shift knob, M10 × 1.5 thread", "1", "1", AMZ, 12,
     "Or print Knob.stl from hardware/beavis_shifter/"),
]

# Tools you may already own. The 3D printer and jack stands are yours already.
TOOLS = [
    ("Factory repair manual + wiring diagram for your VIN", "Toyota TIS 2-day pass, or a used paper set", 25,
     "$25 (TIS) to $60 (paper)"),
    ("Multimeter with continuity beep", "", 25, ""),
    ("Temperature-controlled soldering iron, solder, flux", "e.g. Pinecil kit", 45, ""),
    ("Wire stripper", "", 12, ""),
    ("Flush cutters", "Trim every leg short under the board", 8, ""),
    ("Heat gun", "", 20, "For heat-shrink; a lighter is a poor substitute"),
    ("12 V 5 A DC power adapter + screw-terminal barrel jack", "Bench power for testing", 15, ""),
    ("OBD-II scanner", "1999 is OBD-II", 20, ""),
    ("IR thermometer", "", 15, ""),
    ("Drain pan + funnel", "", 10, ""),
    ("Hot-glue gun", "", 10, ""),
    ("Drill + 3 mm and 6.5 mm bits", "6.5 mm opens the shifter's M6 holes", 0, "Usually owned"),
    ("3D printer", "You have one", 0, "Owned"),
    ("Four jack stands", "You have them", 0, "Owned"),
]

OPTIONAL = [
    ("Genuine Arduino Mega instead of a clone", 26),
    ("3M VHB tape, to stick the dash-pod mount down instead of screwing it", 8),
    ("USB-B panel-mount extension, 50 cm: plug the laptop in from the glovebox", 8),
    ("Cheap logic analyser (checks speed and tach signals)", 12),
]


def money(x):
    return f"${x:,.0f}"


def main():
    sections = []
    for s, *_ in ITEMS:
        if s not in sections:
            sections.append(s)
    tot = {s: sum(i[5] for i in ITEMS if i[0] == s) for s in sections}
    parts_total = sum(tot.values())
    shifter = tot["9. Sequential shifter (Beavis Motorsport DIY)"]
    filament = 36
    tools_total = sum(t[2] for t in TOOLS)

    md = []
    md.append("# Shopping list (with cost estimates)")
    md.append("")
    md.append("<!-- Generated by tools/shopping_list.py: edit the table there, then re-run it. -->")
    md.append("")
    md.append("Everything for the 1999 RAV4 transmission controller, including the 3D-printed housings "
              "and the sequential shifter. Quantities match the circuit board and the "
              "[build book](05-build-book.md). A spreadsheet version is in "
              "[`shopping-list.csv`](shopping-list.csv).")
    md.append("")
    md.append("> **Prices are estimates** (US, late 2026), for what you actually have to buy, which "
              "is often a pack. Shop around: AliExpress is often half the Amazon price but takes weeks. "
              "Your 3D printer and jack stands aren't counted.")
    md.append("")
    md.append("## Totals")
    md.append("")
    md.append("| | Estimate |")
    md.append("|---|---|")
    for s in sections:
        md.append(f"| {s} | {money(tot[s])} |")
    md.append(f"| **All parts, including the shifter** | **{money(parts_total)}** |")
    md.append(f"| Without the shifter (no manual shifting) | {money(parts_total - shifter)} |")
    md.append(f"| Without filament (if your printer comes with it) | {money(parts_total - filament)} |")
    md.append(f"| Tools, if you own none of them | {money(tools_total)} |")
    md.append("")
    md.append("Expect roughly **±25 %** depending on where you shop. Many packs leave spares, which is "
              "useful: you'll want a spare relay and MOSFET if something goes wrong. The circuit board "
              "comes in fives: the spares are there if a first attempt goes badly.")
    md.append("")
    md.append("## Before you buy: measure these on the car first")
    md.append("")
    md.append("Build book **step 0** (one evening, nothing cut) settles these, so you order the right "
              "sizes once:")
    md.append("")
    md.append("1. **Fuse blade size** in the cabin fuse box (standard ATO/ATC or mini): for the fuse "
              "tap and inline fuse holders.")
    md.append("2. **ATF cooler hose inside diameter** (usually 5/16\" or 3/8\"): for the barbed tee.")
    md.append("3. **A spot for the TCU box**, ideally under the passenger side of the dash where you "
              "can reach it: 216 × 212 × 51 mm (244 mm wide with its mounting ears), bolted to metal.")
    md.append("4. **A spot for the dash pod**: 200 × 64 × 64 mm, plus its hinge mount, on the dash top "
              "(driver's side, never over the passenger airbag).")
    md.append("5. **Space on the centre console** beside the gear lever for the shifter base: "
              "150 × 100 mm, long side front-to-back. With the shifter on it, it stands about 35 cm "
              "tall: check it clears the dash and the radio.")
    md.append("")
    md.append("## Where to order")
    md.append("")
    md.append("Batching keeps shipping down: one **board maker** order for the circuit board (do it first: it takes 1–2 weeks), one **DigiKey or Mouser** order for the exact electronic "
              "parts, one **Amazon/AliExpress** order for modules and kits, then a trip to the "
              "**hardware store** and the **auto-parts store**.")
    md.append("")
    for s in sections:
        md.append(f"## {s} — {money(tot[s])}")
        md.append("")
        md.append("| ✓ | Item | Need | Buy | Where | Est. | Notes |")
        md.append("|---|---|---|---|---|---|---|")
        for sec, item, need, buy, store, price, notes in ITEMS:
            if sec == s:
                md.append(f"| ☐ | {item} | {need} | {buy} | {store} | {money(price)} | {notes} |")
        md.append("")
    md.append(f"## Tools — up to {money(tools_total)} if you own none")
    md.append("")
    md.append("| ✓ | Tool | Notes | Est. |")
    md.append("|---|---|---|---|")
    for name, note, price, extra in TOOLS:
        p = "owned" if extra == "Owned" else money(price) if price else "—"
        md.append(f"| ☐ | {name} | {note}{'; ' + extra if extra and extra != 'Owned' else ''} | {p} |")
    md.append("")
    md.append("## Optional extras")
    md.append("")
    for name, price in OPTIONAL:
        md.append(f"- {name}{': +' + money(price) if price else ' (already in the button pack above)'}")
    md.append("")
    md.append("## By store (for ordering)")
    md.append("")
    for store in (FAB, DK, AMZ, HW, AUTO):
        items = [i for i in ITEMS if i[4] == store]
        md.append(f"**{store}** — {len(items)} items, about {money(sum(i[5] for i in items))}: "
                  + "; ".join(i[1] for i in items) + ".")
        md.append("")
    md += ["", "---", "", "*RAV4 TCU by gamer927 · [CC BY-NC-SA 4.0](../LICENSE): free to build, not to sell.*", ""]
    with open(os.path.join(ROOT, "docs", "04-shopping-list.md"), "w") as f:
        f.write("\n".join(md))
    with open(os.path.join(ROOT, "docs", "shopping-list.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Section", "Item", "Need", "Buy", "Where", "Est. US$", "Notes"])
        for row in ITEMS:
            w.writerow(row)
        for name, note, price, extra in TOOLS:
            w.writerow(["Tools", name, "", "", "", price, (note + " " + extra).strip()])
    print(f"parts {money(parts_total)} (shifter {money(shifter)}), tools {money(tools_total)}")


if __name__ == "__main__":
    main()
