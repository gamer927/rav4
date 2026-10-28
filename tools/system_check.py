#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""Cross-checks between the firmware, the circuit, the circuit board, the
housings and the docs: the things no single test sees. Run: python3 tools/system_check.py
(also part of `make check`). Exits non-zero on any mismatch.

  1. Every Mega pin the firmware uses is wired (tools/boards.py's Mega pin
     tables) to the signal that means the same thing, and no wired pin is unused.
  2. Every net that reaches a Mega pin carries that pin's signal.
  3. Fail-safe pull-downs: every output that drives a relay or a solenoid
     driver has a 10k pull-down to ground (reset = everything off).
  4. Every switch input has its pull-up, series resistor, capacitor and clamps.
  5. The ON/OFF hardware cut: the relay coils are fed only through the
     switch's power pole (DASH terminals 7-8), protected by the PTC, with a flyback diode, and
     the coil-supply sense reaches A7. The build book's "no shorts" meter
     checks name pairs that really are on different nets.
  6. The TCU box's glands, the board's terminal groups and the cable map agree.
  7. Every screen text fits where it's drawn.
  8. Part counts on the shopping list match the board.
  9. The spec's pin table lists every pin the firmware uses.
 10. Every relative link in the docs points at a file that exists.
 11. The circuit board file (hardware/pcb/) is the circuit: every pad on the
     net the circuit says, KiCad's design-rule check found no errors and
     no unconnected pads, no copper lies under a bolted MOSFET tab, and
     every real part fits at its largest size (tools/part_fit.py). Read as text, so KiCad isn't needed here.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import boards  # noqa: E402
import pcb  # noqa: E402

problems = []
passed = []


def check(ok, what):
    (passed if ok else problems).append(what)


def read(path):
    with open(os.path.join(ROOT, path)) as f:
        return f.read()


ino = read("firmware/tcu/tcu.ino")
power, logic = boards.power_board(), boards.logic_board()

# ---------------------------------------------------------------- 1. pins
fw_pins = {}
for name, val in re.findall(r"constexpr uint8_t (\w+) = (A?\d+);", ino.split("namespace pin {")[1].split("}")[0]):
    fw_pins[name] = val if val.startswith("A") else f"D{val}"
# What each firmware pin must connect to on the hub: (header, signal words).
EXPECT = {
    "VSS": ("A", "SPD"), "TACH": ("A", "TAC"), "RELAY": ("A", "RLY"), "S1": ("A", "DRV1"),
    "S2": ("A", "DRV2"), "SL": ("A", "DRV3"), "BRAKE": ("A", "STP"), "LEVER_D": ("A", "LEVD"),
    "ECU_S1": ("A", "ECU1"), "ECU_S2": ("A", "ECU2"), "ECU_SL": ("A", "ECU3"), "TPS": ("A", "VTA"),
    "TPS_E2": ("A", "E2"), "VBAT": ("A", "VBAT"), "IS_S1": ("A", "IS1"), "IS_S2": ("A", "IS2"),
    "IS_SL": ("A", "IS3"), "COIL_SENSE": ("A", "COILSNS"),
    "UP": ("B", "shifter UP"), "DOWN": ("B", "shifter DOWN"), "ECO": ("B", "ECO"), "SPORT": ("B", "SPORT"),
    "MASTER": ("B", "ON/OFF"), "TEST": ("B", "TEST"), "ATF_PROBE": ("B", "ATF"), "MANUAL": ("B", "MANUAL"),
}
NOT_ON_HUB = {"STATUS_LED": "on-board LED", "ATF": "analog sender, only with ATF_SENSOR = ATF_NTC"}
hub = {"A": boards.HUB_A, "B": boards.HUB_B}
for name, mega in fw_pins.items():
    if name in NOT_ON_HUB:
        continue
    if name not in EXPECT:
        check(False, f"firmware pin {name} ({mega}) has no expected signal in system_check.py")
        continue
    hdr, word = EXPECT[name]
    rows = [r for r in hub[hdr] if r[2] == mega]
    check(len(rows) == 1 and word.lower() in rows[0][1].lower(),
          f"pin {name} = {mega} is hub {hdr}{rows[0][0] if rows else '?'} '{rows[0][1] if rows else 'missing'}' (wants '{word}')")
used = set(fw_pins.values()) | {"5V", "GND", "D20 (SDA)", "D21 (SCL)", "not connected"}
for hdr, rows in hub.items():
    for pin_no, label, mega in rows:
        check(mega in used, f"Mega pin table {hdr}{pin_no} goes to {mega}, which the firmware doesn't use")

# Every Mega pin is either used, or pulled up by the firmware as unused, and
# no pin the firmware calls unused has anything on the circuit board.
unused = re.search(r"kUnusedPins\[\] = \{([^}]*)\}", ino).group(1)
unused = {u if u.startswith("A") else f"D{u}" for u in re.findall(r"A?\d+", unused)}
on_pcb = {p for n in pcb.Circuit().nets for r, p in n["pins"] if r == "MEGA"}
every = {f"D{i}" for i in range(54)} | {f"A{i}" for i in range(16)}
spoken_for = set(fw_pins.values()) | unused | {"D0", "D1", "D20", "D21"}  # USB serial, I²C
check(every <= spoken_for, f"Mega pins neither used nor pulled up as unused: {sorted(every - spoken_for)}")
check(not unused & on_pcb, f"pins the firmware pulls up as unused are wired on the board: {sorted(unused & on_pcb)}")
check(not unused & set(fw_pins.values()), f"pins both used and 'unused': {sorted(unused & set(fw_pins.values()))}")

# ------------------------------------------------- 2. nets at the headers


def nets_at(board, node):
    return {w["net"] for w in board.wires if node in w["nodes"]}


for board, hdr, rows in ((power, "JA", boards.HUB_A), (logic, "JB", boards.HUB_B)):
    for pin_no, label, mega in rows:
        nets = nets_at(board, f"{hdr}.{pin_no}")
        if mega == "not connected":
            check(not nets, f"{board.name} {hdr}.{pin_no} should be empty but is on {nets}")
        elif mega in ("5V", "GND"):
            check(nets == {mega}, f"{board.name} {hdr}.{pin_no} should be {mega}, is {nets}")
        else:
            m = mega.split()[0]
            ok = bool(nets) and all(m in n or "SDA" in n and m == "D20" or "SCL" in n and m == "D21" or
                                    n.startswith(("IN", "COILSNS", "IS", "VBAT", "RLY", "DRV", "LED", "ATF"))
                                    for n in nets)
            check(ok, f"{board.name} {hdr}.{pin_no} ({mega}) carries {nets or 'nothing'}")

# ------------------------------------------------- 3. fail-safe pull-downs


def resistor_between(board, net_a_nodes, net_b, value):
    for p in board.parts:
        if p.fp["kind"] != "R" or p.value != value:
            continue
        ends = [f"{p.ref}.1", f"{p.ref}.2"]
        nets = [nets_at(board, e) for e in ends]
        if any(net_a_nodes & n for n in nets) and any(net_b in n for n in nets):
            return True
    return False


for hdr_pin, what in (("JA.7", "relays (D6)"), ("JA.8", "S1 driver (D8)"), ("JA.9", "S2 driver (D9)"),
                      ("JA.10", "SL driver (D10)")):
    check(resistor_between(power, nets_at(power, hdr_pin), "GND", "10k"),
          f"{what}: 10k pull-down to ground, so a reset or a dead Mega means off")

# ------------------------------------------------- 4. switch inputs
for i in range(1, 8):
    parts = {p.ref: p for p in logic.parts}
    ok = all(r in parts for r in (f"RU{i}", f"RS{i}", f"CF{i}", f"DH{i}", f"DL{i}"))
    ok = ok and parts[f"RU{i}"].value == "2.2k" and parts[f"RS{i}"].value == "1k"
    check(ok, f"switch input {i}: 2.2k pull-up, 1k series, 100 nF, two BAT85 clamps")

# ------------------------------------------------- 5. the ON/OFF hardware cut
coil_nodes = {w_node for w in power.wires if w["net"] == "+12 COIL" for w_node in w["nodes"]}
drv_nodes = {w_node for w in power.wires if w["net"] == "+12 DRV" for w_node in w["nodes"]}
for k in ("K1", "K2", "K3"):
    check(f"{k}.COIL1" in coil_nodes and f"{k}.COIL1" not in drv_nodes,
          f"{k}'s coil is fed only through the ON/OFF switch's power pole (+12 COIL)")
check("U1.10" in coil_nodes, "the ULN2803's clamp (COM) is on the switched coil supply")
check("J5b.1" in coil_nodes, "+12 COIL comes back from the switch on J5b")
check(any(w["net"] == "SWITCH FEED" and {"F2.2", "J5a.1"} <= set(w["nodes"]) for w in power.wires),
      "the feed to the switch (J5a) goes through the PTC F2")
check(any(w["net"] == "+12 DRV" and "F2.1" in w["nodes"] for w in power.wires), "F2 is fed from +12 DRV")
check("DF.K" in coil_nodes, "a flyback diode (DF) on the coil supply for when the switch opens")
check("JA.25" in {n for w in power.wires if w["net"] == "COILSNS" for n in w["nodes"]},
      "the coil supply sense reaches A7 (JA.25)")
check("TVS3.K" in {n for w in power.wires if w["net"] == "5V" for n in w["nodes"]},
      "a TVS across the 5 V rail (a shorted regulator blows the logic fuse)")

# The build book's "no shorts" pairs: each end is wired, and the two ends are
# on different nets (else the book would call a correct beep a short).
for board in (power, logic):
    for a, b, what in board.shorts:
        na, nb = nets_at(board, a), nets_at(board, b)
        check(bool(na) and bool(nb) and not (na & nb), f"{board.name} no-short pair {a}/{b} ({what}): {na} vs {nb}")

# ------------------------------------------------- 6. glands, terminals, cable map
scad = read("hardware/tcu_enclosure.scad")
glands = re.findall(r'\["(\w+)", 1[25]\.\d\]', scad.split("gland_size = [")[1].split(";")[0])
cable_map = re.findall(r'\("(\w+)", "#', read("tools/cable_map.py"))
check(glands == cable_map, f"TCU box glands {glands} = cable map {cable_map}")
board_terms = [g for g, _, _, _ in pcb.TERMINALS]
check(board_terms == glands, f"board terminal groups {board_terms}, left to right, = the box's glands")
env_terms = re.findall(r'\["(\w+)", [\d.]+, [\d.]+, [\d.]+\]', read("hardware/pcb_envelope.scad").split("pcb_terminals")[1])
check(env_terms == board_terms, f"hardware/pcb_envelope.scad's terminals {env_terms} are up to date")

# ------------------------------------------------- 7. screen text widths
for call in re.findall(r'say\(([^;]*)\);', ino):
    strs = re.findall(r'"([^"]*)"', call)
    if not strs or "t.buf" in call:
        continue
    head, lines = strs[0], strs[1:]
    check(len(head) <= 10, f"headline '{head}' fits 10 big characters")
    for ln in lines:
        check(len(ln) <= 14, f"'{ln}' fits 14 characters beside the gear")
for fn in ("promptText", "inhibitText"):
    body = ino.split(f"const char* {fn}(")[1].split("\n}")[0]
    for txt in re.findall(r'return "([^"]*)"', body):
        check(len(txt) <= 21, f"{fn} '{txt}' fits a 21-character line")
for txt in re.findall(r'return "([^"]*)"', ino.split("const char* faultWords(")[1].split("\n}")[0]):
    check(len(txt) <= 14, f"fault '{txt}' fits 14 characters")
for txt in re.findall(r'line\("([^"]*)"\)', ino) + re.findall(r'setLine\(\d, "([^"]*)"\)', ino):
    check(len(txt) <= 21, f"'{txt}' fits a 21-character line")
names = re.findall(r'"([^"]+)"', read("firmware/tcu/self_test.cpp").split("kNames[T_COUNT] = {")[1].split("};")[0])
valued = {"BATTERY", "GAS PEDAL", "TEMP PROBE", "SOLENOID 1", "SOLENOID 2", "LOCKUP SOL", "RELAYS", "ON/OFF CUT"}
for n in names:
    check(len(n) <= (10 if n in valued else 15), f"self-test name '{n}' fits its result line")

# ------------------------------------------------- 8. shopping list counts
shop = read("tools/shopping_list.py")
circuit = pcb.Circuit()
counts = {}
for p in circuit.parts.values():
    counts[p["value"]] = counts.get(p["value"], 0) + 1


def need(item_words):
    for row in re.findall(r'\("\d\. [^"]*", "([^"]*)", "([^"]*)"', shop):
        if item_words in row[0]:
            return row[1]
    return None


check(need("BAT85") == str(counts.get("BAT85")), f"BAT85: list says {need('BAT85')}, board uses {counts.get('BAT85')}")
check(need("UF4007") == str(counts.get("UF4007")), f"UF4007: list says {need('UF4007')}, board uses {counts.get('UF4007')}")
check(need("100 nF") == str(counts.get("100 nF")), f"100 nF: list says {need('100 nF')}, board uses {counts.get('100 nF')}")
# 10k: the board's, plus the screen's reset pull-up and the bench gas-pedal stand-in's two
check(need("Extra 10 k") == f"{counts.get('10k') + 3} total",
      f"10k: list says {need('Extra 10 k')}, board uses {counts.get('10k')} + 3 off the board")
zeners = sum(v for k, v in counts.items() if k.startswith("1N47"))
check(need("zener diode kit") == str(zeners), f"zeners: list says {need('zener diode kit')}, board uses {zeners}")
check(need("PTC") == str(counts.get("PTC 0.25 A")), "PTC count")
check(need("P6KE6.8A") == str(counts.get("P6KE6.8A")), "P6KE6.8A count")
check("2-pole" in shop, "the ON/OFF switch on the shopping list is 2-pole")

# ------------------------------------------------- 9. spec pin table
spec = read("docs/02-revised-spec.md")
for name, mega in fw_pins.items():
    check(re.search(rf"^\| [^|]*\b{mega}\b", spec, re.M) is not None, f"spec pin table lists {mega} ({name})")

# ------------------------------------------------- 10. links
for f in glob.glob(os.path.join(ROOT, "*.md")) + glob.glob(os.path.join(ROOT, "docs", "*.md")) + \
        glob.glob(os.path.join(ROOT, "hardware", "**", "*.md"), recursive=True):
    d = os.path.dirname(f)
    for m in re.finditer(r"\]\(([^)#\s]+)(#[^)]*)?\)", open(f).read()):
        t = m.group(1)
        if t.startswith("http") or "`" in t or t == "tps":
            continue
        check(os.path.exists(os.path.join(d, t)), f"{os.path.relpath(f, ROOT)} links to {t}")

# ------------------------------------------------- 11. the circuit board


def board_pads(text):
    """{(ref, pad): net} from a .kicad_pcb file."""
    out = {}
    for fp in text.split("\n  (footprint ")[1:]:
        ref = re.search(r'\(fp_text reference "([^"]+)"', fp).group(1)
        for pd in fp.split("(pad ")[1:]:
            num = re.match(r'"([^"]*)"', pd).group(1)
            net = re.search(r'\(net \d+ "([^"]*)"\)', pd.split("(pad ")[0])
            if num:
                out.setdefault((ref, num), set()).add(net.group(1) if net else None)
    return out


pcb_text = read("hardware/pcb/rav4-tcu.kicad_pcb")
on_board = board_pads(pcb_text)
want = {}
for n in circuit.nets:
    for r, p in n["pins"]:
        want[("A1" if r == "MEGA" else r, p)] = n["name"]
for key, net in sorted(want.items()):
    check(on_board.get(key) == {net}, f"board pad {key[0]}.{key[1]} is on {net} (file has {on_board.get(key)})")
extra = [k for k, v in on_board.items() if k not in want and v != {None}]
check(not extra, f"board pads on a net the circuit doesn't have: {extra[:8]}")
check(sorted(re.findall(r'\(fp_text reference "([^"]+)"', pcb_text)) == sorted(set(circuit.parts) | {f"H{i + 1}" for i in range(len(pcb.MOUNT_HOLES))}),
      "the board has exactly the circuit's parts, plus the mounting holes")
drc_text = read("hardware/pcb/drc.txt")
check("** Found 0 unconnected pads **" in drc_text, "KiCad DRC: no unconnected pads")
check("Severity: error" not in drc_text, "KiCad DRC: no errors")
check(os.path.exists(os.path.join(ROOT, "hardware", "pcb", "rav4-tcu-gerbers.zip")), "the Gerber zip is there to upload")
mosfets = len([r for r in circuit.parts if r.startswith("Q")])
check(pcb_text.count("(copperpour not_allowed)") >= 2 * mosfets,
      "no copper under each MOSFET's bolted tab (top) or its bolt (both sides)")
fit_report = read("hardware/pcb/part_fit.md")
check("**Result: 0 problems.**" in fit_report, "every real part fits the board at its largest size (hardware/pcb/part_fit.md)")
check(all(r in fit_report for r in ("ACS712", "G2RL", "TO-220", "DO-201", "R-78HB")), "the part fit report covers every part type")
check("pcb_standoff_h = 18.6;" in read("hardware/pcb_envelope.scad"),
      "the board's posts allow for the header strips' plastic on top of the Mega's sockets")

print(f"system check: {len(passed)} passed, {len(problems)} problems")
for p in problems:
    print("  PROBLEM:", p)
sys.exit(1 if problems else 0)
