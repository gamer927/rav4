#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""Generate docs/wiring-diagram.svg and .png (system wiring for the RAV4 TCU).

Every wire carries a W-number that matches the wire list in
docs/03-wiring-guide.md. Run: python3 tools/wiring_diagram.py
Needs matplotlib (pip install matplotlib).
"""
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "wiring-diagram")

C = {
    "power": "#d62728",
    "ground": "#222222",
    "ecu": "#c49000",
    "sol": "#ff7f0e",
    "sig": "#1f77b4",
    "console": "#2ca02c",
    "i2c": "#9467bd",
    "probe": "#8c564b",
    "internal": "#555555",
}
PIN_PITCH = 0.32

fig = plt.figure(figsize=(24, 16))
ax = fig.add_axes([0, 0, 1, 1])
ax.set_xlim(0, 24)
ax.set_ylim(0, 16)
ax.axis("off")


def zone(x, y, w, h, title, color):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.15",
                                fc=color, ec="#999999", lw=1.2, zorder=0))
    ax.text(x + 0.2, y + h - 0.15, title, fontsize=15, fontweight="bold", va="top", color="#333333")


def box(x, y, w, h, title, lines=(), fc="white", line_size=9):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.08",
                                fc=fc, ec="#333333", lw=1.4, zorder=2))
    ax.text(x + 0.12, y + h - 0.12, title, fontsize=11, fontweight="bold", va="top", zorder=3)
    for i, line in enumerate(lines):
        ax.text(x + 0.12, y + h - 0.45 - i * 0.27, line, fontsize=line_size, va="top",
                family="DejaVu Sans Mono", zorder=3)


def connector(x, top_pin_y, name, pins):
    """Connector as a column of pins; returns {pin: (left_x, y, right_x)}."""
    h = PIN_PITCH * len(pins) + 0.35
    y = top_pin_y + 0.35 - h
    ax.add_patch(FancyBboxPatch((x, y), 0.55, h, boxstyle="square,pad=0", fc="#eeeeee", ec="#333333",
                                lw=1.4, zorder=2))
    ax.text(x + 0.275, y + h + 0.08, name, fontsize=10, fontweight="bold", ha="center", va="bottom", zorder=3)
    anchors = {}
    for i, p in enumerate(pins):
        py = top_pin_y - i * PIN_PITCH
        ax.text(x + 0.275, py, str(i + 1), fontsize=8, ha="center", va="center", zorder=3)
        anchors[p] = (x, py, x + 0.55)
    return anchors


def wire(points, color, lw=2.2):
    xs, ys = zip(*points)
    ax.plot(xs, ys, color=color, lw=lw, solid_capstyle="round", zorder=1.5)
    for px, py in (points[0], points[-1]):
        ax.plot(px, py, "o", color=color, ms=4, zorder=4)


def label(x, y, text, color, ha="center"):
    ax.text(x, y, text, fontsize=8.5, color=color, ha=ha, va="bottom", fontweight="bold", zorder=5,
            bbox=dict(fc="white", ec="none", pad=0.5, alpha=0.9))


# ------------------------------------------------------------------ title
ax.text(12, 15.8, "1999 Toyota RAV4 Transmission Controller - system wiring diagram", fontsize=20,
        fontweight="bold", ha="center", va="top")
ax.text(12, 15.3, "W-numbers match the wire list in docs/03-wiring-guide.md.  Car-side terminal names "
        "(S1, SPD, VTA ...) are Toyota's; confirm every wire with the tests in the guide before cutting or tapping.",
        fontsize=11, ha="center", va="top", color="#444444")

zone(0.2, 0.3, 7.0, 14.6, "1999 RAV4 (factory)", "#f6f1e7")
zone(7.4, 0.3, 10.2, 14.6, "TCU box (under the passenger dash)", "#eaf2fb")
zone(17.9, 0.3, 5.9, 14.6, "Dash pod & shifter", "#ecf7ea")

# ------------------------------------------------------------------ TCU connectors
CX = 7.7
X1 = connector(CX, 13.86, "X1 power", ["+12 DRV", "+12 LOG", "GND"])
X3 = connector(CX, 10.64, "X3 signals", ["D", "STP", "E1", "SPD", "TAC", "VTA", "E2"])
X2 = connector(CX, 7.60, "X2 solenoids", ["S1 ECU", "S2 ECU", "SL ECU", "S1 SOL", "S2 SOL", "SL SOL"])
X6 = connector(CX, 1.76, "X6 ATF", ["+5V", "GND", "DQ"])

# ------------------------------------------------------------------ car side
box(0.5, 13.2, 2.9, 1.25, "Body ground bolt", ["near the ECM, bare", "metal"])
box(3.6, 13.0, 3.1, 1.45, "Interior fuse box", ["add-a-fuse on an IG-", "only fuse (12 V only", "with key ON)"])
box(0.5, 11.5, 2.9, 1.25, "Stop-lamp switch", ["STP: +12 V when", "pedal pressed"])
box(3.6, 11.5, 3.1, 1.25, "Park/neutral switch", ["'D' output: +12 V", "only in D"])
box(0.5, 5.2, 4.4, 5.9, "Factory ECM",
    ["engine + transmission", "computer", "", "Signals - TAP only:", "  E1 SPD TAC VTA E2", "",
     "Solenoid outputs -", "CUT & intercept:", "  S1  S2  SL", "", "(location & pins:", " repair manual)"])
box(0.5, 2.5, 6.2, 1.8, "Transaxle solenoids  S1  S2  SL",
    ["single wire each, grounded through the case", "(trans. connector: 4WD B3/B6/B5,",
     " 2WD A3/A1/A2 - VERIFY)"])
box(0.5, 0.6, 6.2, 1.5, "ATF cooler line (engine bay)",
    ["barbed tee + DS18B20 probe on the hose", "from the transaxle to the radiator"])

ECM_X = 4.9
ecm_terms = ["E1", "SPD", "TAC", "VTA", "E2"]
for name in ecm_terms + ["S1", "S2", "SL"]:
    pin = name if name in X3 else name + " ECU"
    y = (X3 if name in X3 else X2)[pin][1]
    ax.text(ECM_X - 0.1, y, name, fontsize=9, ha="right", va="center", fontweight="bold", zorder=3)

# power & ground (W1-W3)
wire([(6.7, X1["+12 DRV"][1]), X1["+12 DRV"][:2]], C["power"])
label(7.2, X1["+12 DRV"][1], "W1 5A", C["power"])
wire([(6.7, X1["+12 LOG"][1]), X1["+12 LOG"][:2]], C["power"])
label(7.2, X1["+12 LOG"][1], "W2 2A", C["power"])
wire([(1.95, 13.2), (1.95, 12.92), (7.35, 12.92), (7.35, X1["GND"][1]), X1["GND"][:2]], C["ground"])
label(4.5, 12.92, "W3", C["ground"])

# lever D & brake (W10, W11)
wire([(6.2, 11.5), (6.2, 11.33), (7.4, 11.33), (7.4, X3["D"][1]), X3["D"][:2]], C["sig"], lw=1.8)
label(6.8, 11.33, "W10", C["sig"])
wire([(1.95, 11.5), (1.95, 11.2), (7.2, 11.2), (7.2, X3["STP"][1]), X3["STP"][:2]], C["sig"], lw=1.8)
label(3.5, 11.2, "W11", C["sig"])

# ECM taps (W12-W16), straight across
for name, w in zip(ecm_terms, ["W12", "W13", "W14", "W15", "W16"]):
    y = X3[name][1]
    wire([(ECM_X, y), (X3[name][0], y)], C["ground"] if name == "E1" else C["sig"], lw=1.8)
    label(6.2, y, w, C["ground"] if name == "E1" else C["sig"])

# solenoid intercept: ECU side (W4-W6) straight across
for name, w in zip(["S1", "S2", "SL"], ["W4", "W5", "W6"]):
    y = X2[name + " ECU"][1]
    wire([(ECM_X, y), (X2[name + " ECU"][0], y)], C["ecu"], lw=2.4)
    label(6.2, y, w, C["ecu"])
label(5.55, X2["S1 ECU"][1] + 0.22, "cut each wire here: X2 halves", C["ecu"])

# solenoid side (W7-W9) from below, non-crossing routing
for name, sx, corr, vx, w in [("S1", 1.6, 4.8, 7.05, "W7"), ("S2", 3.3, 4.65, 7.2, "W8"),
                              ("SL", 5.0, 4.5, 7.35, "W9")]:
    y = X2[name + " SOL"][1]
    wire([(sx, 4.3), (sx, corr), (vx, corr), (vx, y), (X2[name + " SOL"][0], y)], C["sol"], lw=2.4)
    label(sx + 0.55, corr, w, C["sol"])

# ATF probe (W17-W19)
for name, w in zip(["+5V", "GND", "DQ"], ["W17", "W18", "W19"]):
    y = X6[name][1]
    wire([(6.7, y), (X6[name][0], y)], C["probe"], lw=1.6)
    label(7.2, y, w, C["probe"])

# ------------------------------------------------------------------ TCU internals
BX = 8.7
box(BX, 12.3, 3.9, 2.2, "Power",
    ["X1-1 -> TVS -> driver +12 V", "X1-2 -> rev. diode -> TVS", "  -> 5 V/3 A buck -> Mega 5V,",
     "     screen, gyro, probe", "X1-3 -> GND star point", "J5 -> ON/OFF pole 2 -> coils"], line_size=8.5)
box(BX, 8.15, 3.9, 3.85, "Input conditioning",
    ["D   -> PC817  -> D28", "STP -> PC817  -> D27", "E1  -> signal GND", "SPD -> LM393  -> D2",
     "TAC -> PC817  -> D3", "VTA -> 10k/100nF -> A0", "E2  -> 10k/100nF -> A1", "+12 -> 47k/10k -> A3",
     "ECU S1/S2/SL -> PC817", "            -> D29-31"], line_size=8.5)
box(BX, 3.2, 3.9, 4.65, "Solenoid channel x3",
    ["K1-K3  DPDT relay, 12 V", "Q1-Q3  P-MOSFET drivers", "R1-R3  15 ohm 50 W dummy", "ULN2803 drives coils;", "coil +12 via ON/OFF pole 2",
     "", "coil OFF: ECU -> solenoid", "coil ON : ECU -> dummy,", "          U -> solenoid", "",
     "(Detail A in the guide)"], line_size=8.5)
box(BX, 0.6, 3.9, 2.25, "ATF probe input",
    ["DQ -> 100 ohm -> D33", "4.7k pull-up DQ -> 5V", "5.1 V TVS to GND"], line_size=8.5)
box(13.1, 0.6, 2.7, 2.3, "MPU-6050 gyro", ["GY-521 board,", "on the logic board", "I2C addr 0x68"], line_size=8.5)

mega_pins = ["D2  VSS (int)", "D3  tach (int)", "D6  relay drv", "D8  S1 drv",
             "D9  S2 drv", "D10 SL drv", "D20 SDA", "D21 SCL", "D22 shift up", "D23 shift dn", "D24 ECO",
             "D25 SPORT", "D26 ON/OFF", "D27 brake", "D28 lever D", "D29 ECU S1", "D30 ECU S2",
             "D31 ECU SL", "D32 TEST", "D33 ATF DQ", "D34 MANUAL", "A0  TPS (VTA)", "A1  TPS ref (E2)", "A3  battery", "A4  S1 amps",
             "A5  S2 amps", "A6  SL amps", "A7  coil power on?", "5V / GND", "USB -> laptop"]
box(13.1, 3.3, 4.2, 11.2, "Arduino Mega 2560", mega_pins, fc="#fdfde8", line_size=9.5)

# connector stubs into the blocks
for anchors, color_of in [(X1, lambda p: C["ground"] if p == "GND" else C["power"]),
                          (X3, lambda p: C["ground"] if p == "E1" else C["sig"]),
                          (X2, lambda p: C["sol"] if "SOL" in p else C["ecu"]),
                          (X6, lambda p: C["probe"])]:
    for p, (lx, y, rx) in anchors.items():
        wire([(rx, y), (BX, y)], color_of(p), lw=1.5)

# block -> Mega links
for y, text, color in [(13.4, "5 V", C["power"]), (10.0, "D2 D3 D27 D28 A0 A1 A3", C["sig"]),
                       (9.0, "D29-31", C["internal"]), (6.6, "D6, D8-10", C["internal"]),
                       (5.6, "A4-A7", C["internal"])]:
    wire([(BX + 3.9, y), (13.1, y)], color, lw=2)
    label(12.85, y, text, color)
wire([(BX + 3.9, 1.7), (12.85, 1.7), (12.85, 3.6), (13.1, 3.6)], C["probe"], lw=2)
label(12.85, 2.6, "D33", C["probe"])
wire([(14.4, 2.9), (14.4, 3.3)], C["i2c"], lw=2)

# ------------------------------------------------------------------ dash pod & shifter
X4 = connector(18.0, 13.9, "X4 dash", ["ECO", "SPORT", "MANUAL", "TEST", "ON/OFF", "GND"])
X7 = connector(18.0, 8.3, "X7 shifter", ["UP", "DOWN", "GND"])
X5 = connector(18.0, 5.5, "X5 screen", ["5V", "GND", "SDA", "SCL"])
wire([(17.3, 11.9), (18.0, 11.9)], C["console"], lw=2)
label(17.62, 11.92, "D24-26,32,34", C["console"])
wire([(17.3, 7.9), (18.0, 7.9)], C["console"], lw=2)
label(17.65, 7.92, "D22 D23", C["console"])
wire([(17.3, 5.0), (18.0, 5.0)], C["i2c"], lw=2)
label(17.65, 5.02, "D20 D21", C["i2c"])

box(19.7, 9.85, 3.9, 4.4, "Dash pod: switches",
    ["ECO     toggle (SPST)", "SPORT   toggle (SPST)", "MANUAL  toggle (SPST)", "TEST    push button",
     "ON/OFF  2-pole toggle", "  + cover: pole 1 -> D26,", "  pole 2 -> relay coils", "exactly one of ECO,", "SPORT, MANUAL on", "all switch to GND"],
    line_size=9)
for i, n in enumerate(["ECO", "SPORT", "MANUAL", "TEST", "ON/OFF", "GND"]):
    _, y, rx = X4[n]
    color = C["ground"] if n == "GND" else C["console"]
    wire([(rx, y), (19.7, y)], color, lw=1.8)
    label(19.15, y, "C%d %s" % (i + 1, n), color)
ax.text(18.85, 11.15, "W20: 8-core shielded\n(C7, C8: ON/OFF pole 2\n to J5, relay power)", fontsize=8.5,
        color=C["console"], ha="center", va="top", fontweight="bold")

box(19.7, 6.85, 3.9, 1.75, "Shifter base", ["sequential lever,", "two microswitches (NO)", "COM -> GND"],
    line_size=9)
for n in ["UP", "DOWN", "GND"]:
    _, y, rx = X7[n]
    color = C["ground"] if n == "GND" else C["console"]
    wire([(rx, y), (19.7, y)], color, lw=1.8)
    label(19.15, y, n, color)
ax.text(18.85, 7.15, "W22: 3-core\nshielded", fontsize=8.5, color=C["console"], ha="center",
        va="top", fontweight="bold")

box(19.7, 3.65, 3.9, 2.2, "Dash pod: screen", ["2.42\" SSD1309 OLED,", "128x64, I2C 0x3C"],
    line_size=9)
for n in ["5V", "GND", "SDA", "SCL"]:
    _, y, rx = X5[n]
    color = C["power"] if n == "5V" else (C["ground"] if n == "GND" else C["i2c"])
    wire([(rx, y), (19.7, y)], color, lw=1.8)
    label(19.15, y, n, color)
ax.text(18.85, 3.75, "W21: 4-core shielded,\n2.5 m max", fontsize=8.5, color=C["i2c"], ha="center",
        va="top", fontweight="bold")

# ------------------------------------------------------------------ legend
lx, ly = 18.2, 2.55
ax.text(lx, ly, "Wire colours (suggested)", fontsize=10, fontweight="bold", va="top")
legend = [("power", "+12 V / +5 V (red)"), ("ground", "ground (black)"),
          ("ecu", "ECU side of S1/S2/SL (yellow)"), ("sol", "solenoid side of S1/S2/SL (orange)"),
          ("sig", "signal taps (blue)"), ("console", "dash & shifter (green)"), ("i2c", "I2C (purple)"),
          ("probe", "ATF probe (brown)")]
for i, (k, t) in enumerate(legend):
    col, row = i % 2, i // 2
    xx, yy = lx + col * 2.85, ly - 0.42 - row * 0.4
    ax.plot([xx, xx + 0.35], [yy, yy], color=C[k], lw=3)
    ax.text(xx + 0.42, yy, t, fontsize=8, va="center")

fig.savefig(OUT + ".svg")
fig.savefig(OUT + ".png", dpi=90)
print("wrote", OUT + ".svg", "and .png")


# ====================================================================== Detail A
def detail_a():
    """One solenoid channel (S1 shown; S2 and SL are identical)."""
    f = plt.figure(figsize=(20, 11))
    a = f.add_axes([0, 0, 1, 1])
    a.set_xlim(0, 20)
    a.set_ylim(0, 11)
    a.axis("off")

    def ln(pts, color="#222222", lw=2.2, ls="-"):
        xs, ys = zip(*pts)
        a.plot(xs, ys, color=color, lw=lw, ls=ls, solid_capstyle="round", zorder=2)

    def dot(x, y, color="#222222"):
        a.plot(x, y, "o", color=color, ms=7, zorder=4)

    def txt(x, y, t, size=10, ha="left", va="center", bold=False, color="#222222", mono=False):
        a.text(x, y, t, fontsize=size, ha=ha, va=va, fontweight="bold" if bold else "normal", color=color,
               family="DejaVu Sans Mono" if mono else "DejaVu Sans", zorder=5)

    def blk(x, y, w, h, title, lines=(), fc="white", ls="-"):
        a.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.08", fc=fc,
                                   ec="#333333", lw=1.4, ls=ls, zorder=1))
        txt(x + 0.12, y + h - 0.22, title, 11, bold=True)
        for i, l in enumerate(lines):
            txt(x + 0.12, y + h - 0.55 - i * 0.28, l, 9, mono=True)

    def resistor_v(x, y_top, y_bot, name):
        zig = [(x, y_top), (x, y_top - 0.15)]
        n_z = 6
        span = (y_top - y_bot - 0.3) / n_z
        for k in range(n_z):
            zig.append((x + (0.15 if k % 2 == 0 else -0.15), y_top - 0.15 - (k + 0.5) * span))
        zig += [(x, y_bot + 0.15), (x, y_bot)]
        ln(zig, lw=1.8)
        txt(x + 0.25, (y_top + y_bot) / 2, name, 9)

    def ground(x, y):
        ln([(x, y), (x, y - 0.15)], lw=1.8)
        for k, w in enumerate([0.3, 0.2, 0.1]):
            ln([(x - w, y - 0.15 - k * 0.08), (x + w, y - 0.15 - k * 0.08)], lw=1.8)

    a.text(10, 10.75, "Detail A - one solenoid channel (S1 shown; S2 and SL are identical)", fontsize=18,
           fontweight="bold", ha="center", va="top")
    a.text(10, 10.3, "Relay drawn DE-ENERGISED (coil off) = factory control. Energised, both poles flip: "
           "ECM -> dummy load, TCU driver -> solenoid.", fontsize=11, ha="center", va="top", color="#444444")

    # ECM output and X2 entry
    blk(0.3, 6.1, 2.4, 1.1, "Factory ECM", ["S1 output", "(+12 V when on)"], fc="#f6f1e7")
    ln([(2.7, 6.6), (4.6, 6.6)], C["ecu"])
    txt(3.0, 6.78, "W4 -> X2-1", 9, color=C["ecu"], bold=True)
    dot(4.6, 6.6, C["ecu"])
    txt(4.45, 6.35, "N1 (ECU side)", 9, ha="right", color=C["ecu"])

    # relay outline
    a.add_patch(FancyBboxPatch((5.4, 2.3), 3.2, 5.9, boxstyle="round,pad=0.02", fc="#f4f4f4", ec="#666666",
                               lw=1.4, ls="--", zorder=0))
    txt(5.5, 8.0, "K1  DPDT relay, 12 V coil", 10.5, bold=True)
    txt(5.5, 7.7, "(e.g. Omron G2RL-2-DC12)", 9)
    # pole A
    ln([(4.6, 6.6), (5.9, 6.6)], C["ecu"])
    dot(5.9, 6.6)
    ln([(5.9, 6.6), (7.6, 7.15)], lw=2.6)
    dot(7.8, 7.2)
    dot(7.8, 6.0)
    txt(5.95, 6.35, "COM-A", 9)
    txt(7.75, 7.45, "NC-A", 9)
    txt(7.75, 5.72, "NO-A", 9)
    # pole B
    dot(5.9, 3.6)
    ln([(5.9, 3.6), (7.6, 4.15)], lw=2.6)
    dot(7.8, 4.2)
    dot(7.8, 3.0)
    txt(5.95, 3.35, "COM-B", 9)
    txt(7.75, 4.45, "NC-B (open)", 9)
    txt(7.75, 2.72, "NO-B", 9)
    ln([(6.75, 6.85), (6.75, 3.9)], "#888888", 1.2, ls=":")
    txt(6.55, 5.3, "poles move\ntogether", 8.5, ha="right", color="#666666")

    # solenoid line N2
    ln([(7.8, 7.2), (10.0, 7.2), (10.0, 3.0), (7.8, 3.0)], C["sol"])
    dot(10.0, 5.1, C["sol"])
    txt(10.15, 5.35, "N2 (solenoid side)", 9, color=C["sol"])
    ln([(10.0, 5.1), (12.2, 5.1)], C["sol"])
    txt(10.4, 4.85, "X2-4 -> W7", 9, color=C["sol"], bold=True)
    blk(12.2, 4.4, 3.0, 1.4, "Transaxle S1 solenoid", ["11-15 ohm, single wire,", "case = ground"],
        fc="#f6f1e7")
    ground(13.7, 4.4)

    # dummy load on NO-A
    ln([(7.8, 6.0), (8.9, 6.0)])
    resistor_v(8.9, 6.0, 4.9, "R1 15 ohm\n50 W, on\nheatsink")
    ground(8.9, 4.9)

    # high-side driver
    blk(2.3, 2.6, 2.6, 1.9, "Q1 high-side driver", ["IRF4905 P-MOSFET", "D8 -> ULN2803 -> gate", "clamp UF4007 + 24 V Z"])
    ln([(4.9, 3.6), (5.9, 3.6)], C["power"])
    txt(5.0, 3.8, "OUT", 9, color=C["power"])
    ln([(3.6, 4.5), (3.6, 5.2), (1.0, 5.2)], C["power"])
    txt(1.0, 5.4, "+12 V DRV (X1-1, 5 A fuse) -> CS1 ACS712 ->", 9, color=C["power"])
    ln([(2.3, 3.4), (0.9, 3.4)], C["internal"])
    txt(0.9, 3.6, "D8 (S1 drv)", 9, bold=True)
    resistor_v(1.4, 3.4, 2.5, "10k")
    ground(1.4, 2.5)
    txt(1.6, 2.15, "pull-down: off during reset", 8.5, color="#666666")
    txt(2.3, 1.9, "CS1 (ACS712-05B, in the +12 V feed) OUT -> A4: S1 amps", 9, bold=True)
    txt(2.3, 1.45, "185 mV/A around 2.5 V; zero re-learned with the relays released", 8.5, color="#666666")

    # ECU-side sense opto
    ln([(4.6, 6.6), (4.6, 8.5), (3.6, 8.5)], C["ecu"])
    blk(0.3, 8.0, 3.3, 1.7, "ECU sense (PC817)", ["N1 -> 3.3k -> LED -> GND", "transistor: D29 -> GND",
                                                    "D29 LOW = ECU wants S1 on"])

    # coils / ULN2803
    blk(10.3, 7.0, 4.6, 2.6, "Relay coils (all 3 together)",
        ["+12 DRV -> F2 -> J5a -> ON/OFF", "  pole 2 -> J5b -> K1-K3 coils", "  -> ULN2803 OUT1-3 (IN <- D6,",
         "  10k pull-down); COM + DF", "  = flyback. 47k/10k -> A7:", "  coil power seen by the Mega"])

    # bypass plug & fail-safe
    blk(15.4, 7.0, 4.3, 2.6, "X2 bypass plug",
        ["A mating plug with jumpers", "1-4, 2-5, 3-6.", "Unplug the TCU, plug this in:", "ECM -> solenoids directly,",
         "the car is 100 % stock."], fc="#fff7e6")
    blk(15.4, 2.6, 4.3, 3.9, "Fail-safe",
        ["Power loss / reset / hang,", "or ON/OFF switched off:", "coils off -> ECM drives the", "solenoid, Q1 is disconnected.",
         "", "Coil on: ECM sees R1 (no", "trouble code), Q1 drives", "the solenoid, A4 checks", "its current."],
        fc="#eef8ee")
    blk(10.3, 0.4, 9.4, 1.8, "Wire sizes",
        ["Solenoid path (W4-W9, relay contacts, Q1 drain): 18 AWG", "+12 V DRV feed: 16 AWG.  R1 leads: 18 AWG, keep away from loom",
         "Signals (D8, sensor OUT, opto): 22-24 AWG"])

    f.savefig(os.path.join(ROOT, "docs", "wiring-detail-a.svg"))
    f.savefig(os.path.join(ROOT, "docs", "wiring-detail-a.png"), dpi=90)
    print("wrote docs/wiring-detail-a.svg and .png")


detail_a()
