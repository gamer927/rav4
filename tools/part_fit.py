#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""Will the real parts fit the circuit board? Called by tools/pcb.py.

KiCad's footprints describe a typical part. This checks the board against the
real parts on the shopping list instead, using the largest size each one is
sold in (datasheet maximums, and the biggest variants found from sellers):

  1. Legs: every hole is wider than the thickest leg that goes in it (a leg's
     diagonal, for flat and square legs), by at least 0.1 mm.
  2. Leg spacing: a part lying flat has room to bend its legs down beside its
     body (at least 1 mm each end); a standing part's legs match its holes, or
     bend a little to reach them.
  3. Bodies: every part's body, at its largest, clears every other part's
     body, the screw heads on the mounting holes, the board's edge and the
     notch over the Mega's jacks.
  4. Under the board: the Mega's tallest parts clear the board's trimmed legs.
  5. Heights: each part's tallest variant, for the box's fit check.

The result is written to hardware/pcb/part_fit.md, in words.
"""
import math

# Worst-case real parts: (what, max leg thickness or diagonal, body, height above the board, source)
# body: ("axial", length, diameter)       lies flat between its two holes
#       ("box", along, across)            stands between its two holes (disc capacitor, PTC)
#       ("round", diameter, [spacings])   standing can between its two holes
#       ("fab", grow)                     KiCad's own outline of the part, grown by `grow` mm all round
#       ("acs712", length, width, end)    module: pins `end` mm from one end, body towards the LINK holes
#       ("gy521", length, width, end)     module lying flat on its 8-pin header
REAL = {
    "R": ("1/4 W metal film resistor", 0.6, ("axial", 6.5, 2.5), 3.0,
          "1/4 W metal film: 6.0-6.5 mm long, 2.3-2.5 mm thick, 0.6 mm legs (Yageo MFR-25 type)"),
    "DO-35": ("diode, DO-35 glass (BAT85, 1N4148)", 0.55, ("axial", 4.25, 2.0), 2.5,
              "DO-35: body 4.25 max x 2.0 max, legs 0.55 max (NXP BAT85, Vishay 1N4148)"),
    "DO-41": ("diode, DO-41 (1N47xxA zener, UF4007)", 0.86, ("axial", 5.2, 2.7), 3.2,
              "DO-41: body 5.2 max x 2.7 max, legs 0.86 max (Vishay 1N47xxA, UF4007)"),
    "DO-15": ("TVS, DO-15 (P6KE6.8A)", 0.9, ("axial", 7.6, 3.6), 4.1,
              "DO-15: body 7.6 max x 3.6 max, legs 0.9 max (Littelfuse P6KE)"),
    "DO-201": ("diode, DO-201 (1N5822, 1.5KE27A)", 1.32, ("axial", 9.5, 5.3), 5.8,
               "DO-201AD: body 9.5 max x 5.3 max, legs 1.32 max (1N5822; 1.5KE is 1.07)"),
    "C100n": ("100 nF ceramic, 5 mm legs", 0.55, ("box", 5.5, 3.5), 8.0,
              "radial MLCC or disc: up to 5.5 wide x 3.5 thick, legs 0.5, 5.0/5.08 or 2.54 apart"),
    "PTC": ("PTC 0.25 A (Bourns MF-R025)", 0.55, ("box", 7.4, 3.0), 13.5,
            "MF-R025: 7.4 max wide, 3.0 max thick, 12.7 max tall, 24 AWG legs 5.1 apart"),
    "C100u": ("100 uF 50 V electrolytic", 0.65, ("round", 10.0, [3.5, 5.0, 2.5]), 16.0,
              "100 uF 50 V: 8 x 11.5 (3.5 mm legs) is usual, 10 x 12.5 (5 mm) and 6.3 x 11 (2.5 mm) exist"),
    "C47u": ("47 uF 16 V electrolytic", 0.55, ("round", 6.3, [2.5, 2.0]), 12.5,
             "47 uF 16 V: 5 x 11 (2 mm legs) or 6.3 x 11 (2.5 mm)"),
    "DIP": ("chip in a DIP package (PC817, LM393, ULN2803A)", 0.61, ("fab", 0.3), 5.5,
            "DIP: 0.46-0.56 x 0.25 mm legs (diagonal 0.61), 7.62 mm rows, body 6.6 max wide, 5.3 max tall"),
    "K": ("Omron G2RL-2 relay", 0.94, ("fab", 0.15), 15.7,
          "Omron G2RL: 29 x 12.7 x 15.7 max, terminals 0.8 x 0.5, holes 1.3 +/- 0.1 recommended"),
    "Q": ("IRF4905, TO-220, lying flat and bolted", 1.03, ("fab", 0.3), 8.0,
          "TO-220: legs up to 0.89 x 0.53 (diagonal 1.03), body 10.4 max wide; 4.7 thick + bolt head"),
    "REG": ("RECOM R-78HB5.0-0.5", 0.64, ("fab", 0.3), 18.0,
            "R-78HB-0.5: 11.5 x 8.5 x 17.5, SIP3 on 2.54 mm"),
    "TB3.5": ("screw terminal, 3.5 mm (Phoenix PT or DG350/KF350)", 1.05, ("fab", 0.5), 11.0,
              "3.5 mm terminals: pins up to 1.0 x 0.8 or 1.05 round, 7.2-8.0 deep, 8.5-10.5 tall"),
    "TB5.08": ("screw terminal, 5.08 mm (Phoenix MKDS or KF301/DG301)", 1.15, ("fab", 0.5), 11.0,
               "5.08 mm terminals: pins up to 1.15, 7.6-8.1 deep, 10-10.5 tall"),
    "ACS": ("ACS712-05B module (5 A)", 0.91, ("acs712", 33.0, 15.0, 1.0), 15.0,
            "5 A modules: 31 x 13 mm (27.5 x 11.6 seen too); checked at 33 x 15. 0.64 mm square header pins"),
    "GYRO": ("GY-521 module", 0.91, ("gy521", 21.5, 16.5, 1.3), 7.0,
             "GY-521: 20-21.2 x 15.5-16.4 mm, 2.54 mm header, lying flat on its header"),
    "MEGA": ("male header strip into the Mega", 0.91, None, 0.0,
             "2.54 mm male header: 0.64 mm square pins, diagonal 0.91"),
}
LINK_WIRE = 1.0           # the ACS712 modules' current links: 18 AWG solid or a 1 mm leg offcut
SCREW_HEAD = 6.0          # M3 pan head on a mounting hole
MIN_GAP = 0.2             # bodies closer than this count as touching
BEND = 1.0                # a lying part's leg must run straight this far out of its body before it bends
# Under the board: board underside to the Mega's top = the Mega socket + the header strip's plastic.
UNDER_GAP = 8.5 + 2.5
MEGA_TALLEST = 8.5        # its ICSP header pins (2.5 + 6); the capacitors are 7.7 max
TRIMMED_LEG = 1.5         # legs trimmed to this under the board


def kind_of(part, fp_name):
    k, v = part["kind"], part["value"]
    if k == "R":
        return "R"
    if k == "D":
        for pkg in ("DO-35", "DO-41", "DO-15", "DO-201"):
            if pkg in fp_name:
                return pkg
    if k == "C":
        return "C100n"
    if k == "PTC":
        return "PTC"
    if k == "CE":
        return "C100u" if v.startswith("100") else "C47u"
    if k == "IC":
        return "DIP"
    if k in ("K", "Q", "REG"):
        return k
    if k == "TB":
        return "TB3.5" if "3.5" in fp_name else "TB5.08"
    if k == "MOD":
        return "ACS" if part["ref"].startswith("CS") else "GYRO"
    if k == "MEGA":
        return "MEGA"
    raise KeyError(part["ref"])


def _rect_gap(a, b):
    dx = max(b[0] - a[2], a[0] - b[2], 0.0)
    dy = max(b[1] - a[3], a[1] - b[3], 0.0)
    overlap = a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
    return -1.0 if overlap else math.hypot(dx, dy)


def _around(cx, cy, along, across, ux, uy):
    """Axis-aligned box of a body centred at (cx, cy), `along` the unit vector (ux, uy)."""
    hx = abs(ux) * along / 2 + abs(uy) * across / 2
    hy = abs(uy) * along / 2 + abs(ux) * across / 2
    return cx - hx, cy - hy, cx + hx, cy + hy


def check(board, circuit, bxy, outline_inside, holes, notch, fab_box):
    """Returns (problems, report lines, {ref: height})."""
    import pcbnew
    problems, rows, heights = [], [], {}
    bodies = {}
    worst = {}   # kind -> (smallest hole margin, where)
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref.startswith("H"):
            continue
        part = circuit.parts[ref]
        name = str(fp.GetFPID().GetLibItemName())
        k = kind_of(part, name)
        what, leg, body, height, _ = REAL[k]
        heights[ref] = height
        pads = [p for p in fp.Pads() if p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH]
        # 1. legs into holes
        for p in pads:
            drill = min(p.GetDrillSize().x, p.GetDrillSize().y) / 1e6
            this_leg = LINK_WIRE if p.GetNumber() in ("IP+", "IP-") else leg
            margin = drill - this_leg
            if k not in worst or margin < worst[k][0]:
                worst[k] = (margin, f"{ref}.{p.GetNumber()}: {drill:.2f} mm hole, {this_leg:.2f} mm leg")
            if margin < 0.1 - 1e-9:
                problems.append(f"{ref} pin {p.GetNumber()}: a {this_leg} mm leg in a {drill:.2f} mm hole")
        if body is None:
            continue
        pos = {p.GetNumber(): bxy(*p.GetPosition()) for p in pads}
        # 2. leg spacing, 3. the body
        if body[0] in ("axial", "box", "round"):
            (x1, y1), (x2, y2) = list(pos.values())[:2]
            span = math.hypot(x2 - x1, y2 - y1)
            ux, uy = (x2 - x1) / span, (y2 - y1) / span
            cx, cy = (x1 + x2) / 2, (y1 + y2) / 2
            if body[0] == "axial":
                room = (span - body[1]) / 2
                if room < BEND:
                    problems.append(f"{ref}: holes {span:.2f} mm apart, body {body[1]} mm: no room to bend the legs")
                worst.setdefault(k + " bend", (room, ref))
                if room < worst[k + " bend"][0]:
                    worst[k + " bend"] = (room, ref)
                box = _around(cx, cy, body[1], body[2], ux, uy)
            elif body[0] == "box":
                box = _around(cx, cy, body[1], body[2], ux, uy)
            else:
                d, spacings = body[1], body[2]
                if min(abs(s - span) for s in spacings) > 1.6:
                    problems.append(f"{ref}: holes {span:.2f} mm apart, legs come {spacings} apart")
                box = (cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2)
        elif body[0] == "fab":
            x0, y0, x1, y1 = fab_box(fp)
            xs = [x for x, _ in pos.values()]
            ys = [y for _, y in pos.values()]
            g = body[1]
            box = (min(x0, min(xs) - 0.5) - g, min(y0, min(ys) - 0.5) - g,
                   max(x1, max(xs) + 0.5) + g, max(y1, max(ys) + 0.5) + g)
        elif body[0] == "acs712":
            length, width, end = body[1:]
            hx, hy = pos["OUT"]
            lx, ly = [(a + b) / 2 for a, b in zip(pos["IP+"], pos["IP-"])]
            dist = math.hypot(lx - hx, ly - hy)
            ux, uy = (lx - hx) / dist, (ly - hy) / dist
            far = length - end
            cx, cy = hx + ux * (far - end) / 2, hy + uy * (far - end) / 2
            box = _around(cx, cy, length, width, ux, uy)
            link_room = dist - 1.6 - far
            if link_room < 0.5:
                problems.append(f"{ref}: a {length} mm module would cover its LINK holes")
            worst["ACS link"] = (link_room, f"{ref}: {link_room:.1f} mm between a {length} mm module's end "
                                            "and its LINK holes")
        elif body[0] == "gy521":
            length, width, end = body[1:]
            xs = [x for x, _ in pos.values()]
            ys = [y for _, y in pos.values()]
            fx0, fy0, fx1, fy1 = fab_box(fp, silk=True)
            cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
            # it lies on the side of its header row where its outline is
            sx, sy = (fx0 + fx1) / 2 - cx, (fy0 + fy1) / 2 - cy
            if abs(max(xs) - min(xs)) > abs(max(ys) - min(ys)):   # header row along x
                d = 1 if sy > 0 else -1
                box = (cx - length / 2, cy - d * end, cx + length / 2, cy + d * (width - end))
            else:
                d = 1 if sx > 0 else -1
                box = (cx - d * end, cy - length / 2, cx + d * (width - end), cy + length / 2)
            box = (min(box[0], box[2]), min(box[1], box[3]), max(box[0], box[2]), max(box[1], box[3]))
        bodies[ref] = box
        x0, y0, x1, y1 = box
        if not outline_inside(box):
            problems.append(f"{ref}: its body at its largest goes over the board's edge")
        if x0 < notch[0] and y1 > notch[1]:
            problems.append(f"{ref}: its body reaches into the notch over the Mega's jacks")
        for hx, hy in holes:
            r = SCREW_HEAD / 2
            if _rect_gap(box, (hx - r, hy - r, hx + r, hy + r)) < MIN_GAP:
                problems.append(f"{ref}: its body at its largest touches the screw head at ({hx}, {hy})")

    refs = sorted(bodies)
    closest = {}
    for i, a in enumerate(refs):
        for b in refs[i + 1:]:
            gap = _rect_gap(bodies[a], bodies[b])
            for r, o in ((a, b), (b, a)):
                if r not in closest or gap < closest[r][0]:
                    closest[r] = (gap, o)
            if gap < MIN_GAP:
                problems.append(f"{a} and {b}: at their largest, their bodies "
                                + ("overlap" if gap < 0 else f"are only {gap:.2f} mm apart"))

    under = UNDER_GAP - MEGA_TALLEST - TRIMMED_LEG
    if under < 0.5:
        problems.append(f"under the board: only {under:.1f} mm between trimmed legs and the Mega's parts")

    # the report
    rows.append("| Part | Leg vs hole (tightest) | Closest neighbour, at the largest size | Height | Sizes used |")
    rows.append("|---|---|---|---|---|")
    by_kind = {}
    for ref in sorted(heights):
        part = circuit.parts[ref]
        by_kind.setdefault(kind_of(part, str(board.FindFootprintByReference(ref).GetFPID().GetLibItemName())),
                           []).append(ref)
    for k, refs_k in by_kind.items():
        what, leg, body, height, source = REAL[k]
        margin, where = worst.get(k, (None, ""))
        leg_txt = f"{margin:+.2f} mm ({where})" if margin is not None else ""
        near = [closest[r] + (r,) for r in refs_k if r in closest]
        if near:
            g, o, r = min(near)
            near_txt = f"{g:.1f} mm ({r} to {o})" if g >= 0 else f"OVERLAP ({r} and {o})"
        else:
            near_txt = "under the board"
        rows.append(f"| {what} ×{len(refs_k)} | {leg_txt} | {near_txt} | {height:g} mm | {source} |")
    extra = []
    for k in sorted(worst):
        if k.endswith(" bend"):
            extra.append(f"- {REAL[k[:-5]][0]}: {worst[k][0]:.2f} mm of straight leg each end before the bend "
                         f"({worst[k][1]}), needs {BEND} mm.")
    if "ACS link" in worst:
        extra.append(f"- ACS712 modules: {worst['ACS link'][1]}.")
    extra.append(f"- Under the board: {UNDER_GAP} mm from the board to the Mega, its tallest parts "
                 f"{MEGA_TALLEST} mm, legs trimmed to {TRIMMED_LEG} mm: {under:.1f} mm to spare.")
    return problems, rows, extra, heights
