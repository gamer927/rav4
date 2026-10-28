#!/usr/bin/env python3
"""Draws docs/cable-map.png: the whole system on one page, as cables.

Every cable has its own hole in the TCU box, its own name and its own plug,
so the car side is eight separate, simple jobs. Run: python3 tools/cable_map.py
"""
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# [gland, colour, destination card, what's in the cable]
CABLES = [
    ("SHIFT", "#2ca02c", 0, "3 wires: up, down, ground"),
    ("DASH", "#2ca02c", 1, "8 wires: 5 switches, ground, relay-power pair"),
    ("SCREEN", "#9467bd", 1, "4 wires: power, ground, 2 data"),
    ("ATF", "#8c564b", 2, "3 wires: power, ground, data"),
    ("SIG", "#1f77b4", 3, "7 thin wires: listening taps"),
    ("PWR", "#d62728", 4, "3 wires: 12 V, 12 V, ground"),
    ("SOL", "#ff7f0e", 5, "6 wires: both ends of 3 cut wires"),
    ("LOAD", "#7f7f7f", 6, "4 wires: 3 resistors + ground"),
]
CARDS = [
    ("Shifter base", "centre console, next to the gear lever"),
    ("Dash pod", "dash top, driver's side: the screen and 5 switches"),
    ("Temp probe", "engine bay, in the ATF cooler hose"),
    ("Engine computer", "taps on 7 of its wires: nothing is cut"),
    ("Fuse box", "key-on fuse tap, plus a body ground bolt"),
    ("Solenoid wires", "S1, S2, SL: the only 3 wires that get cut"),
    ("Heatsink", "3 dummy resistors, under the dash in moving air"),
]


def main():
    n = len(CABLES)
    fig, ax = plt.subplots(figsize=(15, 9.4), dpi=110)
    ax.set_xlim(0, 15)
    ax.set_ylim(0, 9.4)
    ax.axis("off")
    ax.text(7.5, 9.25, "The whole system: 8 cables, each into its own labelled hole", ha="center", va="top",
            fontsize=20, fontweight="bold")
    ax.text(7.5, 8.7, "Each cable has its own name, its own hole in the box and its own plug. "
            "Connecting the car is 8 separate jobs, one at a time.", ha="center", va="top", fontsize=12,
            color="#444")
    top, step = 7.55, 0.9
    ys = [top - i * step for i in range(n)]

    # The box on the left, with its 8 glands down its right-hand side.
    bx0, bx1 = 0.3, 3.2
    ax.add_patch(FancyBboxPatch((bx0, ys[-1] - 0.55), bx1 - bx0, ys[0] - ys[-1] + 1.1, boxstyle="round,pad=0.05",
                                fc="#e3eefa", ec="#1f3b5c", lw=2.5))
    ax.text((bx0 + bx1) / 2 - 0.25, (ys[0] + ys[-1]) / 2 + 0.6, "TCU\nbox", ha="center", va="center",
            fontsize=22, fontweight="bold", color="#1f3b5c")
    ax.text((bx0 + bx1) / 2 - 0.25, (ys[0] + ys[-1]) / 2 - 0.75,
            "under the\npassenger side\nof the dash", ha="center", va="center", fontsize=10.5, color="#1f3b5c")

    card_x0, card_x1 = 9.6, 14.8
    for c, (title, sub) in enumerate(CARDS):
        rows = [ys[i] for i, cab in enumerate(CABLES) if cab[2] == c]
        y0, y1 = min(rows) - 0.36, max(rows) + 0.36
        ax.add_patch(FancyBboxPatch((card_x0, y0), card_x1 - card_x0, y1 - y0, boxstyle="round,pad=0.03",
                                    fc="white", ec="#333", lw=1.5))
        yc = (y0 + y1) / 2
        ax.text(card_x0 + 0.2, yc + 0.13, title, ha="left", va="center", fontsize=12.5, fontweight="bold")
        ax.text(card_x0 + 0.2, yc - 0.18, sub, ha="left", va="center", fontsize=9.5, color="#333")

    for i, (gland, colour, card, what) in enumerate(CABLES):
        y = ys[i]
        ax.plot([bx1, card_x0], [y, y], color=colour, lw=7, solid_capstyle="butt", zorder=1)
        ax.add_patch(plt.Circle((bx1, y), 0.17, fc="#555", ec="#222", zorder=3))
        ax.text(bx1 - 0.3, y, gland, ha="right", va="center", fontsize=11, fontweight="bold", color="#1f3b5c")
        ax.text(6.4, y, what, ha="center", va="center", fontsize=11, zorder=4,
                bbox=dict(boxstyle="round,pad=0.3", fc="white", ec=colour, lw=1.6))

    ax.text(7.5, 0.25, "Inside the box (once the boards are built, build book parts 4-6), each cable's wires "
            "screw into its own row of labelled terminals, next to its hole.", ha="center", va="center",
            fontsize=11.5, color="#1f3b5c")
    fig.savefig(os.path.join(ROOT, "docs", "cable-map.png"), bbox_inches="tight", facecolor="white")
    print("wrote docs/cable-map.png")


if __name__ == "__main__":
    main()
