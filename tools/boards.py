#!/usr/bin/env python3
"""Board layouts for the TCU: one source of truth for the build book.

Defines the two perfboards (power board, logic board) part by part on a
0.1" (2.54 mm) grid, plus every wire, grouped into build steps. Running it:

  * checks that no two parts overlap, every part is on its board, and every
    wire lands on a real pin (it fails loudly otherwise)
  * draws each build step, top view (where parts go) and bottom view (where
    wires go, mirrored as you see it when you flip the board), into
    docs/build/
  * draws a 1:1 printable placement template per board (SVG)
  * writes docs/build/board-steps.md (parts and wires for each step) and
    fills them into the build book (tools/build_book_template.md ->
    docs/05-build-book.md)
  * writes hardware/board_envelopes.scad (every part's footprint and height)
    for the 3D fit check in hardware/tcu_enclosure.scad

  python3 tools/boards.py
"""
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyBboxPatch, Rectangle
from PIL import Image

P = 2.54  # grid pitch, mm
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "docs", "build")

# --------------------------------------------------------------------------
# Footprints: pins as (name, dcol, drow) from pin 1, body as a rectangle in
# grid units (c0, r0, c1, r1) relative to pin 1, height in mm above the board.
# "module" parts sit on foam tape: their pins are wire pads, not board holes.


def fp_r():
    return dict(pins=[("1", 0, 0), ("2", 4, 0)], body=(0.7, -0.5, 3.3, 0.5), h=3, kind="R")


def fp_d35():  # DO-35 glass diode: 1N4148, BAT85
    return dict(pins=[("A", 0, 0), ("K", 3, 0)], body=(0.6, -0.4, 2.4, 0.4), h=2, kind="D")


def fp_d41():  # DO-41: 1N47xx zener, UF4007
    return dict(pins=[("A", 0, 0), ("K", 4, 0)], body=(0.95, -0.55, 3.05, 0.55), h=3, kind="D")


def fp_d201():  # DO-201: 1N5822, 1.5KE TVS
    return dict(pins=[("A", 0, 0), ("K", 6, 0)], body=(1.1, -1.05, 4.9, 1.05), h=6, kind="D")


def fp_c():
    return dict(pins=[("1", 0, 0), ("2", 2, 0)], body=(-0.4, -0.5, 2.4, 0.5), h=6, kind="C")


def fp_ce8():  # 8 mm electrolytic, legs bent to 2.54 mm
    return dict(pins=[("+", 0, 0), ("-", 1, 0)], body=(-1.1, -1.6, 2.1, 1.6), h=14, kind="CE")


def fp_dip(n):
    half = n // 2
    pins = [(str(i + 1), 0, -i) for i in range(half)]
    pins += [(str(half + i + 1), 3, -(half - 1) + i) for i in range(half)]
    return dict(pins=pins, body=(0.4, -(half - 1) - 0.5, 2.6, 0.5), h=5, kind="IC")


def fp_tb():  # one position of a 5.08 mm screw terminal; wire enters at -row
    return dict(pins=[("1", 0, 0)], body=(-1.0, -1.6, 1.0, 1.4), h=10, kind="TB")


def fp_g2rl():
    # Omron G2RL-2 (KiCad Relay_DPDT_Omron_G2RL): coil at one end, contacts at
    # 15 / 20 / 25 mm from it, rows 7.5 mm apart (on 0.1" grid: 6 / 8 / 10 and
    # 3 holes; the pins bend the last 0.1-0.4 mm). Contact names are the usual
    # Omron arrangement; the build book confirms them with a beep test first.
    pins = [("COIL1", 0, 0), ("COIL2", 3, 0),
            ("NC_A", 0, -6), ("COM_A", 0, -8), ("NO_A", 0, -10),
            ("NC_B", 3, -6), ("COM_B", 3, -8), ("NO_B", 3, -10)]
    return dict(pins=pins, body=(-1.0, 0.9, 4.0, -10.45), h=15.7, kind="K")


def fp_to220():  # standing, tab to +row
    return dict(pins=[("G", 0, 0), ("D", 1, 0), ("S", 2, 0)], body=(-1.0, -0.4, 3.0, 1.4), h=19,
                kind="Q")


def fp_acs712():  # 31 x 13 mm module on foam tape, terminals at -row end
    return dict(pins=[("IP+", -1, 0), ("IP-", 1, 0), ("VCC", -1, 11), ("OUT", 0, 11), ("GND", 1, 11)],
                body=(-2.5, -1.2, 2.5, 11.0), h=15, kind="MOD", module=True)


def fp_buck():  # LM2596HV module 43 x 21 mm on foam tape, lying along +col
    return dict(pins=[("IN+", 0, 0), ("IN-", 0, 6), ("OUT+", 15, 0), ("OUT-", 15, 6)],
                body=(-0.8, -1.2, 16.0, 7.2), h=15, kind="MOD", module=True)


def fp_boxhdr(n):  # 2-row box header, pin 1 at (0,0), odd pins col 0, even col 1
    rows = n // 2
    pins = []
    for i in range(rows):
        pins.append((str(2 * i + 1), 0, -i))
        pins.append((str(2 * i + 2), 1, -i))
    return dict(pins=pins, body=(-1.25, -(rows - 1) - 1.5, 2.25, 1.5), h=18, kind="J")


def fp_gy521():  # header along the bottom edge; 21 x 16 mm board
    names = ["VCC", "GND", "SCL", "SDA", "XDA", "XCL", "AD0", "INT"]
    return dict(pins=[(n, i, 0) for i, n in enumerate(names)], body=(-0.6, -0.5, 7.7, 5.8), h=7,
                kind="MOD")


def fp_led_hdr(n):  # plain pin header (unused here, kept for symmetry)
    return dict(pins=[(str(i + 1), i, 0) for i in range(n)], body=(-0.5, -0.5, n - 0.5, 0.5), h=9,
                kind="J")


# --------------------------------------------------------------------------
class Part:
    def __init__(self, ref, fp, col, row, rot=0, value="", step=None, note=""):
        self.ref, self.fp, self.col, self.row, self.rot = ref, fp, col, row, rot
        self.value, self.step, self.note = value, step, note

    def _rot(self, dc, dr):
        for _ in range((self.rot // 90) % 4):
            dc, dr = -dr, dc
        return dc, dr

    def pins(self):
        out = {}
        for name, dc, dr in self.fp["pins"]:
            rc, rr = self._rot(dc, dr)
            out[name] = (self.col + rc, self.row + rr)
        return out

    def body(self):
        c0, r0, c1, r1 = self.fp["body"]
        pts = [self._rot(c, r) for c, r in ((c0, r0), (c1, r1))]
        xs = sorted(self.col + p[0] for p in pts)
        ys = sorted(self.row + p[1] for p in pts)
        return xs[0], ys[0], xs[1], ys[1]


class Board:
    def __init__(self, name, title, size, holes):
        self.name, self.title, self.size, self.holes = name, title, size, holes
        # centre the hole grid on the board
        self.x0 = (size[0] - (holes[0] - 1) * P) / 2
        self.y0 = (size[1] - (holes[1] - 1) * P) / 2
        self.parts, self.wires, self.steps = [], [], []

    def mm(self, c, r):
        return self.x0 + c * P, self.y0 + r * P

    def add(self, *parts):
        self.parts.extend(parts)

    def wire(self, step, net, *nodes, gauge="22"):
        self.wires.append(dict(step=step, net=net, nodes=list(nodes), gauge=gauge))

    def part(self, ref):
        for p in self.parts:
            if p.ref == ref:
                return p
        raise KeyError(f"{self.name}: no part {ref}")

    def pin(self, node):
        ref, name = node.split(".", 1)
        pins = self.part(ref).pins()
        if name not in pins:
            raise KeyError(f"{self.name}: {ref} has no pin {name} (has {list(pins)})")
        return pins[name]

    # ----------------------------------------------------------- checks
    def check(self):
        errors = []
        for p in self.parts:
            c0, r0, c1, r1 = p.body()
            x0, y0 = self.mm(c0, r0)
            x1, y1 = self.mm(c1, r1)
            if x0 < -0.01 or y0 < -0.01 or x1 > self.size[0] + 0.01 or y1 > self.size[1] + 0.01:
                errors.append(f"{p.ref} body off the board ({x0:.1f},{y0:.1f})-({x1:.1f},{y1:.1f})")
            if not p.fp.get("module"):
                for name, (c, r) in p.pins().items():
                    if not (0 <= c < self.holes[0] and 0 <= r < self.holes[1]):
                        errors.append(f"{p.ref}.{name} not on a hole ({c},{r})")
        for i, a in enumerate(self.parts):
            ab = a.body()
            for b in self.parts[i + 1:]:
                bb = b.body()
                if ab[0] < bb[2] - 0.05 and bb[0] < ab[2] - 0.05 and ab[1] < bb[3] - 0.05 and bb[1] < ab[3] - 0.05:
                    errors.append(f"{a.ref} overlaps {b.ref}")
        holes = {}
        for p in self.parts:
            if p.fp.get("module"):
                continue
            for name, cr in p.pins().items():
                if cr in holes:
                    errors.append(f"{p.ref}.{name} shares hole {cr} with {holes[cr]}")
                holes[cr] = f"{p.ref}.{name}"
        used = {}
        for w in self.wires:
            for n in w["nodes"]:
                try:
                    self.pin(n)
                except KeyError as e:
                    errors.append(str(e))
                if n in used and used[n] != w["net"]:
                    errors.append(f"{n} is on nets {used[n]} and {w['net']}")
                used[n] = w["net"]
        for p in self.parts:
            if p.step is None:
                errors.append(f"{p.ref} has no build step")
            if p.fp["kind"] == "D":  # the band note must say where the cathode really is
                (ac, ar), (kc, kr) = p.pins()["A"], p.pins()["K"]
                side = "back" if kr > ar else "front" if kr < ar else "right" if kc > ac else "left"
                if f"(K) {side}" not in p.note and f"(K) towards the {side}" not in p.note:
                    errors.append(f"{p.ref}: note '{p.note}' but the band (K) is to the {side}")
        return errors

    # ----------------------------------------------------------- drawing
    def draw(self, ax, step_idx=None, bottom=False, template=False):
        W, H = self.size
        ax.set_xlim(-3, W + 3)
        ax.set_ylim(-3, H + 3)
        ax.set_aspect("equal")
        ax.axis("off")
        if bottom:
            ax.invert_xaxis()
        ax.add_patch(FancyBboxPatch((0, 0), W, H, boxstyle="round,pad=0,rounding_size=2",
                                    fc="white" if template else "#e9f2df", ec="#55703a", lw=1.2))
        hole_c = "#9a9a9a" if template else "#c9b77d"
        hx = [self.mm(c, r)[0] for c in range(self.holes[0]) for r in range(self.holes[1])]
        hy = [self.mm(c, r)[1] for c in range(self.holes[0]) for r in range(self.holes[1])]
        if template:  # one path collection keeps the SVG small; 0.9 mm dots at 1:1
            ax.scatter(hx, hy, s=(0.9 / 25.4 * 72) ** 2, c=hole_c, linewidths=0)
        else:
            for x, y in zip(hx, hy):
                ax.add_patch(Circle((x, y), 0.45, fc=hole_c, ec="none"))
        # grid labels every 5 holes
        for c in range(0, self.holes[0], 5):
            x, _ = self.mm(c, 0)
            ax.text(x, -1.6, str(c), ha="center", va="center", fontsize=4.5, color="#555")
        for r in range(0, self.holes[1], 5):
            _, y = self.mm(0, r)
            ax.text(-1.8 if not bottom else W + 1.8, y, str(r), ha="center", va="center",
                    fontsize=4.5, color="#555")
        order = {s: i for i, s in enumerate(self.steps)}
        cur = None if step_idx is None else self.steps[step_idx]
        for p in self.parts:
            ps = order[p.step]
            if step_idx is not None and ps > step_idx:
                continue
            new = cur is not None and p.step == cur
            c0, r0, c1, r1 = p.body()
            x0, y0 = self.mm(c0, r0)
            x1, y1 = self.mm(c1, r1)
            if template:
                fc, ec, alpha = "none", "#222", 1.0
            elif bottom:
                fc, ec, alpha = "#dddddd", "#bbbbbb", 0.35
            elif new:
                fc, ec, alpha = COLORS.get(p.fp["kind"], "#ffd54f"), "#000", 1.0
            else:
                fc, ec, alpha = "#d6d6d6", "#8a8a8a", 0.9
            ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc=fc, ec=ec, lw=0.8 if new else 0.5,
                                   alpha=alpha, ls="--" if p.fp.get("module") else "-"))
            if not bottom or template:
                fs = 5.2 if (new or template) else 4.2
                label = p.ref if not (new or template) else f"{p.ref}\n{p.value}" if p.value else p.ref
                rot = 90 if (y1 - y0) > (x1 - x0) * 1.6 and (x1 - x0) < 8 else 0
                ax.text((x0 + x1) / 2, (y0 + y1) / 2, label, ha="center", va="center", fontsize=fs,
                        rotation=rot, color="#000" if (new or template) else "#555",
                        fontweight="bold" if new else "normal")
            for name, (c, r) in p.pins().items():
                x, y = self.mm(c, r)
                col = "#c62828" if new and not bottom else "#333"
                ax.add_patch(Circle((x, y), 0.75 if p.fp.get("module") else 0.6,
                                    fc="none" if p.fp.get("module") else col, ec=col, lw=0.6))
                if (new or template) and len(p.fp["pins"]) > 2 and p.fp["kind"] in ("K", "Q", "MOD", "IC", "J"):
                    if p.fp["kind"] == "J" and name not in ("1", "2"):
                        continue
                    ax.text(x + 0.9, y + 0.9, name.replace("_", "-"), fontsize=3.2, color="#1a237e")
            if p.fp["kind"] == "D" and (new or template or bottom):
                kx, ky = self.mm(*p.pins()["K"])
                ax.text(kx, ky + (1.6 if not bottom else -1.6), "K", fontsize=3.5, ha="center",
                        color="#6a1b9a")
        if template:
            return
        for w in self.wires:
            ws = order[w["step"]]
            if step_idx is not None and ws > step_idx:
                continue
            new = cur is not None and w["step"] == cur
            if not bottom and not new:
                continue
            pts = [self.mm(*self.pin(n)) for n in w["nodes"]]
            color = NET_COLORS.get(w["net"], NET_COLORS.get(w["net"].split()[0], "#1565c0"))
            ax.plot([p[0] for p in pts], [p[1] for p in pts], color=color if new else "#9e9e9e",
                    lw=1.3 if new else 0.6, alpha=0.95 if new else 0.6, solid_capstyle="round",
                    ls="-" if bottom else ":")
            for x, y in pts:
                ax.add_patch(Circle((x, y), 0.55, fc=color if new else "#9e9e9e", ec="none"))

    def render(self):
        os.makedirs(OUT, exist_ok=True)
        W, H = self.size
        files = []
        for i, s in enumerate(self.steps):
            fig, axs = plt.subplots(1, 2, figsize=(2 * (W + 8) / 25.4 * 1.55, (H + 14) / 25.4 * 1.55))
            self.draw(axs[0], i, bottom=False)
            self.draw(axs[1], i, bottom=True)
            axs[0].set_title("TOP: where the parts go", fontsize=8)
            axs[1].set_title("BOTTOM (board flipped): where the wires go", fontsize=8)
            fig.suptitle(f"{self.title} - step {s}", fontsize=10, fontweight="bold")
            fig.tight_layout()
            f = os.path.join(OUT, f"{self.name}-{s}.png")
            fig.savefig(f, dpi=140)
            plt.close(fig)
            shrink(f)
            files.append(f)
        # full layout
        fig, ax = plt.subplots(figsize=((W + 8) / 25.4 * 1.8, (H + 10) / 25.4 * 1.8))
        self.draw(ax, len(self.steps) - 1)
        for p in self.parts:  # recolour everything as "placed"
            pass
        ax.set_title(f"{self.title} - finished layout (top view)", fontsize=9)
        fig.tight_layout()
        fig.savefig(os.path.join(OUT, f"{self.name}-layout.png"), dpi=150)
        plt.close(fig)
        shrink(os.path.join(OUT, f"{self.name}-layout.png"))
        # 1:1 template: figure size in inches = board size in mm / 25.4
        fig = plt.figure(figsize=((W + 6) / 25.4, (H + 6) / 25.4))
        ax = fig.add_axes([0, 0, 1, 1])
        self.draw(ax, template=True)
        ax.set_xlim(-3, W + 3)
        ax.set_ylim(-3, H + 3)
        ax.text(W / 2, H + 1.5, f"{self.title}: 1:1 TEMPLATE, print at 100 % (check: board = {W} x {H} mm)",
                ha="center", fontsize=4.5)
        fig.savefig(os.path.join(OUT, f"{self.name}-template.svg"))
        plt.close(fig)
        return files

    # ----------------------------------------------------------- text
    def hole(self, cr):
        return f"col {cr[0]}, row {cr[1]}"

    def markdown(self, step):
        lines = []
        parts = [p for p in self.parts if p.step == step]
        if parts:
            lines.append("| Part | Value | Goes in | Notes |")
            lines.append("|---|---|---|---|")
            for p in parts:
                pins = p.pins()
                if p.fp.get("module"):
                    where = "on foam tape, " + ", ".join(f"{k} at {self.hole(v)}" for k, v in
                                                          list(pins.items())[:1])
                elif len(pins) <= 2:
                    where = " → ".join(f"{k} {self.hole(v)}" for k, v in pins.items())
                else:
                    first = list(pins.items())[0]
                    where = f"{first[0]} at {self.hole(first[1])}"
                lines.append(f"| **{p.ref}** | {p.value} | {where} | {p.note} |")
            lines.append("")
        wires = [w for w in self.wires if w["step"] == step]
        if wires:
            lines.append("| Wire | Net | Connect, in this order | Wire |")
            lines.append("|---|---|---|---|")
            for i, w in enumerate(wires, 1):
                chain = " → ".join(n.replace("_", "-") for n in w["nodes"])
                lines.append(f"| {step}.{i} | {w['net']} | {chain} | {w['gauge']} AWG |")
            lines.append("")
        return "\n".join(lines)

    def envelopes(self):
        """[x0, y0, x1, y1, h] in mm, board-local, for the fit check."""
        out = []
        for p in self.parts:
            c0, r0, c1, r1 = p.body()
            x0, y0 = self.mm(c0, r0)
            x1, y1 = self.mm(c1, r1)
            out.append((p.ref, x0, y0, x1, y1, p.fp["h"]))
        return out


def shrink(path):
    """Palette PNG: these drawings use few colours, so this cuts the size ~4x."""
    Image.open(path).convert("RGB").quantize(colors=96, method=Image.Quantize.MEDIANCUT).save(path, optimize=True)


COLORS = {"R": "#ffe082", "D": "#ef9a9a", "C": "#90caf9", "CE": "#4fc3f7", "IC": "#424242",
          "TB": "#66bb6a", "K": "#ffb74d", "Q": "#9575cd", "MOD": "#80cbc4", "J": "#bcaaa4"}
NET_COLORS = {"GND": "#212121", "5V": "#d32f2f", "+12": "#ef6c00", "DRV": "#ef6c00", "LOG": "#f9a825",
              "ECU": "#b8860b", "SOL": "#ff8f00", "LOAD": "#8d6e63"}


# ==========================================================================
# POWER BOARD: everything that touches the car's 12 V wiring.
# 90 x 150 mm perfboard, 35 x 59 holes. Row 0 is the front edge, facing the
# cable glands; the ribbon to the Mega leaves from the left edge.
def power_board():
    b = Board("power", "Power board", (90, 150), (35, 59))
    b.steps = ["P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"]
    A = b.add
    CH = (13, 19, 25)  # left pin column of each solenoid channel (S1, S2, SL)

    # ---- P1: power in, protection, 5 V supply, battery sense
    A(Part("J1a", fp_tb(), 3, 2, value="+12 DRV", step="P1", note="X1-1 (5 A fuse)"),
      Part("J1b", fp_tb(), 5, 2, value="+12 LOG", step="P1", note="X1-2 (2 A fuse)"),
      Part("J1c", fp_tb(), 7, 2, value="GND", step="P1", note="X1-3 (body ground)"),
      Part("TVS1", fp_d201(), 4, 5, rot=90, value="1.5KE27A", step="P1", note="band (K) towards the back"),
      Part("D1", fp_d201(), 7, 5, rot=90, value="1N5822", step="P1", note="band (K) towards the back"),
      Part("TVS2", fp_d201(), 10, 5, rot=90, value="1.5KE27A", step="P1", note="band (K) towards the back"),
      Part("C1", fp_ce8(), 4, 15, value="100 µF 50 V", step="P1", note="long leg (+) left, stripe (−) right"),
      Part("RB1", fp_r(), 7, 13, rot=90, value="47k", step="P1", note="battery sense, top"),
      Part("RB2", fp_r(), 9, 13, rot=90, value="10k", step="P1", note="battery sense, bottom"),
      Part("C2", fp_c(), 5, 18, value="100 nF", step="P1"),
      Part("Z1", fp_d41(), 11, 19, rot=180, value="1N4733A 5.1 V", step="P1", note="band (K) left"),
      Part("BUCK", fp_buck(), 17, 50, value="LM2596HV module", step="P1",
           note="foam tape; set to 5.0 V before wiring 5 V anywhere"))
    b.wire("P1", "+12 DRV", "J1a.1", "TVS1.K", gauge="18")
    b.wire("P1", "+12 LOG", "J1b.1", "D1.A", "RB1.1", gauge="20")
    b.wire("P1", "LOG", "D1.K", "TVS2.K", "C1.+", "BUCK.IN+", gauge="20")
    b.wire("P1", "GND", "J1c.1", "TVS1.A", "TVS2.A", "C1.-", "RB2.2", "C2.2", "Z1.A", "BUCK.IN-", gauge="18")
    b.wire("P1", "VBAT", "RB1.2", "RB2.1", "C2.1", "Z1.K")
    b.wire("P1", "GND", "BUCK.IN-", "BUCK.OUT-", gauge="20")

    # ---- P2: ribbon header to the Mega; 5 V and ground to it
    A(Part("JA", fp_boxhdr(26), 1, 57, value="26-pin box header", step="P2",
           note="notch facing the left edge; pin 1 (square pad) at the back"))
    b.wire("P2", "5V", "BUCK.OUT+", "JA.1", "JA.3")
    b.wire("P2", "GND", "BUCK.OUT-", "JA.2", "JA.4", "JA.16", "JA.23", "JA.24")
    b.wire("P2", "VBAT", "Z1.K", "JA.19")

    # ---- P3: relay driver and relays
    A(Part("U1", fp_dip(18), 10, 58, value="ULN2803A", step="P3", note="notch / dot (pin 1) at the back-left"),
      Part("RD1", fp_r(), 5, 57, value="10k", step="P3", note="pull-down, D6"),
      Part("RD2", fp_r(), 5, 55, value="10k", step="P3", note="pull-down, D8"),
      Part("RD3", fp_r(), 5, 53, value="10k", step="P3", note="pull-down, D9"),
      Part("RD4", fp_r(), 5, 51, value="10k", step="P3", note="pull-down, D10"))
    for n, c in enumerate(CH, 1):
        A(Part(f"K{n}", fp_g2rl(), c, 17, value="G2RL-2 12 V", step="P3", note="coil end at the back"))
    b.wire("P3", "RLY (D6)", "JA.7", "RD1.2", "U1.1", "U1.2", "U1.3")
    b.wire("P3", "DRV1 (D8)", "JA.8", "RD2.2", "U1.4")
    b.wire("P3", "DRV2 (D9)", "JA.9", "RD3.2", "U1.5")
    b.wire("P3", "DRV3 (D10)", "JA.10", "RD4.2", "U1.6")
    b.wire("P3", "GND", "JA.16", "RD1.1", "RD2.1", "RD3.1", "RD4.1", "U1.9")
    b.wire("P3", "+12 DRV", "TVS1.K", "U1.10", "K1.COIL1", "K2.COIL1", "K3.COIL1", gauge="20")
    b.wire("P3", "COIL K1", "U1.18", "K1.COIL2")
    b.wire("P3", "COIL K2", "U1.17", "K2.COIL2")
    b.wire("P3", "COIL K3", "U1.16", "K3.COIL2")

    # ---- P4: solenoid terminals, relay contacts, dummy-load terminals
    for i, (ref, val, note) in enumerate((("J2a", "ECU S1", "X2-1"), ("J2b", "ECU S2", "X2-2"),
                                          ("J2c", "ECU SL", "X2-3"), ("J2d", "SOL S1", "X2-4"),
                                          ("J2e", "SOL S2", "X2-5"), ("J2f", "SOL SL", "X2-6"))):
        A(Part(ref, fp_tb(), 11 + 2 * i, 2, value=val, step="P4", note=note))
    for i, (ref, val, note) in enumerate((("J4a", "LOAD 1", "to dummy resistor 1"), ("J4b", "LOAD 2", "to 2"),
                                          ("J4c", "LOAD 3", "to 3"), ("J4d", "LOAD GND", "resistors' common"))):
        A(Part(ref, fp_tb(), 25 + 2 * i, 2, value=val, step="P4", note=note))
    for k, e, s_, l in (("K1", "J2a", "J2d", "J4a"), ("K2", "J2b", "J2e", "J4b"), ("K3", "J2c", "J2f", "J4c")):
        n = k[1]
        b.wire("P4", f"ECU {n}", f"{e}.1", f"{k}.COM_A", gauge="20")
        b.wire("P4", f"SOL {n}", f"{s_}.1", f"{k}.NC_A", f"{k}.NO_B", gauge="20")
        b.wire("P4", f"LOAD {n}", f"{l}.1", f"{k}.NO_A", gauge="20")
    b.wire("P4", "GND", "J1c.1", "J4d.1", gauge="18")

    # ---- P5: the three high-side drivers (P-MOSFET + current sensor)
    for n, c in enumerate(CH, 1):
        A(Part(f"Q{n}", fp_to220(), c, 21, value="IRF4905", step="P5", note="metal tab towards the back"),
          Part(f"RG{n}", fp_r(), c - 1, 23, rot=90, value="4.7k", step="P5", note="gate drive"),
          Part(f"RP{n}", fp_r(), c + 1, 23, rot=90, value="10k", step="P5", note="gate pull-up (= off)"),
          Part(f"ZG{n}", fp_d41(), c + 3, 23, rot=90, value="1N4744A 15 V", step="P5",
               note="band (K) towards the back"),
          Part(f"DF{n}", fp_d41(), c - 1, 29, value="UF4007", step="P5", note="band (K) right"),
          Part(f"ZF{n}", fp_d41(), c + 3, 31, rot=180, value="1N4749A 24 V", step="P5", note="band (K) left"),
          Part(f"CS{n}", fp_acs712(), c + 1, 34, value="ACS712-05B", step="P5",
               note="foam tape; screw terminals towards the front"))
    for n, k, u in ((1, "K1", 15), (2, "K2", 14), (3, "K3", 13)):
        b.wire("P5", "+12 DRV", "TVS1.K" if n == 1 else f"CS{n - 1}.IP+", f"CS{n}.IP+", gauge="20")
        b.wire("P5", f"SRC{n}", f"CS{n}.IP-", f"Q{n}.S", f"RP{n}.2", f"ZG{n}.K", gauge="20")
        b.wire("P5", f"GATE{n}", f"Q{n}.G", f"RP{n}.1", f"ZG{n}.A", f"RG{n}.1")
        b.wire("P5", f"GDRV{n}", f"RG{n}.2", f"U1.{u}")
        b.wire("P5", f"DOUT{n}", f"Q{n}.D", f"DF{n}.K", f"{k}.COM_B", gauge="20")
        b.wire("P5", f"CLAMP{n}", f"DF{n}.A", f"ZF{n}.A")
        b.wire("P5", "GND", "J4d.1" if n == 1 else f"ZF{n - 1}.K", f"ZF{n}.K")
        b.wire("P5", "5V", "JA.1" if n == 1 else f"CS{n - 1}.VCC", f"CS{n}.VCC")
        b.wire("P5", "GND", "JA.2" if n == 1 else f"CS{n - 1}.GND", f"CS{n}.GND")
        b.wire("P5", f"IS{n} (A{3 + n})", f"CS{n}.OUT", f"JA.{19 + n}")

    # ---- P6: signal terminals and optocouplers for the 12 V inputs
    for i, (val, note) in enumerate((("D", "X3-1 lever in D"), ("STP", "X3-2 brake"), ("E1", "X3-3 ECM ground"),
                                     ("SPD", "X3-4 speed"), ("TAC", "X3-5 tach"), ("VTA", "X3-6 throttle"),
                                     ("E2", "X3-7 throttle ground"))):
        A(Part(f"J3{chr(97 + i)}", fp_tb(), 1, 20 - 2 * i, rot=270, value=val, step="P6", note=note))
    opto = (("OK1", "J3a.1", "2.2k", "D in", "LEVD (D28)", 12),
            ("OK2", "J3b.1", "2.2k", "STP in", "STP (D27)", 11),
            ("OK3", "J3e.1", "2.2k", "TAC in", "TAC (D3)", 6),
            ("OK4", "K1.COM_A", "3.3k", "ECU 1", "ECU1 (D29)", 13),
            ("OK5", "K2.COM_A", "3.3k", "ECU 2", "ECU2 (D30)", 14),
            ("OK6", "K3.COM_A", "3.3k", "ECU 3", "ECU3 (D31)", 15))
    for i, (ref, src, rv, src_net, net, ja) in enumerate(opto):
        r0 = 20 + 5 * i
        A(Part(ref, fp_dip(4), 6, r0 + 2, value="PC817", step="P6", note="dot (pin 1) top-left"),
          Part(f"RI{i + 1}", fp_r(), 4, r0, rot=90, value=rv, step="P6", note="input"),
          Part(f"DI{i + 1}", fp_d35(), 5, r0 + 3, rot=270, value="1N4148", step="P6", note="band (K) towards the front"),
          Part(f"RU{i + 1}", fp_r(), 11, r0, rot=90, value="10k", step="P6", note="output pull-up"))
    for i, (ref, src, rv, src_net, net, ja) in enumerate(opto):
        b.wire("P6", src_net, src, f"RI{i + 1}.1")
        b.wire("P6", f"LED {ref}", f"RI{i + 1}.2", f"{ref}.1", f"DI{i + 1}.K")
        b.wire("P6", "GND", f"{ref}.2", f"DI{i + 1}.A", f"{ref}.3")
        b.wire("P6", net, f"{ref}.4", f"RU{i + 1}.1", f"JA.{ja}")
        b.wire("P6", "5V", "JA.3" if i == 0 else f"RU{i}.2", f"RU{i + 1}.2")
        b.wire("P6", "GND", "JA.4" if i == 0 else f"OK{i}.3", f"{ref}.3")
    b.wire("P6", "GND", "J3c.1", "J1c.1", gauge="20")

    # ---- P7: speed-signal comparator (right-hand strip)
    A(Part("U2", fp_dip(8), 30, 45, value="LM393", step="P7", note="notch / dot (pin 1) top-left"),
      Part("RC1", fp_r(), 30, 22, rot=90, value="22k", step="P7", note="SPD in"),
      Part("RC2", fp_r(), 31, 22, rot=90, value="10k", step="P7", note="SPD to ground"),
      Part("Z2", fp_d41(), 32, 22, rot=90, value="1N4733A 5.1 V", step="P7", note="band (K) towards the back"),
      Part("RC3", fp_r(), 33, 22, rot=90, value="1M", step="P7", note="hysteresis"),
      Part("RC6", fp_r(), 34, 22, rot=90, value="10k", step="P7", note="output pull-up"),
      Part("RC4", fp_r(), 30, 28, rot=90, value="10k", step="P7", note="threshold, top"),
      Part("RC5", fp_r(), 31, 28, rot=90, value="1.8k", step="P7", note="threshold, bottom (0.76 V)"))
    b.wire("P7", "SPD in", "J3d.1", "RC1.1")
    b.wire("P7", "SPDIN", "RC1.2", "RC2.2", "Z2.K", "RC3.2", "U2.3")
    b.wire("P7", "SPD (D2)", "U2.1", "RC3.1", "RC6.2", "JA.5")
    b.wire("P7", "REF", "RC4.1", "RC5.2", "U2.2")
    b.wire("P7", "5V", "BUCK.OUT+", "U2.8", "U2.6", "RC4.2", "RC6.1")
    b.wire("P7", "GND", "BUCK.OUT-", "U2.4", "U2.5", "RC5.1", "RC2.1", "Z2.A")

    # ---- P8: throttle taps (right-hand strip)
    A(Part("RT1", fp_r(), 32, 28, rot=90, value="10k", step="P8", note="VTA series"),
      Part("RT2", fp_r(), 33, 28, rot=90, value="10k", step="P8", note="E2 series"),
      Part("C3", fp_c(), 34, 28, rot=90, value="100 nF", step="P8", note="VTA filter"),
      Part("C4", fp_c(), 30, 34, rot=90, value="100 nF", step="P8", note="E2 filter"),
      Part("DT1", fp_d35(), 31, 34, rot=90, value="BAT85", step="P8", note="VTA to 5 V; band (K) towards the back"),
      Part("DT2", fp_d35(), 32, 37, rot=270, value="BAT85", step="P8", note="GND to VTA; band (K) towards the front"),
      Part("DT3", fp_d35(), 33, 34, rot=90, value="BAT85", step="P8", note="E2 to 5 V; band (K) towards the back"),
      Part("DT4", fp_d35(), 34, 37, rot=270, value="BAT85", step="P8", note="GND to E2; band (K) towards the front"))
    b.wire("P8", "VTA in", "J3f.1", "RT1.1")
    b.wire("P8", "E2 in", "J3g.1", "RT2.1")
    b.wire("P8", "VTA (A0)", "RT1.2", "C3.1", "DT1.A", "DT2.K", "JA.17")
    b.wire("P8", "E2 (A1)", "RT2.2", "C4.1", "DT3.A", "DT4.K", "JA.18")
    b.wire("P8", "5V", "U2.8", "DT1.K", "DT3.K")
    b.wire("P8", "GND", "U2.4", "C3.2", "C4.2", "DT2.A", "DT4.A")
    return b


# ==========================================================================
# LOGIC BOARD: console, screen, ATF probe, gyro. 90 x 70 mm, 35 x 27 holes.
def logic_board():
    b = Board("logic", "Logic board", (90, 70), (35, 27))
    b.steps = ["L1", "L2", "L3", "L4"]
    A = b.add
    names = ["C1 [+]", "C2 [−]", "C3 ECO", "C4 SPORT", "C5 ON/OFF", "C6 TEST", "C7 GND", "C8 LED"]
    for i, n in enumerate(names):
        A(Part(f"X4{chr(97 + i)}", fp_tb(), 2 + 2 * i, 1, value=n.split()[0], step="L1",
               note=f"X4-{i + 1}: {n}"))
    for i, n in enumerate(["5V", "GND", "SDA", "SCL"]):
        A(Part(f"X5{chr(97 + i)}", fp_tb(), 19 + 2 * i, 1, value=n, step="L1", note=f"X5-{i + 1} OLED"))
    for i, n in enumerate(["5V", "GND", "DQ"]):
        A(Part(f"X6{chr(97 + i)}", fp_tb(), 28 + 2 * i, 1, value=n, step="L1", note=f"X6-{i + 1} ATF probe"))
    A(Part("JB", fp_boxhdr(16), 9, 24, rot=270, value="16-pin box header", step="L1",
           note="notch facing the back edge; pin 1 (square pad) at the right-hand end, back row"))
    b.wire("L1", "5V", "JB.1", "X5a.1", "X6a.1")
    b.wire("L1", "GND", "JB.2", "X4g.1", "X5b.1", "X6b.1", "JB.13")
    b.wire("L1", "SDA (D20)", "JB.11", "X5c.1")
    b.wire("L1", "SCL (D21)", "JB.12", "X5d.1")

    # L2: six console input filters
    for i in range(6):
        c = 2 + 2 * i
        t = f"X4{chr(97 + i)}"
        A(Part(f"RU{i + 1}", fp_r(), c, 4, rot=90, value="2.2k", step="L2", note="pull-up"),
          Part(f"RS{i + 1}", fp_r(), c + 1, 9, rot=90, value="1k", step="L2", note="series"),
          Part(f"CF{i + 1}", fp_c(), c, 14, rot=90, value="100 nF", step="L2"),
          Part(f"DH{i + 1}", fp_d35(), c + 1, 15, rot=90, value="BAT85", step="L2", note="band (K) towards the back"),
          Part(f"DL{i + 1}", fp_d35(), c, 18, rot=90, value="BAT85", step="L2", note="band (K) towards the back"))
    for i in range(6):
        t = f"X4{chr(97 + i)}"
        b.wire("L2", f"C{i + 1}", f"{t}.1", f"RU{i + 1}.1", f"RS{i + 1}.1")
        b.wire("L2", f"IN{i + 1} (JB.{3 + i})", f"RS{i + 1}.2", f"DH{i + 1}.A", f"CF{i + 1}.1", f"DL{i + 1}.K",
               f"JB.{3 + i}")
    b.wire("L2", "5V", "JB.1", "RU1.2", "RU2.2", "RU3.2", "RU4.2", "RU5.2", "RU6.2", "DH6.K", "DH5.K",
           "DH4.K", "DH3.K", "DH2.K", "DH1.K")
    b.wire("L2", "GND", "JB.14", "DL1.A", "DL2.A", "DL3.A", "DL4.A", "DL5.A", "DL6.A", "CF6.2", "CF5.2",
           "CF4.2", "CF3.2", "CF2.2", "CF1.2")

    # L3: status LED resistor, ATF probe
    A(Part("RL", fp_r(), 14, 4, rot=90, value="330", step="L3", note="status LED"),
      Part("RA1", fp_r(), 26, 5, rot=90, value="4.7k", step="L3", note="ATF pull-up"),
      Part("RA2", fp_r(), 28, 5, rot=90, value="100", step="L3", note="ATF series"),
      Part("ZA", fp_d41(), 30, 5, rot=90, value="1N4733A 5.1 V", step="L3", note="band (K) towards the back"))
    b.wire("L3", "LED (D7)", "JB.9", "RL.2")
    b.wire("L3", "C8 LED+", "RL.1", "X4h.1")
    b.wire("L3", "DQ", "X6c.1", "RA1.1", "RA2.1", "ZA.K")
    b.wire("L3", "ATF (D33)", "RA2.2", "JB.10")
    b.wire("L3", "5V", "X6a.1", "RA1.2")
    b.wire("L3", "GND", "X6b.1", "ZA.A")

    # L4: gyro, soldered flat on its header
    A(Part("GYRO", fp_gy521(), 19, 12, value="GY-521", step="L4",
           note="chip side up, flat on its header + a dab of hot glue"))
    b.wire("L4", "5V", "X5a.1", "GYRO.VCC")
    b.wire("L4", "GND", "X5b.1", "GYRO.GND")
    b.wire("L4", "SCL (D21)", "X5d.1", "GYRO.SCL")
    b.wire("L4", "SDA (D20)", "X5c.1", "GYRO.SDA")
    return b


HUB_A = [  # ribbon A (power board) pin -> Mega pin
    (1, "5V", "5V"), (2, "GND", "GND"), (3, "5V", "5V"), (4, "GND", "GND"),
    (5, "SPD (speed)", "D2"), (6, "TAC (tach)", "D3"), (7, "RLY (relays)", "D6"),
    (8, "DRV1 (S1 driver)", "D8"), (9, "DRV2 (S2 driver)", "D9"), (10, "DRV3 (SL driver)", "D10"),
    (11, "STP (brake)", "D27"), (12, "LEVD (lever in D)", "D28"), (13, "ECU1 (ECU S1)", "D29"),
    (14, "ECU2 (ECU S2)", "D30"), (15, "ECU3 (ECU SL)", "D31"), (16, "GND", "GND"),
    (17, "VTA (throttle)", "A0"), (18, "E2 (throttle ground)", "A1"), (19, "VBAT (battery)", "A3"),
    (20, "IS1 (S1 current)", "A4"), (21, "IS2 (S2 current)", "A5"), (22, "IS3 (SL current)", "A6"),
    (23, "GND", "GND"), (24, "GND", "GND"), (25, "–", "not connected"), (26, "–", "not connected")]
HUB_B = [  # ribbon B (logic board) pin -> Mega pin
    (1, "5V", "5V"), (2, "GND", "GND"), (3, "C1 [+]", "D22"), (4, "C2 [−]", "D23"),
    (5, "C3 ECO", "D24"), (6, "C4 SPORT", "D25"), (7, "C5 ON/OFF", "D26"), (8, "C6 TEST", "D32"),
    (9, "LED", "D7"), (10, "ATF probe", "D33"), (11, "SDA", "D20 (SDA)"), (12, "SCL", "D21 (SCL)"),
    (13, "GND", "GND"), (14, "GND", "GND"), (15, "–", "not connected"), (16, "–", "not connected")]


def hub_markdown():
    lines = ["| Header A pin | Signal | Mega pin |", "|---|---|---|"]
    lines += [f"| A{p} | {s} | **{m}** |" for p, s, m in HUB_A]
    lines += ["", "| Header B pin | Signal | Mega pin |", "|---|---|---|"]
    lines += [f"| B{p} | {s} | **{m}** |" for p, s, m in HUB_B]
    return "\n".join(lines)


def write_envelopes(boards):
    path = os.path.join(ROOT, "hardware", "board_envelopes.scad")
    with open(path, "w") as f:
        f.write("// Generated by tools/boards.py - do not edit. Each part: [x0, y0, x1, y1, height]\n")
        f.write("// in mm, board-local (origin = board's front-left corner, height above the board).\n")
        for b in boards:
            f.write(f"{b.name}_board_size = [{b.size[0]}, {b.size[1]}];\n")
            f.write(f"{b.name}_parts = [\n")
            for ref, x0, y0, x1, y1, h in b.envelopes():
                f.write(f"    [{x0:.2f}, {y0:.2f}, {x1:.2f}, {y1:.2f}, {h}],  // {ref}\n")
            f.write("];\n")


def main():
    boards = [power_board(), logic_board()]
    bad = False
    for b in boards:
        errs = b.check()
        for e in errs:
            print(f"{b.name}: {e}", file=sys.stderr)
        bad |= bool(errs)
    if bad and "--force" not in sys.argv:
        sys.exit(1)
    md = ["<!-- Generated by tools/boards.py - do not edit by hand. -->", ""]
    for b in boards:
        b.render()
        for s in b.steps:
            md.append(f"<!-- STEP {s} -->")
            md.append(b.markdown(s))
    md.append("<!-- STEP HUB -->")
    md.append(hub_markdown())
    with open(os.path.join(OUT, "board-steps.md"), "w") as f:
        f.write("\n".join(md) + "\n")
    # the build book: template + generated step tables
    with open(os.path.join(ROOT, "tools", "build_book_template.md")) as f:
        book = f.read()
    for b in boards:
        for s in b.steps:
            book = book.replace("{{STEP %s}}" % s, b.markdown(s).rstrip())
    book = book.replace("{{HUB}}", hub_markdown())
    if "{{" in book:
        sys.exit("build book: unfilled placeholder " + book[book.index("{{"):book.index("{{") + 20])
    with open(os.path.join(ROOT, "docs", "05-build-book.md"), "w") as f:
        f.write(book)
    write_envelopes(boards)
    for b in boards:
        n = sum(len(b.markdown(s)) > 0 for s in b.steps)
        print(f"{b.name}: {len(b.parts)} parts, {len(b.wires)} wire runs, {len(b.steps)} steps")


if __name__ == "__main__":
    main()
