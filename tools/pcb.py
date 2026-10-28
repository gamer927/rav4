#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""The TCU's circuit board: one board that plugs onto the Arduino Mega.

The circuit comes from tools/boards.py (the tested design: every part and
every connection). This script turns it into a real printed circuit board:

  * the netlist: boards.py's wires joined into nets, the two ribbon headers
    replaced by the Mega's own pins (the board is a shield), the 5 V buck
    module replaced by a fixed 5 V regulator (RECOM R-78HB5.0-0.5, 9-72 V in)
  * the placement: every part's position, in functional blocks, with the
    screw terminals along the front edge in the order of the box's glands
  * the routing: tools/pcb_router.py (a grid router), then KiCad's own
    design-rule check (DRC) must report no errors and no unconnected pads
  * the outputs in hardware/pcb/: the KiCad board, the Gerber/drill files to
    upload to a board maker (zip), the parts list, pictures for the build
    book, and hardware/pcb_envelope.scad for the box's fit check

  python3 tools/pcb.py            # everything
  python3 tools/pcb.py --place    # placement and pictures only (no routing)
"""
import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import boards  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "hardware", "pcb")
LIB = os.path.join(OUT, "rav4tcu.pretty")
KLIB = "/usr/share/kicad/footprints"

W, H = 200.0, 150.0      # board size, mm
X0, Y0 = 50.0, 50.0      # where the board sits on KiCad's page
NOTCH = (17.78, 95.25)   # back-left cut-out over the Mega's USB and power jacks: x < 17.78, y > 95.25
MEGA_AT = (2.54, 95.25)  # the Mega's corner (its USB side, front edge) under the board
STANDOFF_H = 16.1        # floor to the board's underside: 6 mm Mega standoff + 1.6 + 8.5 header

COPYRIGHT = "RAV4 TCU  github.com/gamer927/rav4  CC BY-NC-SA 4.0"


def kxy(x, y):
    """Board coordinates (mm, origin front-left, y towards the back) -> KiCad nm."""
    return int(round((X0 + x) * 1e6)), int(round((Y0 + H - y) * 1e6))


def bxy(kx, ky):
    return kx / 1e6 - X0, H - (ky / 1e6 - Y0)


# ---------------------------------------------------------------- footprints
# The Arduino Mega 2560 R3's headers, in mm from the board's USB-side front
# corner (y towards the back). Cross-checked: every pin it shares with the
# UNO R3 lands exactly on KiCad's own Arduino_UNO_R3 footprint.
def mega_pins():
    pins = []
    for i, n in enumerate(["IOREF", "RST", "3V3", "5V", "GND", "GND", "VIN"]):
        pins.append((n, 30.48 + 2.54 * i, 2.54))
    for i in range(8):
        pins.append((f"A{i}", 50.8 + 2.54 * i, 2.54))
        pins.append((f"A{8 + i}", 73.66 + 2.54 * i, 2.54))
    top = ["SCL", "SDA", "AREF", "GND", "D13", "D12", "D11", "D10", "D9", "D8"]
    for i, n in enumerate(top):
        pins.append((n, 18.8 + 2.54 * i, 50.8))
    for i, n in enumerate(["D7", "D6", "D5", "D4", "D3", "D2", "D1", "D0"]):
        pins.append((n, 45.72 + 2.54 * i, 50.8))
    for i in range(8):
        pins.append((f"D{14 + i}", 68.58 + 2.54 * i, 50.8))
    pins += [("5V", 93.98, 50.8), ("5V", 96.52, 50.8), ("GND", 93.98, 7.62), ("GND", 96.52, 7.62)]
    for i in range(16):  # D22/D23 at the top (y 48.26) down to D52/D53 (y 10.16)
        pins.append((f"D{22 + 2 * i}", 93.98, 48.26 - 2.54 * i))
        pins.append((f"D{23 + 2 * i}", 96.52, 48.26 - 2.54 * i))
    return pins


MEGA_HOLES = [(13.97, 2.54), (15.24, 50.8), (66.04, 7.62), (66.04, 35.56), (90.17, 50.8), (96.52, 2.54)]


def fp_text(kind, text, x, y, layer, size=1.0, hide=False):
    h = " hide" if hide else ""
    return (f'  (fp_text {kind} "{text}" (at {x:.3f} {y:.3f}) (layer "{layer}"){h}\n'
            f'    (effects (font (size {size} {size}) (thickness {size * 0.15:.3f}))))\n')


def fp_rect(x0, y0, x1, y1, layer, w=0.12):
    s = ""
    for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        s += (f'  (fp_line (start {a[0]:.3f} {a[1]:.3f}) (end {b[0]:.3f} {b[1]:.3f}) '
              f'(stroke (width {w}) (type solid)) (layer "{layer}"))\n')
    return s


def pad(num, x, y, size=1.7, drill=1.0, shape="circle"):
    return (f'  (pad "{num}" thru_hole {shape} (at {x:.3f} {y:.3f}) (size {size} {size}) (drill {drill}) '
            f'(layers "*.Cu" "*.Mask"))\n')


def write_lib():
    """Our own footprints: the Mega shield headers and the two plug-in modules."""
    os.makedirs(LIB, exist_ok=True)
    # Mega shield: footprint origin = the Mega's corner; KiCad y is down, so y -> -y.
    s = '(footprint "Arduino_Mega2560_Shield" (version 20221018) (generator rav4tcu) (layer "F.Cu")\n'
    s += '  (attr through_hole)\n'
    s += fp_text("reference", "A1", 50, -26, "F.Fab", hide=True)
    s += fp_text("value", "Arduino_Mega2560_Shield", 50, -24, "F.Fab", hide=True)
    for n, x, y in mega_pins():
        s += pad(n, x, -y, shape="rect" if n == "IOREF" else "circle")
    # It sits under the board: its outline is on the bottom courtyard layer.
    s += fp_rect(-0.25, 0.25, 101.85, -53.59, "B.CrtYd", 0.05)
    s += fp_rect(0, 0, 101.6, -53.34, "B.Fab", 0.1)
    s += ")\n"
    open(os.path.join(LIB, "Arduino_Mega2560_Shield.kicad_mod"), "w").write(s)

    # ACS712 current-sensor module (31 x 13 mm): its 3 pins in a row at one end,
    # and two big holes beyond its screw-terminal end for the two short links
    # that carry the solenoid current through it.
    s = '(footprint "ACS712_Module" (version 20221018) (generator rav4tcu) (layer "F.Cu")\n'
    s += '  (attr through_hole)\n'
    s += fp_text("reference", "CS?", 0, -3.5, "F.SilkS", 1.0)
    s += fp_text("value", "ACS712-05B module", 0, 16, "F.Fab")
    for i, n in enumerate(["VCC", "OUT", "GND"]):
        s += pad(n, -2.54 + 2.54 * i, 0, shape="rect" if n == "VCC" else "circle")
        s += fp_text("user", n, -2.54 + 2.54 * i, -1.9 if i != 1 else 2.1, "F.SilkS", 0.8)
    s += pad("IP+", -2.54, 31.5, 3.2, 1.8)
    s += pad("IP-", 2.54, 31.5, 3.2, 1.8)
    s += fp_text("user", "LINK", 0, 34.5, "F.SilkS", 0.8)
    s += fp_rect(-6.5, -1.2, 6.5, 29.8, "F.SilkS")
    s += fp_rect(-6.75, -2.5, 6.75, 33.6, "F.CrtYd", 0.05)
    s += ")\n"
    open(os.path.join(LIB, "ACS712_Module.kicad_mod"), "w").write(s)

    # GY-521 (MPU-6050) gyro: 8-pin header along one edge, board lying flat.
    s = '(footprint "GY-521_Module" (version 20221018) (generator rav4tcu) (layer "F.Cu")\n'
    s += '  (attr through_hole)\n'
    s += fp_text("reference", "GYRO", 8.9, -17, "F.SilkS", 1.0)
    s += fp_text("value", "GY-521", 8.9, 3, "F.Fab")
    for i, n in enumerate(["VCC", "GND", "SCL", "SDA", "XDA", "XCL", "AD0", "INT"]):
        s += pad(n, 2.54 * i, 0, shape="rect" if i == 0 else "circle")
        s += fp_text("user", n[:3], 2.54 * i, 2.1 if i % 2 else 3.2, "F.SilkS", 0.8)
    s += fp_rect(-1.5, 1.2, 19.3, -14.8, "F.SilkS")
    s += fp_rect(-1.75, 3.2, 19.55, -15.05, "F.CrtYd", 0.05)
    s += ")\n"
    open(os.path.join(LIB, "GY-521_Module.kicad_mod"), "w").write(s)


# ---------------------------------------------------------------- the circuit
# Screw-terminal groups along the front edge, left to right, in the order of
# the box's glands: (gland, ref, positions [old perfboard terminal refs], pitch)
TERMINALS = [
    ("SHIFT", "X7", ["X7a", "X7b", "X7c"], 3.5),
    ("DASH", "X4", ["X4a", "X4b", "X4c", "X4d", "X4e", "X4f", "J5a", "J5b"], 3.5),
    ("SCREEN", "X5", ["X5a", "X5b", "X5c", "X5d"], 3.5),
    ("ATF", "X6", ["X6a", "X6b", "X6c"], 3.5),
    ("SIG", "J3", ["J3a", "J3b", "J3c", "J3d", "J3e", "J3f", "J3g"], 3.5),
    ("PWR", "J1", ["J1a", "J1b", "J1c"], 5.08),
    ("SOL", "J2", ["J2a", "J2b", "J2c", "J2d", "J2e", "J2f"], 5.08),
    ("LOAD", "J4", ["J4a", "J4b", "J4c", "J4d"], 5.08),
]
TERM_GAPS = [5.0, 3.0, 9.5, 7.0, 9.0, 3.5, 2.0]   # between the groups above, mm
TERM_LABEL = {}   # filled in by circuit(): ref -> list of short labels per position


def term_footprint(n, pitch):
    if pitch == 3.5:
        return "TerminalBlock_Phoenix", f"TerminalBlock_Phoenix_PT-1,5-{n}-3.5-H_1x{n:02d}_P3.50mm_Horizontal"
    return "TerminalBlock_Phoenix", f"TerminalBlock_Phoenix_MKDS-1,5-{n}-5.08_1x{n:02d}_P5.08mm_Horizontal"


def part_footprint(p):
    """KiCad footprint (library, name) and pin-name map for a boards.py part."""
    k, v = p.fp["kind"], p.value
    if k == "R":
        return ("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal"), {}
    if k == "C":
        return ("Capacitor_THT", "C_Disc_D5.0mm_W2.5mm_P5.00mm"), {}
    if k == "CE":
        return ("Capacitor_THT", "CP_Radial_D8.0mm_P3.50mm"), {"+": "1", "-": "2"}
    if k == "PTC":
        return ("Capacitor_THT", "C_Disc_D7.5mm_W5.0mm_P5.00mm"), {}
    if k == "D":
        span = max(abs(dc) for _, dc, _ in p.fp["pins"])
        name = {3: "D_DO-35_SOD27_P7.62mm_Horizontal", 4: "D_DO-41_SOD81_P10.16mm_Horizontal",
                6: "D_DO-201AD_P15.24mm_Horizontal"}[span]
        if v.startswith("P6KE"):
            name = "D_DO-15_P10.16mm_Horizontal"
        return ("Diode_THT", name), {"K": "1", "A": "2"}
    if k == "IC":
        return ("Package_DIP", f"DIP-{len(p.fp['pins'])}_W7.62mm"), {}
    if k == "K":
        return ("Relay_THT", "Relay_DPDT_Omron_G2RL-2"), {
            "COIL1": "A1", "COIL2": "A2", "COM_A": "11", "NC_A": "12", "NO_A": "14",
            "COM_B": "21", "NC_B": "22", "NO_B": "24"}
    if k == "Q":
        return ("Package_TO_SOT_THT", "TO-220-3_Horizontal_TabDown"), {"G": "1", "D": "2", "S": "3"}
    if k == "MOD" and p.ref.startswith("CS"):
        return ("rav4tcu", "ACS712_Module"), {}
    if k == "MOD" and p.ref == "GYRO":
        return ("rav4tcu", "GY-521_Module"), {}
    raise ValueError(f"no footprint for {p.ref} ({k})")


def hub_pin(table, n):
    for p, _, mega in table:
        if p == n:
            return None if mega == "not connected" else ("MEGA", mega.split()[0])
    raise KeyError(n)


class Circuit:
    """Parts and nets for the board: {ref: dict}, [{name, pins: [(ref, pin)]}]."""

    def __init__(self):
        pw, lg = boards.power_board(), boards.logic_board()
        self.parts = {}
        term_of = {}
        for gland, ref, olds, pitch in TERMINALS:
            lib, name = term_footprint(len(olds), pitch)
            self.parts[ref] = dict(ref=ref, lib=lib, fp=name, value=gland, kind="TB", step=None, note="")
            for i, o in enumerate(olds):
                term_of[o] = (ref, str(i + 1))
        self.parts["A1"] = dict(ref="A1", lib="rav4tcu", fp="Arduino_Mega2560_Shield", value="Arduino Mega",
                                kind="MEGA", step=None, note="")
        labels = {}
        for b in (pw, lg):
            for p in b.parts:
                if p.ref in ("JA", "JB", "BUCK") or p.ref in term_of:
                    if p.ref in term_of:
                        labels[p.ref] = p.value
                    continue
                ref = self.rename(b.name, p.ref)
                (lib, name), pmap = part_footprint(p)
                self.parts[ref] = dict(ref=ref, lib=lib, fp=name, value=p.value, kind=p.fp["kind"],
                                       step=p.step, note=p.note, pmap=pmap)
        for gland, ref, olds, pitch in TERMINALS:
            TERM_LABEL[ref] = [labels[o] for o in olds]
        # The 5 V supply: a fixed-voltage switching regulator in place of the buck module.
        self.parts["U3"] = dict(ref="U3", lib="Converter_DCDC", fp="Converter_DCDC_RECOM_R-78HB-0.5_THT",
                                value="R-78HB5.0-0.5", kind="REG", step="P1", note="", pmap={})
        self.parts["C6"] = dict(ref="C6", lib="Capacitor_THT", fp="CP_Radial_D6.3mm_P2.50mm", value="47 µF 16 V",
                                kind="CE", step="P1", note="", pmap={"+": "1", "-": "2"})
        self.parts["C7"] = dict(ref="C7", lib="Capacitor_THT", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm", value="100 nF",
                                kind="C", step="P1", note="", pmap={})

        # Union-find over every wire's stops.
        parent = {}

        def find(a):
            parent.setdefault(a, a)
            while parent[a] != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a

        def union(a, b):
            parent[find(a)] = find(b)

        def node(bname, n):
            ref, pin = n.split(".", 1)
            if ref == "JA":
                return hub_pin(boards.HUB_A, int(pin))
            if ref == "JB":
                return hub_pin(boards.HUB_B, int(pin))
            if ref == "BUCK":
                return {"IN+": ("U3", "1"), "IN-": ("U3", "2"), "OUT-": ("U3", "2"), "OUT+": ("U3", "3")}[pin]
            if ref in term_of:
                return term_of[ref]
            ref = self.rename(bname, ref)
            return ref, self.parts[ref]["pmap"].get(pin, pin)

        wire_label = {}
        for b in (pw, lg):
            for w in b.wires:
                ns = [node(b.name, n) for n in w["nodes"]]
                ns = [n for n in ns if n]
                for n in ns:
                    find(n)
                    wire_label.setdefault(n, []).append(w["net"])
                for a, c in zip(ns, ns[1:]):
                    union(a, c)
        # the regulator's own capacitors
        for n in (("C6", "1"), ("C7", "1")):
            union(n, ("U3", "3"))
        for n in (("C6", "2"), ("C7", "2")):
            union(n, ("U3", "2"))
        groups = {}
        for n in parent:
            groups.setdefault(find(n), []).append(n)
        self.nets = []
        used = set()
        for pins in groups.values():
            name = self.net_name(pins, wire_label)
            base, k = name, 2
            while name in used:
                name, k = f"{base}_{k}", k + 1
            used.add(name)
            self.nets.append(dict(name=name, pins=sorted(pins)))
        self.nets.sort(key=lambda n: n["name"])

    @staticmethod
    def rename(board, ref):
        # Both perfboards had pull-ups RU1-RU6; the logic board's become RU11-RU17.
        if board == "logic" and re.fullmatch(r"RU\d+", ref):
            return f"RU{int(ref[2:]) + 10}"
        return ref

    @staticmethod
    def net_name(pins, wire_label):
        mega = sorted(p for r, p in pins if r == "MEGA")
        if "GND" in mega:
            return "GND"
        if "5V" in mega:
            return "+5V"
        labels = [lab for n in pins for lab in wire_label.get(n, [])]
        if mega:
            return mega[0]
        for pref in ("+12 DRV", "+12 LOG", "+12 COIL", "LOG", "VBAT", "SWITCH FEED"):
            if pref in labels:
                return pref.replace(" ", "_")
        if not labels:
            return "N_" + "_".join(f"{r}{p}" for r, p in pins[:2])
        lab = max(set(labels), key=labels.count)
        return re.sub(r"[^A-Za-z0-9+_-]+", "_", lab).strip("_")

    def net_of(self):
        out = {}
        for n in self.nets:
            for r, p in n["pins"]:
                out[(r, p)] = n["name"]
        return out


# ---------------------------------------------------------------- placement
# Each part: (x, y, rotation) of its pin 1 in board mm (origin at the front-left
# corner, y towards the back); rotation counter-clockwise, seen from above.
def terminal_x():
    """Pin-1 x of each terminal group: bodies left to right with TERM_GAPS between."""
    out, x = {}, 0.0
    bodies = []
    for gland, ref, olds, pitch in TERMINALS:
        n = len(olds)
        lo = 2.25 if pitch == 3.5 else 2.54
        bodies.append((ref, lo, (n - 1) * pitch + 2 * lo))
    total = sum(w for _, _, w in bodies) + sum(TERM_GAPS)
    x = (W - total) / 2
    for i, (ref, lo, w) in enumerate(bodies):
        out[ref] = round(x + lo, 2)
        x += w + (TERM_GAPS[i] if i < len(TERM_GAPS) else 0)
    return out


TERM_Y = 6.35


def placement():
    P = {}
    for ref, x in terminal_x().items():
        P[ref] = (x, TERM_Y, 0)
    P["A1"] = (MEGA_AT[0], MEGA_AT[1], 0)
    V = 90  # standing up: pin 1 towards the front

    # --- logic, front left: seven switch-input filters, one column per input
    for i in range(7):
        x = 8.89 + 5.08 * i
        P[f"RU{11 + i}"] = (x, 16.51, V)         # pull-up to 5 V
        P[f"RS{i + 1}"] = (x + 2.54, 30.48, V)   # series 1k
        P[f"DH{i + 1}"] = (x, 43.18, V)          # clamp to 5 V
        P[f"DL{i + 1}"] = (x + 2.54, 53.34, V)   # clamp to ground
        P[f"CF{i + 1}"] = (x, 63.5, V)           # 100 nF
    # ATF probe input
    for j, r in enumerate(["RA1", "RA2", "ZA"]):
        P[r] = (45.72 + 3.81 * j, 16.51, V)
    # gyro, flat (header at the front)
    P["GYRO"] = (30.48, 73.66, 0)

    # --- car signal inputs, middle: three optocouplers, speed comparator, throttle taps
    for i, (ok, ri, di, ru) in enumerate((("OK1", "RI1", "DI1", "RU1"), ("OK2", "RI2", "DI2", "RU2"),
                                          ("OK3", "RI3", "DI3", "RU3"))):
        x = 59.69 + 13.97 * i
        P[ri] = (x, 16.51, V)
        P[di] = (x + 3.81, 16.51, V)
        P[ru] = (x + 7.62, 16.51, V)
        P[ok] = (x, 36.83, 0)
    P["U2"] = (59.69, 62.23, 0)
    for j, r in enumerate(["RC1", "RC2", "Z2", "RC3", "RC6", "RC4", "RC5"]):
        P[r] = (72.39 + 3.81 * j, 46.99, V)
    for j, r in enumerate(["RT1", "RT2", "DT1", "DT2", "DT3", "DT4", "C3", "C4"]):
        P[r] = (59.69 + 3.81 * j, 72.39, V)

    # --- power in, behind the PWR terminals
    P["TVS1"] = (102.87, 16.51, V)
    P["D1"] = (109.22, 16.51, V)
    P["TVS2"] = (115.57, 16.51, V)
    P["TVS3"] = (121.92, 16.51, V)
    P["U3"] = (102.87, 43.18, 0)
    P["C1"] = (114.3, 41.91, 0)
    P["C6"] = (123.19, 41.91, 0)
    for j, r in enumerate(["RB1", "RB2", "C2", "Z1", "C7"]):
        P[r] = (102.87 + 3.81 * j, 48.26, V)
    # ON/OFF power pole: fuse, flyback diode, coil-supply sense
    P["F2"] = (102.87, 64.77, 0)
    P["DF"] = (111.76, 64.77, 0)
    for j, r in enumerate(["RK1", "RK2", "ZK", "CK"]):
        P[r] = (102.87 + 3.81 * j, 72.39, V)

    # --- three solenoid channels, 19 mm apart: relay, small parts, MOSFET, current sensor
    for n in (1, 2, 3):
        x = 130.81 + 19.05 * (n - 1)
        P[f"K{n}"] = (x, 50.8, 0)              # coil pins at the back, contacts towards the terminals
        for j, r in enumerate([f"RG{n}", f"RP{n}", f"ZG{n}", f"DF{n}", f"ZF{n}"]):
            P[r] = (x - 3.81 + 3.81 * j, 55.88, V)
        P[f"Q{n}"] = (x, 71.12, 0)             # flat, tab towards the back
        P[f"CS{n}"] = (x + 3.81, 134.62, 0)    # pins at the back, current links towards the MOSFET
    # relay driver, right
    P["U1"] = (186.69, 133.35, 0)
    for j, r in enumerate(["RD1", "RD2", "RD3", "RD4"]):
        P[r] = (184.15, 92.71 + 3.81 * j, 0)
    # ECU sense, back middle (right of the Mega)
    for i, (ok, ri, di, ru) in enumerate((("OK4", "RI4", "DI4", "RU4"), ("OK5", "RI5", "DI5", "RU5"),
                                          ("OK6", "RI6", "DI6", "RU6"))):
        y = 95.25 + 15.24 * i
        P[ri] = (105.41, y, 0)
        P[di] = (105.41, y + 3.81, 0)
        P[ru] = (105.41, y + 7.62, 0)
        P[ok] = (118.11, y + 2.54, 0)
    return P


# ---------------------------------------------------------------- the board
def outline():
    """The board edge, clockwise from the front-left corner (board mm)."""
    nx, ny = NOTCH
    return [(0, 0), (W, 0), (W, H), (nx, H), (nx, ny), (0, ny)]


MOUNT_HOLES = [(3.5, 18.0), (196.5, 18.0), (196.5, 146.5), (10.0, 88.0), (110.0, 146.5)]


def load_fp(lib, name):
    import pcbnew
    path = LIB if lib == "rav4tcu" else os.path.join(KLIB, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise FileNotFoundError(f"{lib}:{name}")
    return fp


def build_board(c, P):
    import pcbnew
    board = pcbnew.BOARD()
    nets = {}
    for n in c.nets:
        ni = pcbnew.NETINFO_ITEM(board, n["name"])
        board.Add(ni)
        nets[n["name"]] = ni
    net_of = c.net_of()
    for ref, part in sorted(c.parts.items()):
        fp = load_fp(part["lib"], part["fp"])
        fp.SetReference(ref)
        fp.SetValue(part["value"])
        x, y, rot = P[ref]
        fp.SetPosition(pcbnew.VECTOR2I(*kxy(x, y)))
        fp.SetOrientationDegrees(rot)
        key = "MEGA" if ref == "A1" else ref
        for pd in fp.Pads():
            n = net_of.get((key, pd.GetNumber()))
            if n:
                pd.SetNet(nets[n])
        board.Add(fp)
    silkscreen(board)
    pts = outline()
    for a, b in zip(pts, pts[1:] + pts[:1]):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(pcbnew.VECTOR2I(*kxy(*a)))
        seg.SetEnd(pcbnew.VECTOR2I(*kxy(*b)))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(int(0.1e6))
        board.Add(seg)
    for i, (x, y) in enumerate(MOUNT_HOLES):
        fp = load_fp("MountingHole", "MountingHole_3.2mm_M3")
        fp.SetReference(f"H{i + 1}")
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
        fp.SetPosition(pcbnew.VECTOR2I(*kxy(x, y)))
        board.Add(fp)
    return board


# Short labels printed behind each screw terminal (at most 4-5 characters).
TERM_SILK = {
    "X7": ["UP", "DN", "GND"],
    "X4": ["ECO", "SPT", "MAN", "TST", "ON", "GND", "C7", "C8"],
    "X5": ["5V", "GND", "SDA", "SCL"],
    "X6": ["5V", "GND", "DQ"],
    "J3": ["D", "STP", "E1", "SPD", "TAC", "VTA", "E2"],
    "J1": ["12V", "12V", "GND"],
    "J2": ["ECU1", "ECU2", "ECU3", "SOL1", "SOL2", "SOL3"],
    "J4": ["LD1", "LD2", "LD3", "GND"],
}


def _text(board, text, x, y, size, layer=None, angle=0, bold=False):
    import pcbnew
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I(*kxy(x, y)))
    t.SetTextSize(pcbnew.VECTOR2I(int(size * 1e6), int(size * 1e6)))
    t.SetTextThickness(int(size * (0.2 if bold else 0.15) * 1e6))
    t.SetLayer(pcbnew.F_SilkS if layer is None else layer)
    if angle:
        t.SetTextAngleDegrees(angle)
    board.Add(t)
    return t


def silkscreen(board):
    """References in the middle of each part, terminal names, the title."""
    import pcbnew
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        fp.Value().SetVisible(False)
        if ref == "A1" or ref.startswith("H"):
            fp.Reference().SetVisible(False)
            continue
        if ref in TERM_SILK:
            fp.Reference().SetVisible(False)
            for pd in fp.Pads():
                x, y = bxy(*pd.GetPosition())
                _text(board, TERM_SILK[ref][int(pd.GetNumber()) - 1], x, 13.1, 0.9, bold=True)
            continue
        x0, y0, x1, y1 = courtyard(fp)
        lib_name = str(fp.GetFPID().GetLibItemName())
        big = (x1 - x0) > 9 and (y1 - y0) > 9 and not lib_name.startswith("CP_Radial")
        size = 1.2 if big else 0.8
        r = fp.Reference()
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if lib_name.startswith("CP_Radial"):
            cx = bxy(*fp.GetPosition())[0] + (1.75 if "D8.0" in lib_name else 1.25)   # in front of the can
            cy = y0 - 1.0
        r.SetPosition(pcbnew.VECTOR2I(*kxy(cx, cy)))
        r.SetTextSize(pcbnew.VECTOR2I(int(size * 1e6), int(size * 1e6)))
        r.SetTextThickness(int(size * 0.15e6))
        r.SetTextAngleDegrees(0)   # relative to the part: along its body
    # The Mega pins the TCU uses, named next to their holes (the bench checks probe them).
    used = {"5V", "GND", "D2", "D3", "D6", "D8", "D9", "D10", "D20", "D21", "A0", "A1", "A3", "A4", "A5", "A6",
            "A7"} | {f"D{n}" for n in range(22, 35)}
    mx, my = MEGA_AT
    seen = set()
    for n, x, y in mega_pins():
        if n not in used or (n, y) in seen:
            continue
        seen.add((n, y))
        if x > 93:                            # the double header: labels beside it
            _text(board, n, mx + x + (4.0 if x > 95 else -4.0), my + y, 0.8)
        elif y > 45:                          # top row: labels in front of it
            _text(board, n, mx + x, my + y - 2.6, 0.8, angle=90)
        else:                                 # bottom row: labels behind it
            _text(board, n, mx + x, my + y + 2.6, 0.8, angle=90)
    _text(board, "RAV4 TCU  rev 1", 52.0, 128.0, 3.0, bold=True)
    _text(board, COPYRIGHT, 52.0, 122.5, 1.2)
    _text(board, "ARDUINO MEGA UNDERNEATH: TRIM ALL LEADS SHORT IN THIS AREA", 52.0, 118.0, 1.0)
    _text(board, "RAV4 TCU: BOTTOM", 52.0, 128.0, 2.0, layer=pcbnew.B_SilkS)


def courtyard(fp):
    """Axis-aligned courtyard box of a footprint, board mm (x0, y0, x1, y1)."""
    import pcbnew
    xs, ys = [], []
    for g in fp.GraphicalItems():
        if g.GetLayer() == pcbnew.F_CrtYd:
            bb = g.GetBoundingBox()
            for kx, ky in ((bb.GetLeft(), bb.GetTop()), (bb.GetRight(), bb.GetBottom())):
                bx, by = bxy(kx, ky)
                xs.append(bx)
                ys.append(by)
    if not xs:
        bb = fp.GetBoundingBox(False, False)
        for kx, ky in ((bb.GetLeft(), bb.GetTop()), (bb.GetRight(), bb.GetBottom())):
            bx, by = bxy(kx, ky)
            xs.append(bx)
            ys.append(by)
    return min(xs), min(ys), max(xs), max(ys)


def placement_problems(board):
    """Overlapping courtyards, parts off the board or in the notch, parts on the Mega's pins."""
    fps = [f for f in board.GetFootprints() if f.GetReference() != "A1"]
    boxes = {f.GetReference(): courtyard(f) for f in fps}
    probs = []
    refs = sorted(boxes)
    for i, a in enumerate(refs):
        ax0, ay0, ax1, ay1 = boxes[a]
        if ax0 < -0.01 or ay0 < -0.01 or ax1 > W + 0.01 or ay1 > H + 0.01:
            probs.append(f"{a} off the board {tuple(round(v, 1) for v in boxes[a])}")
        if ax0 < NOTCH[0] and ay1 > NOTCH[1]:
            probs.append(f"{a} in the USB notch")
        for b in refs[i + 1:]:
            bx0, by0, bx1, by1 = boxes[b]
            if ax0 < bx1 and bx0 < ax1 and ay0 < by1 and by0 < ay1:
                probs.append(f"{a} overlaps {b}")
    mega = [f for f in board.GetFootprints() if f.GetReference() == "A1"][0]
    for pd in mega.Pads():
        px, py = bxy(*pd.GetPosition())
        for r, (x0, y0, x1, y1) in boxes.items():
            if x0 - 0.8 < px < x1 + 0.8 and y0 - 0.8 < py < y1 + 0.8:
                probs.append(f"{r} sits on Mega pin {pd.GetNumber()}")
    return probs


def render(board, path, title="", highlight=(), done=(), routes=True, scale=6.0):
    """A plain top view: board, courtyards, pads, labels; highlight = this step's parts."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Circle, Polygon, Rectangle
    import pcbnew
    fig = plt.figure(figsize=(W / 25.4 * scale / 2.2, H / 25.4 * scale / 2.2), dpi=110)
    ax = fig.add_axes([0.01, 0.01, 0.98, 0.94])
    ax.set_xlim(-3, W + 3)
    ax.set_ylim(-3, H + 3)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.add_patch(Polygon(outline(), closed=True, fc="#2e7d32", ec="#1b5e20", lw=2))
    if routes:
        for t in board.GetTracks():
            a, b = bxy(*t.GetStart()), bxy(*t.GetEnd())
            if t.GetClass() == "PCB_VIA":
                ax.add_patch(Circle(a, 0.4, fc="#c8e6c9", ec="none", zorder=2))
                continue
            col = "#a5d6a7" if t.GetLayer() == pcbnew.F_Cu else "#66bb6a"
            ax.plot([a[0], b[0]], [a[1], b[1]], color=col, lw=t.GetWidth() / 1e6 * 2.2, solid_capstyle="round",
                    zorder=1)
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref == "A1":
            mx, my = MEGA_AT
            ax.add_patch(Rectangle((mx, my), 101.6, 53.34, fc="none", ec="#90caf9", lw=1.2, ls="--", zorder=3))
            ax.text(mx + 50, my + 27, "Arduino Mega\n(underneath)", color="#90caf9", ha="center", va="center",
                    fontsize=9, zorder=3)
        elif ref.startswith("H"):
            x, y = bxy(*fp.GetPosition())
            ax.add_patch(Circle((x, y), 1.6, fc="#1b5e20", ec="#e0e0e0", zorder=3))
            continue
        else:
            x0, y0, x1, y1 = courtyard(fp)
            if ref in highlight:
                fc, ec = "#ff7043", "#bf360c"
            elif ref in done or not highlight:
                fc, ec = "#eceff1", "#90a4ae"
            else:
                fc, ec = "none", "#81c784"
            ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc=fc, ec=ec, lw=0.8, zorder=3, alpha=0.9))
            fs = 6.5 if ref in highlight else 5
            ax.text((x0 + x1) / 2, (y0 + y1) / 2, ref, ha="center", va="center", fontsize=fs, zorder=5,
                    color="#000" if (ref in highlight or ref in done or not highlight) else "#c8e6c9",
                    fontweight="bold" if ref in highlight else "normal")
        for pd in fp.Pads():
            x, y = bxy(*pd.GetPosition())
            sz = pd.GetSize()
            r = max(sz.x, sz.y) / 2e6
            ax.add_patch(Circle((x, y), r * 0.8, fc="#ffd54f", ec="#8d6e63", lw=0.3, zorder=4))
    ax.set_title(title, fontsize=12, fontweight="bold")
    fig.savefig(path)
    plt.close(fig)


# ---------------------------------------------------------------- routing
# Track widths (mm): the solenoid current paths are wide, the 12 V feed widest.
def track_width(net):
    if net == "+12_DRV":
        return 1.5
    if re.match(r"(SOL|ECU|LOAD|DOUT|SRC)_?\d", net) or net in ("+12_LOG", "LOG"):
        return 1.0
    if net in ("GND", "+5V", "+12_COIL", "SWITCH_FEED"):
        return 0.8
    return 0.4


def via_size(net):
    """(drill, pad) mm: bigger vias where the current is bigger."""
    return (0.6, 1.2) if track_width(net) >= 1.0 else (0.4, 0.8)


def inside_board(x, y, margin):
    pts = outline()
    inside = False
    for (x1, y1), (x2, y2) in zip(pts, pts[1:] + pts[:1]):
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            inside = not inside
    if not inside:
        return False
    for (x1, y1), (x2, y2) in zip(pts, pts[1:] + pts[:1]):
        dx, dy = x2 - x1, y2 - y1
        t = max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)))
        if math.hypot(x - (x1 + t * dx), y - (y1 + t * dy)) < margin:
            return False
    return True


def route(board, log=print):
    import pcbnew
    import pcb_router as pr
    nets = sorted({p.GetNetname() for f in board.GetFootprints() for p in f.Pads() if p.GetNetname()})
    nid = {n: i for i, n in enumerate(nets)}
    hw = {i: track_width(n) / 2 for n, i in nid.items()}
    vh = {i: via_size(n)[1] / 2 for n, i in nid.items()}
    r = pr.Router(W, H, inside_board, hw, vh)
    pads_of = {}
    pad_obj = []
    shapes = {pcbnew.PAD_SHAPE_CIRCLE: "circle", pcbnew.PAD_SHAPE_OVAL: "oval", pcbnew.PAD_SHAPE_RECT: "rect",
              pcbnew.PAD_SHAPE_ROUNDRECT: "rect"}
    for f in board.GetFootprints():
        for p in f.Pads():
            x, y = bxy(*p.GetPosition())
            sz = p.GetSize()
            sx, sy = sz.x / 1e6, sz.y / 1e6
            ang = p.GetOrientationDegrees()
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                d = max(p.GetDrillSize().x / 1e6, sx)
                if f.GetReference().startswith("H"):
                    d = 7.0   # screw head and standoff
                r.pads.append(pr.Pad(-2, x, y, "circle", d, d, keepout=True))
                continue
            if not p.GetNetname():
                r.pads.append(pr.Pad(-2, x, y, shapes.get(p.GetShape(), "rect"), sx, sy, ang, keepout=True))
                continue
            n = nid[p.GetNetname()]
            pads_of.setdefault(n, []).append(len(r.pads))
            r.pads.append(pr.Pad(n, x, y, shapes.get(p.GetShape(), "rect"), sx, sy, ang))
            pad_obj.append(p)
        # A flat TO-220's metal tab lies on the top layer: keep other tracks out from under it.
        if f.GetReference().startswith("Q"):
            x0, y0, x1, y1 = courtyard(f)
            r.keepouts.append((0, x0 + 1.0, y0 + 3.5, x1 - 1.0, y1 - 1.0))
    r.build(list(nid.values()))
    span = {}
    for n, ps in pads_of.items():
        xs = [r.pads[i].x for i in ps]
        ys = [r.pads[i].y for i in ps]
        span[n] = (max(xs) - min(xs)) + (max(ys) - min(ys))
    order = sorted(pads_of, key=lambda n: (-hw[n] if hw[n] >= 0.5 else 0, span[n]))
    gnd = nid.get("GND")
    order = [n for n in order if n != gnd] + ([gnd] if gnd is not None else [])
    failed = r.route_all(order, pads_of, log=log)
    # into KiCad tracks
    for net, paths in r.paths.items():
        name = nets[net]
        ni = board.FindNet(name)
        w = int(track_width(name) * 1e6)
        dr, vd = via_size(name)
        for path in paths:
            seg_start = path[0]
            for a, b in zip(path, path[1:] + [None]):
                if b is not None and b[0] == a[0] and _collinear(seg_start, a, b):
                    continue
                if b is not None and b[0] != a[0]:
                    _track(board, ni, w, seg_start, a)
                    via = pcbnew.PCB_VIA(board)
                    via.SetPosition(pcbnew.VECTOR2I(*kxy(a[1] * pr.G, a[2] * pr.G)))
                    via.SetDrill(int(dr * 1e6))
                    via.SetWidth(int(vd * 1e6))
                    via.SetNet(ni)
                    board.Add(via)
                    seg_start = b
                    continue
                _track(board, ni, w, seg_start, a)
                seg_start = a
    for net, stubs in r.stubs.items():
        name = nets[net]
        ni = board.FindNet(name)
        w = int(track_width(name) * 1e6)
        for pi, node in stubs:
            p = r.pads[pi]
            nx, ny = node[0] * pr.G, node[1] * pr.G
            if abs(nx - p.x) > 1e-6 or abs(ny - p.y) > 1e-6:
                for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
                    t = pcbnew.PCB_TRACK(board)
                    t.SetStart(pcbnew.VECTOR2I(*kxy(p.x, p.y)))
                    t.SetEnd(pcbnew.VECTOR2I(*kxy(nx, ny)))
                    t.SetWidth(min(w, int(0.6e6)))
                    t.SetLayer(layer)
                    t.SetNet(ni)
                    board.Add(t)
    return [nets[n] for n in failed]


def _collinear(s, a, b):
    return (s[1] == a[1] == b[1]) or (s[2] == a[2] == b[2])


def _track(board, ni, w, a, b):
    import pcbnew
    import pcb_router as pr
    if (a[1], a[2]) == (b[1], b[2]):
        return
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I(*kxy(a[1] * pr.G, a[2] * pr.G)))
    t.SetEnd(pcbnew.VECTOR2I(*kxy(b[1] * pr.G, b[2] * pr.G)))
    t.SetWidth(w)
    t.SetLayer(pcbnew.F_Cu if a[0] == 0 else pcbnew.B_Cu)
    t.SetNet(ni)
    board.Add(t)


def add_ground_fill(board):
    import pcbnew
    gnd = board.FindNet("GND")
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        z.SetLocalClearance(int(0.4e6))
        z.SetMinThickness(int(0.3e6))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(int(0.5e6))
        z.SetThermalReliefSpokeWidth(int(0.5e6))
        ol = z.Outline()
        ol.NewOutline()
        for x, y in outline():
            ol.Append(*kxy(min(max(x, 0.5), W - 0.5), min(max(y, 0.5), H - 0.5)))
        board.Add(z)


def write_project(path):
    """KiCad project settings next to the board: which DRC checks are errors.
    The footprints come straight from KiCad's library folders (no library
    table), so the "doesn't match library" checks don't apply."""
    import json
    pro = {
        "board": {"design_settings": {"rule_severities": {
            "lib_footprint_issues": "ignore", "lib_footprint_mismatch": "ignore",
            "starved_thermal": "warning"}}},
        "meta": {"filename": os.path.basename(path), "version": 1},
    }
    json.dump(pro, open(path, "w"), indent=2)


def fill_and_reload(path):
    """Zone filling is only reliable on a board loaded from a file."""
    import pcbnew
    write_project(path.replace(".kicad_pcb", ".kicad_pro"))
    board = pcbnew.LoadBoard(path)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(path)
    return board


def design_rules(board):
    import pcbnew
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = int(0.2e6)
    ds.m_MinClearance = int(0.15e6)
    ds.m_CopperEdgeClearance = int(0.5e6)
    ds.m_ViasMinSize = int(0.6e6)
    ds.m_MinThroughDrill = int(0.3e6)
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetClearance(int(0.2e6))
    nc.SetTrackWidth(int(0.4e6))
    nc.SetViaDiameter(int(0.8e6))
    nc.SetViaDrill(int(0.4e6))


def starved_pads(report):
    """(ref, pad) of every pad whose thermal relief DRC calls starved."""
    text = open(report).read()
    out = set()
    for blk in re.findall(r"\[starved_thermal\].*?(?=\n\[|\Z)", text, re.S):
        for num, ref in re.findall(r"PTH pad (\S+) \[[^\]]*\] of (\S+)", blk):
            out.add((ref, num))
    return out


def drc(board, path):
    import pcbnew
    pcbnew.WriteDRCReport(board, path, pcbnew.EDA_UNITS_MILLIMETRES, True)
    text = open(path).read()
    items = re.findall(r"^\[(\w+)\]: [^\n]*\n[^\n]*Severity: (\w+)", text, re.M)
    u = re.search(r"\*\* Found (\d+) unconnected pads \*\*", text)
    errors = [k for k, sev in items if sev == "error"]
    warnings = [k for k, sev in items if sev != "error"]
    return errors, (int(u.group(1)) if u else -1), warnings


# ---------------------------------------------------------------- build steps
# Solder the board in six steps, testing as you go. In each step, the lowest
# parts first.
STEPS = [
    ("S1", "Power in", ["J1", "TVS1", "TVS2", "TVS3", "D1", "C1", "U3", "C6", "C7", "RB1", "RB2", "C2", "Z1"]),
    ("S2", "Relays and the ON/OFF power pole", ["U1", "RD1", "RD2", "RD3", "RD4", "K1", "K2", "K3", "X4", "F2", "DF",
                                                "RK1", "RK2", "CK", "ZK"]),
    ("S3", "Solenoid drivers and current sensors", ["J2", "J4"] + [f"{k}{n}" for n in (1, 2, 3)
                                                     for k in ("Q", "RG", "RP", "ZG", "DF", "ZF", "CS")]),
    ("S4", "Car signal inputs", ["J3", "U2", "RC1", "RC2", "RC3", "RC4", "RC5", "RC6", "Z2", "RT1", "RT2", "C3", "C4",
                                 "DT1", "DT2", "DT3", "DT4"] + [f"{k}{n}" for n in range(1, 7)
                                                                 for k in ("OK", "RI", "DI", "RU")]),
    ("S5", "Dash, shifter, screen and probe inputs", ["X7", "X5", "X6", "RA1", "RA2", "ZA", "GYRO"]
     + [f"{k}{n}" for n in range(1, 8) for k in ("RU1", "RS", "CF", "DH", "DL")]),
    ("S6", "The Mega's pins", ["A1"]),
]

HEIGHT_ORDER = {"R": 0, "D": 1, "C": 2, "PTC": 3, "IC": 4, "CE": 6, "Q": 5, "REG": 6, "TB": 7, "K": 8, "MOD": 9,
                "MEGA": 10}
PART_HEIGHT = {"R": 3, "D": 4, "C": 6, "PTC": 9, "IC": 5, "CE": 13, "Q": 5, "REG": 18, "TB": 10, "K": 16,
               "MOD": 14}


def step_of(ref):
    for s, _, refs in STEPS:
        if ref in refs:
            return s
    return None


def part_line(c, ref):
    """One build-book line for a part: what it is, which way round."""
    p = c.parts[ref]
    k, v = p["kind"], p["value"]
    words = {"R": "resistor", "D": "diode", "C": "capacitor", "CE": "capacitor", "IC": "chip",
             "PTC": "resettable fuse", "TB": "screw terminal block", "K": "relay", "Q": "transistor",
             "REG": "5 V regulator", "MOD": "module", "MEGA": ""}
    what = words.get(k, "part")
    extra = ""
    if k == "R":
        extra = f" (stripes: {boards.bands_text(v)})"
    how = {
        "D": "Stripe to the end with the stripe printed on the board.",
        "IC": "Notch (or dot) to the notch printed on the board.",
        "CE": "Long leg (+) into the hole marked +; the stripe on the can is the − side.",
        "K": "It only fits one way.",
        "Q": "Bend the legs down 90° about 3 mm from the body, so it lies flat with the hole in its "
             "metal tab over the hole in the board. Bolt the tab down (M3 × 8 screw and nut) before soldering.",
        "REG": "The printed side faces the front edge (pin 1, the square pad, on the left).",
        "TB": "Wire openings towards the front edge of the board.",
        "PTC": "Either way round.",
    }.get(k, "")
    if ref.startswith("CS"):
        how = ("Its 3 pins go into the holes marked VCC, OUT, GND: check its own printed labels match. "
               "Then two short pieces of stiff wire from its screw terminal into the two big holes marked LINK.")
    if ref == "GYRO":
        how = "Solder its 8-pin header into the board first, then the gyro onto the header, lying flat, chip side up."
    if ref == "A1":
        return ("- [ ] **The Mega's pins:** push male header strips into the Mega's sockets (cut to length: "
                "8, 8, 8, 10, 8, 8 and an 18 × 2), lay this board on top so every pin pokes through its hole, "
                "and solder them from the top. They line up perfectly that way.")
    return f"- [ ] **{ref}**: {v} {what}{extra}. {how}".replace("  ", " ").rstrip(" .") + ("." if how else "")


def steps_markdown(c, board):
    out = {}
    for s, title, refs in STEPS:
        refs = sorted(refs, key=lambda r: (HEIGHT_ORDER.get(c.parts[r]["kind"], 5), _natural(r)))
        lines = [f"![Step {s}](build/pcb-{s}.png)", "",
                 f"*{len(refs)} part{'s' if len(refs) > 1 else ''}, orange in the picture; grey ones are already "
                 "in. Lowest parts first.*", ""]
        lines += [part_line(c, r) for r in refs]
        out[s] = "\n".join(lines)
    return out


def _natural(s):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", s)]


def bags_markdown(c):
    lines = []
    for s, title, refs in STEPS:
        count = {}
        for r in refs:
            p = c.parts[r]
            if p["kind"] == "MEGA":
                key = ("male header strips (40-pin)", "")
            elif p["kind"] == "TB":
                n = int(re.search(r"1x(\d+)", p["fp"]).group(1))
                pitch = "3.5 mm" if "3.5" in p["fp"] else "5.08 mm"
                key = (f"{n}-way {pitch} screw terminal ({r})", "")
            else:
                key = (p["value"], p["kind"])
            count[key] = count.get(key, 0) + 1
        words = {"R": "resistor", "D": "diode", "C": "capacitor", "CE": "capacitor", "IC": "chip", "K": "relay",
                 "Q": "transistor", "PTC": "resettable fuse", "REG": "regulator", "MOD": "module"}
        items = []
        for (v, k), n in count.items():
            w = words.get(k, "")
            band = f" ({boards.bands_text(v)})" if k == "R" else ""
            items.append(f"{n}× {v} {w}{band}".replace("  ", " ").strip())
        lines.append(f"- [ ] **Bag {s}** ({title}): " + "; ".join(items))
    return "\n".join(lines)


def step_pictures(board):
    os.makedirs(os.path.join(ROOT, "docs", "build"), exist_ok=True)
    done = set()
    for s, title, refs in STEPS:
        render(board, os.path.join(ROOT, "docs", "build", f"pcb-{s}.png"), f"Step {s}: {title}",
               highlight=set(refs), done=set(done), routes=False, scale=5.0)
        done |= set(refs)


# ---------------------------------------------------------------- outputs
def bom(c):
    rows = {}
    for ref, p in sorted(c.parts.items(), key=lambda kv: _natural(kv[0])):
        if p["kind"] == "MEGA":
            continue
        key = (p["value"], p["fp"])
        rows.setdefault(key, []).append(ref)
    lines = ["Qty,Value,Footprint,References"]
    for (v, fp), refs in sorted(rows.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        lines.append(f'{len(refs)},"{v}","{fp}","{" ".join(refs)}"')
    lines.append('1,"Arduino Mega 2560 (underneath, on male headers)","Arduino_Mega2560_Shield","A1"')
    open(os.path.join(OUT, "bom.csv"), "w").write("\n".join(lines) + "\n")


def fab_files(pcb_path):
    """Gerbers + drill files, zipped: the one file to upload to a board maker."""
    import subprocess
    import zipfile
    gdir = os.path.join(OUT, "gerbers")
    os.makedirs(gdir, exist_ok=True)
    for f in os.listdir(gdir):
        os.remove(os.path.join(gdir, f))
    subprocess.run(["kicad-cli", "pcb", "export", "gerbers", "--layers",
                    "F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts", "--subtract-soldermask",
                    "-o", gdir + "/", pcb_path], check=True, capture_output=True)
    subprocess.run(["kicad-cli", "pcb", "export", "drill", "-o", gdir + "/", pcb_path], check=True,
                   capture_output=True)
    zpath = os.path.join(OUT, "rav4-tcu-gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)
    return zpath


def layer_pictures(pcb_path):
    """docs/pcb-top.png and docs/pcb-bottom.png from KiCad's own plots."""
    import subprocess
    import tempfile
    tmp = tempfile.mkdtemp()
    for name, layers in (("top", "F.Cu,F.Silkscreen,Edge.Cuts"), ("bottom", "B.Cu,B.Silkscreen,Edge.Cuts")):
        svg = os.path.join(tmp, name + ".svg")
        subprocess.run(["kicad-cli", "pcb", "export", "svg", "--layers", layers, "--exclude-drawing-sheet",
                        "--page-size-mode", "2", "-o", svg, pcb_path], check=True, capture_output=True)
        png = os.path.join(ROOT, "docs", f"pcb-{name}.png")
        subprocess.run(["rsvg-convert", "-z", "2.5", "-b", "white", "-o", png, svg], check=True)
        from PIL import Image
        Image.open(png).convert("RGB").quantize(colors=32).save(png, optimize=True)


def write_envelope(c, board):
    """hardware/pcb_envelope.scad: the board, every part's footprint and height, for the box's fit check."""
    lines = ["// Generated by tools/pcb.py - do not edit.",
             "// Board mm: origin at the front-left corner, y towards the back; part heights above the board.",
             f"pcb_size = [{W}, {H}];",
             "pcb_outline = [" + ", ".join(f"[{x:.2f}, {y:.2f}]" for x, y in outline()) + "];",
             "pcb_holes = [" + ", ".join(f"[{x:.2f}, {y:.2f}]" for x, y in MOUNT_HOLES) + "];",
             f"pcb_mega_at = [{MEGA_AT[0]:.2f}, {MEGA_AT[1]:.2f}];",
             f"pcb_standoff_h = {STANDOFF_H};",
             "pcb_parts = [  // [ref, x0, y0, x1, y1, height]"]
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref == "A1" or ref.startswith("H"):
            continue
        x0, y0, x1, y1 = courtyard(fp)
        h = PART_HEIGHT.get(c.parts[ref]["kind"], 6)
        if c.parts[ref]["kind"] == "MOD" and ref == "GYRO":
            h = 6
        lines.append(f'    ["{ref}", {x0:.2f}, {y0:.2f}, {x1:.2f}, {y1:.2f}, {h}],')
    lines.append("];")
    lines.append("pcb_terminals = [  // [gland, pad x first, pad x last, front of the body y]")
    for gland, ref, olds, pitch in TERMINALS:
        fp = board.FindFootprintByReference(ref)
        xs = [bxy(*pd.GetPosition())[0] for pd in fp.Pads()]
        lines.append(f'    ["{gland}", {min(xs):.2f}, {max(xs):.2f}, {courtyard(fp)[1]:.2f}],')
    lines.append("];")
    open(os.path.join(ROOT, "hardware", "pcb_envelope.scad"), "w").write("\n".join(lines) + "\n")


def write_book(c, board):
    tpl = open(os.path.join(ROOT, "tools", "build_book_template.md")).read()
    md = steps_markdown(c, board)
    for s, text in md.items():
        tpl = tpl.replace("{{PCB STEP %s}}" % s, text)
    tpl = tpl.replace("{{BAGS}}", bags_markdown(c))
    if "{{" in tpl:
        i = tpl.index("{{")
        sys.exit("build book: unfilled placeholder " + tpl[i:i + 30])
    open(os.path.join(ROOT, "docs", "05-build-book.md"), "w").write(tpl)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--place", action="store_true", help="placement and pictures only")
    args = ap.parse_args()
    write_lib()
    c = Circuit()
    P = placement()
    missing = sorted(set(c.parts) - set(P))
    if missing:
        sys.exit("no placement for: " + " ".join(missing))
    board = build_board(c, P)
    probs = placement_problems(board)
    in_steps = [r for _, _, refs in STEPS for r in refs]
    if sorted(in_steps) != sorted(c.parts):
        probs.append("build steps: missing " + " ".join(sorted(set(c.parts) - set(in_steps)))
                     + "; extra/doubled " + " ".join(sorted(r for r in set(in_steps)
                                                            if in_steps.count(r) > 1 or r not in c.parts)))
    for pr in probs:
        print("PLACEMENT:", pr)
    os.makedirs(OUT, exist_ok=True)
    render(board, os.path.join(OUT, "placement.png"), "TCU board: placement", routes=False)
    print(len(c.parts), "parts,", len(c.nets), "nets,", len(probs), "placement problems")
    if args.place:
        sys.exit(1 if probs else 0)
    import pcbnew
    design_rules(board)
    failed = route(board)
    add_ground_fill(board)
    pcb_path = os.path.join(OUT, "rav4-tcu.kicad_pcb")
    board.Save(pcb_path)
    board = fill_and_reload(pcb_path)
    errs, nu, warns = drc(board, os.path.join(OUT, "drc.txt"))
    starved = starved_pads(os.path.join(OUT, "drc.txt"))
    if starved:
        for fp in board.GetFootprints():
            for pd in fp.Pads():
                if (fp.GetReference(), pd.GetNumber()) in starved:
                    pd.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        board.Save(pcb_path)
        board = fill_and_reload(pcb_path)
        errs, nu, warns = drc(board, os.path.join(OUT, "drc.txt"))
    count = lambda xs: ", ".join(f"{k} {xs.count(k)}" for k in sorted(set(xs)))
    print(f"DRC: {len(errs)} errors ({count(errs)}), {nu} unconnected, {len(warns)} warnings ({count(warns)})")
    render(board, os.path.join(OUT, "routed.png"), "TCU board: routed")
    if errs or nu:
        sys.exit("the board has DRC errors or unconnected pads: see hardware/pcb/drc.txt")
    step_pictures(board)
    bom(c)
    fab_files(pcb_path)
    layer_pictures(pcb_path)
    write_envelope(c, board)
    write_book(c, board)
    print("wrote hardware/pcb/ (board, gerbers zip, bom.csv), docs/pcb-*.png, docs/build/pcb-S*.png, "
          "hardware/pcb_envelope.scad, docs/05-build-book.md")
