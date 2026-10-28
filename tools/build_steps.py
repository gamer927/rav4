#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""Lego-style soldering steps for the circuit board. Called by tools/pcb.py.

Each of the build book's six soldering steps is cut into small sub-steps: one
kind of part (one value) in one area of the board, at most MAX_PARTS at a time,
lowest parts first. Every sub-step gets a picture (docs/build/pcb-<n>-<m>.png):

  * left, "find": the part as it looks in your hand, with its colour stripes or
    markings, how many, and what's printed on the board for it
  * right, "fit": a close-up of that area of the board, the parts going in now
    in full colour with an orange outline and their names, the ones already
    fitted faded, the empty places as printed on the board; and a small map of
    the whole board showing where the close-up is
"""
import math
import os

MAX_PARTS = 7
MAX_AREA = (80.0, 55.0)       # a sub-step's parts fit in this much board (mm)

C = {"board": "#2e7d32", "edge": "#1b5e20", "pad": "#d9b44a", "hole": "#2b1d14", "silk": "#f5f5f5",
     "now": "#ff6d00", "leg": "#9e9e9e", "page": "#ffffff", "text": "#212121"}
BAND_RGB = {"black": "#111111", "brown": "#7b3f00", "red": "#d32f2f", "orange": "#f57c00", "yellow": "#fdd835",
            "green": "#388e3c", "blue": "#1976d2", "violet": "#7b1fa2", "grey": "#9e9e9e", "white": "#fafafa",
            "gold": "#c9a227", "silver": "#c0c0c0"}
# Diodes as they look: (body length, diameter, body colour, band colour)
DIODE_LOOK = {"DO-35": (3.8, 1.8, "#ef6c00", "#111111"), "DO-41": (5.0, 2.6, "#212121", "#bdbdbd"),
              "DO-15": (7.0, 3.4, "#212121", "#eeeeee"), "DO-201": (9.0, 5.0, "#212121", "#bdbdbd")}


class Part:
    """What the pictures need to know about one footprint on the board."""

    def __init__(self, fp, part, env):
        import pcbnew
        self.ref, self.kind, self.value = fp.GetReference(), part["kind"], part["value"]
        self.lib = str(fp.GetFPID().GetLibItemName())
        self.pads = []
        for p in fp.Pads():
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            x, y = env["bxy"](*p.GetPosition())
            self.pads.append((p.GetNumber(), x, y, p.GetSize().x / 1e6, p.GetSize().y / 1e6))
        self.holes = [env["bxy"](*p.GetPosition()) for p in fp.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH]
        self.fab = env["fab_box"](fp)
        self.box = env["courtyard"](fp)
        self.silk = env["fab_box"](fp, silk=True)
        self.label = env["short_value"](self.value)

    def pad(self, num):
        for n, x, y, _, _ in self.pads:
            if n == num:
                return x, y
        return None

    def centre(self):
        x0, y0, x1, y1 = self.box
        return (x0 + x1) / 2, (y0 + y1) / 2


# ------------------------------------------------------------------ drawing
def _axial(ax, a, b, length, width, body, bands=(), band_at=None, alpha=1.0, z=6):
    """A part lying flat between pads a and b: legs, body, stripes. bands: [(pos 0-1, width 0-1, colour)]."""
    from matplotlib.patches import Polygon
    (ax_, ay), (bx, by) = a, b
    d = math.hypot(bx - ax_, by - ay)
    ux, uy = (bx - ax_) / d, (by - ay) / d
    nx, ny = -uy, ux
    cx, cy = (ax_ + bx) / 2, (ay + by) / 2
    ax.plot([ax_, bx], [ay, by], color=C["leg"], lw=1.6, alpha=alpha, zorder=z, solid_capstyle="round")

    def quad(s0, s1, w, colour, zz):
        p = [(cx + ux * (s0 - 0.5) * length + nx * w / 2, cy + uy * (s0 - 0.5) * length + ny * w / 2),
             (cx + ux * (s1 - 0.5) * length + nx * w / 2, cy + uy * (s1 - 0.5) * length + ny * w / 2),
             (cx + ux * (s1 - 0.5) * length - nx * w / 2, cy + uy * (s1 - 0.5) * length - ny * w / 2),
             (cx + ux * (s0 - 0.5) * length - nx * w / 2, cy + uy * (s0 - 0.5) * length - ny * w / 2)]
        ax.add_patch(Polygon(p, closed=True, fc=colour, ec="#000000", lw=0.4, alpha=alpha, zorder=zz))

    quad(0, 1, width, body, z + 1)
    for pos, w, colour in bands:
        quad(pos - w / 2, pos + w / 2, width * 0.98, colour, z + 2)


def draw_part(ax, p, style, env):
    """style: 'now' (full colour, orange outline), 'done' (faded), 'todo' (as printed on the bare board)."""
    from matplotlib.patches import Circle, FancyBboxPatch, Rectangle, Wedge
    alpha = 0.42 if style == "done" else 1.0
    x0, y0, x1, y1 = p.fab
    if style == "todo":
        ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc="none", ec=C["silk"], lw=0.7, zorder=3))
        cx, cy = p.centre()
        ax.text(cx, cy, p.ref, color=C["silk"], fontsize=5.5, ha="center", va="center", zorder=4, clip_on=True)
        return
    k = p.kind
    if k in ("R", "D", "C", "PTC") and len(p.pads) == 2:
        a = p.pad("1") or (p.pads[0][1], p.pads[0][2])
        b = p.pad("2") or (p.pads[1][1], p.pads[1][2])
        if k == "R":
            cols = [BAND_RGB[c] for c in env["bands"](p.value)]
            bands = [(0.2, 0.09, cols[0]), (0.33, 0.09, cols[1]), (0.46, 0.09, cols[2]), (0.59, 0.09, cols[3]),
                     (0.84, 0.09, cols[4])]
            _axial(ax, a, b, 6.3, 2.4, "#6fa8dc", bands, alpha=alpha)
        elif k == "D":
            pkg = next(n for n in DIODE_LOOK if n in p.lib)
            length, width, body, band = DIODE_LOOK[pkg]
            # pad 1 is the cathode: the stripe at that end
            _axial(ax, a, b, length, width, body, [(0.12, 0.16, band)], alpha=alpha)
        elif k == "C":
            _axial(ax, a, b, 4.6, 2.6, "#f9a825", alpha=alpha)
        else:
            _axial(ax, a, b, 7.2, 3.0, "#fdd835", alpha=alpha)
    elif k == "CE":
        (ax_, ay), (bx, by) = p.pad("1"), p.pad("2")
        d = 8.0 if "D8" in p.lib else 6.3
        cx, cy = (ax_ + bx) / 2, (ay + by) / 2
        ax.add_patch(Circle((cx, cy), d / 2, fc="#1a237e", ec="#000", lw=0.5, alpha=alpha, zorder=7))
        ang = math.degrees(math.atan2(by - ay, bx - ax_))       # towards the − leg
        ax.add_patch(Wedge((cx, cy), d / 2, ang - 35, ang + 35, fc="#cfd8dc", ec="none", alpha=alpha, zorder=8))
        ax.text(ax_ - (bx - ax_) * 0.55, ay - (by - ay) * 0.55, "+", color="#fff" if style == "now" else "#ddd",
                fontsize=9, fontweight="bold", ha="center", va="center", zorder=9, clip_on=True)
    elif k == "IC":
        body = (x0 + 0.4, y0, x1 - 0.4, y1) if (x1 - x0) < (y1 - y0) else (x0, y0 + 0.4, x1, y1 - 0.4)
        ax.add_patch(Rectangle(body[:2], body[2] - body[0], body[3] - body[1], fc="#212121", ec="#000", lw=0.5,
                               alpha=alpha, zorder=7))
        px, py = p.pad("1")
        # the notch: on the end of the body nearest pin 1
        if (x1 - x0) < (y1 - y0):
            ny = body[3] if abs(py - body[3]) < abs(py - body[1]) else body[1]
            ax.add_patch(Wedge(((body[0] + body[2]) / 2, ny), 0.9, 180 if ny == body[3] else 0,
                               360 if ny == body[3] else 180, fc="#9e9e9e", alpha=alpha, zorder=8))
        else:
            nx = body[0] if abs(px - body[0]) < abs(px - body[2]) else body[2]
            ax.add_patch(Wedge((nx, (body[1] + body[3]) / 2), 0.9, -90 if nx == body[0] else 90,
                               90 if nx == body[0] else 270, fc="#9e9e9e", alpha=alpha, zorder=8))
        ax.text((body[0] + body[2]) / 2, (body[1] + body[3]) / 2, p.label, color="#eee", fontsize=5,
                rotation=90 if (x1 - x0) < (y1 - y0) else 0, ha="center", va="center", zorder=9, alpha=alpha, clip_on=True)
    elif k == "K":
        ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc="#1565c0", ec="#0d47a1", lw=0.6, alpha=alpha,
                               zorder=7))
        ax.text((x0 + x1) / 2, (y0 + y1) / 2, "OMRON\nG2RL-2\n12V", color="#fff", fontsize=5.5, ha="center",
                va="center", zorder=9, alpha=alpha, clip_on=True)
    elif k == "Q":
        pcx = sum(x for _, x, _, _, _ in p.pads) / len(p.pads)
        pcy = sum(y for _, _, y, _, _ in p.pads) / len(p.pads)
        bx0, by0, bx1, by1 = x0, y0, x1, y1
        for _, x, y, _, _ in p.pads:   # legs
            ax.plot([x, x], [y, by0 if abs(pcy - by0) < abs(pcy - by1) else by1], color=C["leg"], lw=1.4,
                    alpha=alpha, zorder=6)
        legs_low = abs(pcy - by0) < abs(pcy - by1)
        split = by0 + (by1 - by0) * (0.6 if legs_low else 0.4)
        body = (bx0, by0, bx1, split) if legs_low else (bx0, split, bx1, by1)
        tab = (bx0, split, bx1, by1) if legs_low else (bx0, by0, bx1, split)
        ax.add_patch(Rectangle(tab[:2], tab[2] - tab[0], tab[3] - tab[1], fc="#b0bec5", ec="#607d8b", lw=0.5,
                               alpha=alpha, zorder=7))
        ax.add_patch(Rectangle(body[:2], body[2] - body[0], body[3] - body[1], fc="#212121", ec="#000", lw=0.5,
                               alpha=alpha, zorder=8))
        for hx, hy in p.holes:
            ax.add_patch(Circle((hx, hy), 1.7, fc="#78909c", ec="#37474f", lw=0.6, alpha=alpha, zorder=9, clip_on=True))
        ax.text((body[0] + body[2]) / 2, (body[1] + body[3]) / 2, "IRF\n4905", color="#eee", fontsize=5,
                ha="center", va="center", zorder=9, alpha=alpha, clip_on=True)
    elif k == "REG":
        ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc="#212121", ec="#000", lw=0.5, alpha=alpha, zorder=7))
        ax.text((x0 + x1) / 2, (y0 + y1) / 2, "RECOM\nR-78HB5.0", color="#eee", fontsize=5, ha="center",
                va="center", zorder=9, alpha=alpha, clip_on=True)
    elif k == "TB":
        ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc="#1e88e5", ec="#0d47a1", lw=0.6, alpha=alpha,
                               zorder=7))
        for _, x, y, _, _ in p.pads:
            ax.add_patch(Circle((x, y), 1.1, fc="#cfd8dc", ec="#546e7a", lw=0.5, alpha=alpha, zorder=8))
            ax.plot([x - 0.7, x + 0.7], [y, y], color="#546e7a", lw=0.8, alpha=alpha, zorder=9, clip_on=True)
            ax.add_patch(Rectangle((x - 1.1, y0), 2.2, 1.4, fc="#0d1b2a", alpha=alpha, zorder=8))   # wire opening
    elif k == "MOD" and p.ref.startswith("CS"):
        hx, hy = p.pad("OUT")
        lx, ly = [(u + v) / 2 for u, v in zip(p.pad("IP+"), p.pad("IP-"))]
        down = -1 if ly < hy else 1
        top, bottom = hy + down * -1.3, hy + down * 29.7
        ax.add_patch(Rectangle((hx - 6.5, min(top, bottom)), 13, 31, fc="#c62828", ec="#7f0000", lw=0.6, alpha=alpha,
                               zorder=7))
        ax.add_patch(Rectangle((hx - 2.5, hy + down * 12 - 2.5), 5, 5, fc="#212121", alpha=alpha, zorder=8))
        ax.add_patch(Rectangle((hx - 5, hy + down * 24 - 3.5), 10, 7, fc="#1e88e5", ec="#0d47a1", lw=0.5,
                               alpha=alpha, zorder=8))
        for s in (-1, 1):
            ax.plot([hx + 2.54 * s, hx + 2.54 * s], [hy + down * 24, ly], color=C["leg"], lw=2.2, alpha=alpha,
                    zorder=9)
    elif k == "MOD":
        sx0, sy0, sx1, sy1 = p.silk
        ax.add_patch(Rectangle((sx0, sy0), sx1 - sx0, sy1 - sy0, fc="#1565c0", ec="#0d47a1", lw=0.6, alpha=alpha,
                               zorder=7))
        ax.add_patch(Rectangle(((sx0 + sx1) / 2 - 2, (sy0 + sy1) / 2 - 2), 4, 4, fc="#212121", alpha=alpha, zorder=8))
        ax.text((sx0 + sx1) / 2, (sy0 + sy1) / 2 - 4.5, "GY-521", color="#fff", fontsize=5, ha="center",
                alpha=alpha, zorder=9, clip_on=True)
    elif k == "MEGA":
        for _, x, y, _, _ in p.pads:
            ax.add_patch(Rectangle((x - 1.27, y - 1.27), 2.54, 2.54, fc="#212121", ec="#000", lw=0.3, alpha=alpha,
                                   zorder=7))
            ax.add_patch(Rectangle((x - 0.32, y - 0.32), 0.64, 0.64, fc="#ffd54f", alpha=alpha, zorder=8))
    if style == "now":
        bx0, by0, bx1, by1 = p.box
        ax.add_patch(FancyBboxPatch((bx0, by0), bx1 - bx0, by1 - by0, boxstyle="round,pad=0.3", fc="none",
                                    ec=C["now"], lw=2.2, zorder=12))


def draw_board(ax, parts, now, done, env):
    from matplotlib.patches import Circle, Polygon
    ax.add_patch(Polygon(env["outline"](), closed=True, fc=C["board"], ec=C["edge"], lw=2, zorder=1))
    for hx, hy in env["holes"]:
        ax.add_patch(Circle((hx, hy), 1.6, fc=C["page"], ec="#bdbdbd", zorder=2))
    for p in parts.values():
        for _, x, y, w, h in p.pads:
            ax.add_patch(Circle((x, y), max(w, h) / 2 * 0.85, fc=C["pad"], ec="#8d6e63", lw=0.3, zorder=2))
            ax.add_patch(Circle((x, y), 0.4, fc=C["hole"], zorder=2))
    for ref, p in parts.items():
        draw_part(ax, p, "now" if ref in now else "done" if ref in done else "todo", env)


# ------------------------------------------------------------------ planning
def plan(steps, c, parts, height_order, natural):
    """[(step id, title, [sub-step refs lists])]: one value per sub-step, near each other, lowest first."""
    out = []
    for s, title, refs in steps:
        groups = {}
        for r in refs:
            p = c.parts[r]
            key = (p["kind"], p["value"], parts[r].lib) if p["kind"] != "TB" else ("TB", r, "")
            groups.setdefault(key, []).append(r)
        subs = []
        for key, rs in sorted(groups.items(), key=lambda kv: (height_order.get(kv[0][0], 5),
                                                               min(parts[r].centre()[0] for r in kv[1]))):
            rs = sorted(rs, key=lambda r: (round(parts[r].centre()[1] / 10), parts[r].centre()[0]))
            chunk = []
            for r in rs:
                trial = chunk + [r]
                xs = [v for q in trial for v in (parts[q].box[0], parts[q].box[2])]
                ys = [v for q in trial for v in (parts[q].box[1], parts[q].box[3])]
                if chunk and (len(trial) > MAX_PARTS or max(xs) - min(xs) > MAX_AREA[0]
                              or max(ys) - min(ys) > MAX_AREA[1]):
                    subs.append(chunk)
                    chunk = [r]
                else:
                    chunk = trial
            if chunk:
                subs.append(chunk)
        out.append((s, title, subs))
    return out


# ------------------------------------------------------------------ pictures
def picture(path, parts, now, done, env, find_text, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Rectangle
    W, H = env["W"], env["H"]
    fig = plt.figure(figsize=(11, 6.2), dpi=90)
    fig.patch.set_facecolor(C["page"])
    fig.text(0.015, 0.95, title, fontsize=15, fontweight="bold", color=C["text"], va="top")

    # find: one of the parts, on its own
    first = parts[now[0]]
    axf = fig.add_axes([0.03, 0.50, 0.22, 0.34])
    axf.set_aspect("equal")
    axf.axis("off")
    x0, y0, x1, y1 = first.box
    span = max(x1 - x0, y1 - y0) / 2 + 2.5
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    axf.set_xlim(cx - span, cx + span)
    axf.set_ylim(cy - span, cy + span)
    draw_part(axf, first, "icon", env)   # full colour, no outline
    fig.text(0.14, 0.49, find_text[0], fontsize=21, fontweight="bold", ha="center", va="top", color=C["text"])
    fig.text(0.14, 0.40, find_text[1], fontsize=10, ha="center", va="top", color=C["text"])
    fig.text(0.14, 0.86, "FIND", fontsize=11, fontweight="bold", ha="center", color="#757575")
    fig.text(0.14, 0.305, "WHERE", fontsize=11, fontweight="bold", ha="center", color="#757575")

    # fit: the close-up
    axz = fig.add_axes([0.285, 0.02, 0.70, 0.86])
    axz.set_aspect("equal")
    axz.axis("off")
    xs = [v for r in now for v in (parts[r].box[0], parts[r].box[2])]
    ys = [v for r in now for v in (parts[r].box[1], parts[r].box[3])]
    mx, my = 9.0, 9.0
    zx0, zx1, zy0, zy1 = min(xs) - mx, max(xs) + mx, min(ys) - my, max(ys) + my
    aspect = (0.70 * 11) / (0.86 * 6.2)
    w, h = max(zx1 - zx0, 45.0), max(zy1 - zy0, 30.0)
    if w / h < aspect:
        w = h * aspect
    else:
        h = w / aspect
    cxz, cyz = (zx0 + zx1) / 2, (zy0 + zy1) / 2
    axz.set_xlim(cxz - w / 2, cxz + w / 2)
    axz.set_ylim(cyz - h / 2, cyz + h / 2)
    draw_board(axz, parts, set(now), set(done), env)
    for r in now:   # name tags
        bx0, by0, bx1, by1 = parts[r].box
        axz.text((bx0 + bx1) / 2, by1 + 0.8, r, color="#fff", fontsize=9, fontweight="bold", ha="center",
                 va="bottom", zorder=14,
                 bbox=dict(boxstyle="round,pad=0.2", fc=C["now"], ec="none"), clip_on=True)
    axz.text(cxz - w / 2 + 1, cyz - h / 2 + 1, "front edge (screw terminals) this way  ↓", color="#fff",
             fontsize=8, zorder=15, va="bottom", clip_on=True)

    # where: the whole board, small
    axm = fig.add_axes([0.055, 0.02, 0.17, 0.17 * 11 / 6.2 * H / W])
    axm.set_aspect("equal")
    axm.axis("off")
    axm.set_xlim(-2, W + 2)
    axm.set_ylim(-2, H + 2)
    from matplotlib.patches import Polygon
    axm.add_patch(Polygon(env["outline"](), closed=True, fc=C["board"], ec="#fff", lw=1.2))
    for r in now:
        bx0, by0, bx1, by1 = parts[r].box
        axm.add_patch(Rectangle((bx0, by0), bx1 - bx0, by1 - by0, fc=C["now"], ec="none"))
    axm.add_patch(Rectangle((cxz - w / 2, cyz - h / 2), w, h, fc="none", ec="#fff", lw=1.5))
    fig.savefig(path, facecolor=C["page"])
    plt.close(fig)
    from PIL import Image
    Image.open(path).convert("RGB").quantize(colors=64).save(path, optimize=True)


def find_words(c, refs, env):
    """('4×', 'second line') for the find panel."""
    p = c.parts[refs[0]]
    what = env["what"](p["kind"])
    n = len(refs)
    big = f"{n}×  {p['value']}" if p["kind"] not in ("TB", "MEGA") else f"{n}×"
    if p["kind"] == "R":
        small = f"{what}: {env['bands_text'](p['value'])}"
    elif p["kind"] == "TB":
        small = env["tb_words"](refs[0])
    elif p["kind"] == "MEGA":
        big, small = "3×", "40-pin male header strips"
    else:
        lab = env["short_value"](p["value"])
        small = what + (f", printed {lab}" if lab != p["value"] else "")
    return big, small
