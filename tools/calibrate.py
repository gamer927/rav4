#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""Calibration helper: teaches the TCU your car, step by step.

Plug a laptop into the TCU's USB socket and run one step at a time:

    python3 tools/calibrate.py bench     # build book part 10: current sensors
    python3 tools/calibrate.py parked    # in the car, key ON, engine OFF
    python3 tools/calibrate.py drive     # record a drive, then work out the numbers
    python3 tools/calibrate.py unlock    # when all three are done
    python3 tools/calibrate.py status    # what's done so far

Each step tells you what to do, records the TCU's USB log into the
calibration/ folder, works out the settings, shows them next to the old ones
and (if you say yes) writes them into firmware/tcu/config.h. Then upload the
firmware again (Arduino IDE: Upload).

Re-check a saved log without the car:  calibrate.py drive --file calibration/drive-....log
Needs Python 3 and pyserial (pip install pyserial) for the live steps.
"""
import argparse
import datetime
import json
import os
import re
import statistics
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(ROOT, "firmware", "tcu", "config.h")
CAL_DIR = os.path.join(ROOT, "calibration")
PROGRESS = os.path.join(CAL_DIR, "progress.json")

COLS = "ms,ctrl,mode,gear,lockup,mph,rpm,rpm_per_mph,tps,atf,volts,faults,warnings,tps_v".split(",")
# Published ratios of the 4-speed: 1st, 2nd and 4th relative to 3rd (direct, 1.000).
RATIO_TO_3RD = {1: 2.810, 2: 1.549, 4: 0.734}
DUMMY_OHMS = 15.0


class CalError(Exception):
    """Something in the recording doesn't add up: say what, and what to do."""


# ---------------------------------------------------------------- the log
class Log:
    def __init__(self, lines):
        self.rows = []      # dicts of floats
        self.marks = []     # (row index, "name begin"/"name end")
        self.cal = {}       # the settings the firmware was built with
        self.bit = []       # self-test tables: list of {name: (result, value)}
        self.shifts = []    # "# ECU shift ..." lines
        cols = COLS
        for raw in lines:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#!"):
                self.marks.append((len(self.rows), line[2:].strip()))
            elif line.startswith("# cols "):
                cols = line[7:].split(",")
            elif line.startswith("# cal "):
                for k, v in re.findall(r"(\w+)=([-\d.]+)", line):
                    self.cal[k] = float(v)
            elif re.match(r"# (PBIT|IBIT)", line):
                self.bit.append({})
            elif line.startswith("#   ") and self.bit:
                m = re.match(r"#\s+(.+?)\s+(OK|BAD|CHECK|SKIP|--)(?:\s+([-\d.]+))?", line)
                if m:
                    self.bit[-1][m.group(1)] = (m.group(2), float(m.group(3)) if m.group(3) else None)
            elif line.startswith("# ECU"):
                self.shifts.append(line[2:])
            elif not line.startswith("#"):
                parts = line.split(",")
                if len(parts) != len(cols):
                    continue  # a line cut short when the cable was plugged in
                try:
                    self.rows.append({c: float(v) for c, v in zip(cols, parts)})
                except ValueError:
                    continue

    def between(self, name):
        """The rows recorded between '#! name begin' and '#! name end' (the last such pair)."""
        begin = end = None
        for i, m in self.marks:
            if m == f"{name} begin":
                begin, end = i, None
            elif m == f"{name} end" and begin is not None:
                end = i
        if begin is None or end is None:
            return []
        return self.rows[begin:end]


def steady_segments(rows, min_s=10.0, spread=1.5, key="mph"):
    """Stretches where `key` stays within `spread` for at least `min_s` seconds."""
    segs = []
    i = 0
    while i < len(rows):
        lo = hi = rows[i][key]
        j = i + 1
        while j < len(rows):
            v = rows[j][key]
            if max(hi, v) - min(lo, v) > spread:
                break
            lo, hi = min(lo, v), max(hi, v)
            j += 1
        if (rows[j - 1]["ms"] - rows[i]["ms"]) / 1000.0 >= min_s:
            segs.append(rows[i:j])
            i = j
        else:
            i += 1
    return segs


# ---------------------------------------------------------------- the maths
def bench_result(log, supply_v, ohms=DUMMY_OHMS):
    """IS_AMPS_PER_VOLT from the solenoid currents the last self-test measured."""
    old = log.cal.get("IS_AMPS_PER_VOLT")
    if old is None:
        raise CalError("The log has no '# cal' line. Upload the latest firmware, then try again.")
    tables = [t for t in log.bit if any(k.startswith("SOLENOID") or k == "LOCKUP SOL" for k in t)]
    readings = []
    for t in reversed(tables):
        readings = [t[k][1] for k in ("SOLENOID 1", "SOLENOID 2", "LOCKUP SOL") if k in t and t[k][1] is not None]
        if len(readings) == 3:
            break
    if len(readings) != 3:
        raise CalError("No self-test results with all three solenoid currents in the log. Hold TEST for "
                       "2 s and let the self-test run to the end while this records.")
    shown = statistics.mean(readings)
    if shown < 0.05:
        raise CalError("The self-test measured no current at all. Check the J2d-J4a, J2e-J4b, J2f-J4c "
                       "test wires and the big resistors (build book part 10).")
    true_a = supply_v / ohms
    new = old * true_a / shown
    if abs(new / old - 1) > 0.35:
        raise CalError(f"The readings ({shown:.2f} A) are far from the expected {true_a:.2f} A. That's "
                       "a wiring problem, not calibration: check the current sensors CS1-CS3 (step P5).")
    spread = max(readings) - min(readings)
    notes = []
    if spread > 0.1 * true_a:
        notes.append(f"The three channels differ by {spread:.2f} A: check the weakest one's wiring.")
    return {"IS_AMPS_PER_VOLT": round(new, 3)}, {"readings": readings, "expected": true_a}, notes


def parked_result(log, meter_v, cfg):
    """Gas pedal closed/full voltages and the battery divider."""
    up = [r["tps_v"] for r in log.between("pedal_up") if "tps_v" in r]
    down = [r["tps_v"] for r in log.between("pedal_down") if "tps_v" in r]
    batt = [r["volts"] for r in log.between("battery")]
    if len(up) < 10 or len(down) < 10 or len(batt) < 10:
        raise CalError("Not enough readings. Is the USB cable plugged in, and the latest firmware uploaded?")
    closed, wot = statistics.median(up), statistics.median(down)
    lo, hi = cfg["TPS_VALID_MIN_V"], cfg["TPS_VALID_MAX_V"]
    if wot - closed < 2.0:
        raise CalError(f"The pedal only changed the sensor from {closed:.2f} V to {wot:.2f} V. Expected about "
                       "0.6 V to 4 V. Check the VTA and E2 wires (part 0 table), and press the pedal all "
                       "the way down when asked.")
    if closed < lo + 0.05 or wot > hi - 0.05:
        raise CalError(f"The sensor reads {closed:.2f}-{wot:.2f} V, too close to the fault limits "
                       f"({lo:.2f}-{hi:.2f} V): it would report a pedal fault. Check the VTA/E2 wiring.")
    logged = statistics.mean(batt)
    old_div = log.cal.get("VBAT_DIVIDER", cfg["VBAT_DIVIDER"])
    new_div = old_div * meter_v / logged
    if abs(new_div / old_div - 1) > 0.1:
        raise CalError(f"The TCU reads {logged:.1f} V but your meter says {meter_v:.1f} V: more than 10 % "
                       "apart. Check the meter is on the battery terminals, and resistors RB1/RB2 (step P1).")
    notes = []
    if not 0.3 <= closed <= 0.9:
        notes.append(f"Pedal-up voltage {closed:.2f} V is unusual (expected 0.3-0.9 V). It works, but "
                     "check the sensor isn't worn.")
    return ({"TPS_CLOSED_V": round(closed, 2), "TPS_WOT_V": round(wot, 2), "VBAT_DIVIDER": round(new_div, 3)},
            {"tps_up": closed, "tps_down": wot, "battery_logged": logged}, notes)


def drive_result(log, gps_speeds, cfg):
    """Speed sensor scaling and rpm per mph in each gear."""
    old_ppm = log.cal.get("VSS_PULSES_PER_MILE", cfg["VSS_PULSES_PER_MILE"])
    rows = [r for r in log.rows if r["ctrl"] == 0]  # the factory computer driving (switch OFF)
    if len(rows) < 600:
        raise CalError("The recording is too short. Drive for at least 10 minutes with the switch OFF.")
    segs = steady_segments(rows)
    factors = []
    for gps in gps_speeds:
        near = [s for s in segs if abs(statistics.mean(r["mph"] for r in s) / gps - 1) < 0.15]
        if not near:
            raise CalError(f"Couldn't find 10 seconds of steady speed near {gps:g} mph. Drive again and "
                           f"hold {gps:g} mph on the GPS for at least 15 seconds.")
        best = max(near, key=len)
        factors.append(statistics.mean(r["mph"] for r in best) / gps)
    if max(factors) - min(factors) > 0.03:
        raise CalError("The speeds disagree with each other by more than 3 %: one GPS reading was probably "
                       "off. Drive again, holding each speed steady for longer.")
    factor = statistics.mean(factors)  # TCU mph / true mph
    new_ppm = old_ppm * factor

    # rpm per true mph with the converter locked, from steady stretches in 3rd and 4th
    k = {}
    for gear in (3, 4):
        locked = [r for r in rows if r["gear"] == gear and r["lockup"] == 1 and r["mph"] > 25]
        vals = []
        for s in steady_segments(locked, min_s=5.0):
            vals += [r["rpm"] / (r["mph"] / factor) for r in s]
        if len(vals) >= 30:
            k[gear] = statistics.median(vals)
    if 3 not in k:
        raise CalError("No steady driving in 3rd with lockup on. Press the O/D OFF button on the gear lever "
                       "and hold a steady 45 mph for 20 seconds, then record again.")
    if 4 not in k:
        raise CalError("No steady driving in 4th with lockup on. Hold a steady 60 mph (O/D on) for 20 "
                       "seconds, then record again.")
    ratio = k[4] / k[3]
    if abs(ratio - RATIO_TO_3RD[4]) > 0.03:
        raise CalError(f"4th/3rd came out as {ratio:.3f}, not about 0.734. Your gear ratios may differ from "
                       "the published ones: don't unlock until that's understood (spec section 8).")
    k3 = k[3]
    rpm = [0.0, k3 * RATIO_TO_3RD[1], k3 * RATIO_TO_3RD[2], k3, k3 * RATIO_TO_3RD[4]]
    notes = []
    if abs(factor - 1) > 0.2:
        notes.append(f"The speed setting changed by {abs(factor - 1) * 100:.0f} %: check the tyre size "
                     "is right for the car.")
    return ({"VSS_PULSES_PER_MILE": round(new_ppm, 1), "RPM_PER_MPH": [round(v, 1) for v in rpm]},
            {"speed_factor": factor, "k3": k[3], "k4": k[4], "ratio_4_3": ratio}, notes)


def shift_summary(log):
    """The factory computer's upshifts, grouped: a reference for the shift maps."""
    out = {}
    for s in log.shifts:
        m = re.match(r"ECU shift (\d)>(\d) at (\d+) mph, TPS (\d+) %", s)
        if m and int(m.group(2)) == int(m.group(1)) + 1:
            out.setdefault(f"{m.group(1)}>{m.group(2)}", []).append((int(m.group(4)), int(m.group(3))))
    return out


# ---------------------------------------------------------------- config.h
def read_config(text):
    """Every plain float and bool setting. Simple sums like 1.0f / 0.185f are worked out."""
    vals = {}
    for m in re.finditer(r"constexpr\s+(float|bool)\s+(\w+)\s*=\s*([^;{]+);", text):
        v = m.group(3).strip()
        if m.group(1) == "bool":
            vals[m.group(2)] = v == "true"
            continue
        expr = re.sub(r"(\d)f\b", r"\1", v)
        if re.fullmatch(r"[\d.\s/*+-]+", expr):
            vals[m.group(2)] = float(eval(expr))  # digits and + - * / only (checked just above)
    return vals


def set_value(text, name, value):
    if isinstance(value, list):
        body = "{" + ", ".join(f"{v:.1f}f" for v in value) + "}"
        pat = rf"(constexpr\s+float\s+{name}\[\d+\]\s*=\s*)\{{[^}}]*\}}"
        new, n = re.subn(pat, lambda m: m.group(1) + body, text)
    elif isinstance(value, bool):
        pat = rf"(constexpr\s+bool\s+{name}\s*=\s*)(true|false)"
        new, n = re.subn(pat, lambda m: m.group(1) + ("true" if value else "false"), text)
    else:
        pat = rf"(constexpr\s+float\s+{name}\s*=\s*)[^;]+;"
        new, n = re.subn(pat, lambda m: m.group(1) + f"{value}f;", text)
    if n != 1:
        raise CalError(f"Couldn't find {name} in config.h")
    return new


def show_and_write(values, config_path=None, ask=None, out=print):
    ask = ask or input
    config_path = config_path or CONFIG
    text = open(config_path).read()
    old = read_config(text)
    out("\n  Setting                 old            new")
    for k, v in values.items():
        if isinstance(v, list):
            o = re.search(rf"{k}\[\d+\]\s*=\s*\{{([^}}]*)\}}", text).group(1).replace("f", "")
            out(f"  {k:22s}  {{{o.strip()}}}")
            out(f"  {'':22s}  -> {{{', '.join(f'{x:.1f}' for x in v)}}}")
        else:
            out(f"  {k:22s}  {old.get(k, '?'):<13}  {v}")
    if ask("\nWrite these into firmware/tcu/config.h? (y/n) ").strip().lower().startswith("y"):
        for k, v in values.items():
            text = set_value(text, k, v)
        open(config_path, "w").write(text)
        out("Written. Now upload the firmware again (Arduino IDE: Upload).")
        return True
    out("Nothing written.")
    return False


def load_progress(path=None):
    path = path or PROGRESS
    try:
        return json.load(open(path))
    except (OSError, ValueError):
        return {}


def save_progress(step, values, path=None):
    path = path or PROGRESS
    p = load_progress(path)
    p[step] = {"when": datetime.datetime.now().isoformat(timespec="minutes"), "values": values}
    os.makedirs(os.path.dirname(path), exist_ok=True)
    json.dump(p, open(path, "w"), indent=2)


# ---------------------------------------------------------------- live USB
class Recorder:
    """Reads the TCU's USB log and saves every line to a file."""

    def __init__(self, port=None, path=None):
        try:
            import serial  # noqa: F401
            import serial.tools.list_ports
        except ImportError:
            sys.exit("This step needs pyserial. Install it with:  pip install pyserial")
        if not port:
            ports = [p for p in serial.tools.list_ports.comports()
                     if any(w in (p.description + " " + str(p.manufacturer)).lower()
                            for w in ("arduino", "mega", "ch340", "usb serial", "usb-serial", "acm"))]
            if not ports:
                sys.exit("Can't find the TCU. Is the USB cable plugged in? (Or give it: --port COM3)")
            if len(ports) > 1:
                for i, p in enumerate(ports):
                    print(f"  {i + 1}: {p.device}  {p.description}")
                ports = [ports[int(input("Which one is the TCU? ")) - 1]]
            port = ports[0].device
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        os.makedirs(CAL_DIR, exist_ok=True)
        self.path = path
        self.file = open(path, "w")
        self.lines = []
        print(f"Connected to {port}. (The TCU's screen restarts when you connect: that's normal.)")
        print(f"Saving everything to {os.path.relpath(path, ROOT)}")

    def pump(self, seconds):
        """Record for this long."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            raw = self.ser.readline()
            if raw:
                line = raw.decode("ascii", "replace").rstrip()
                self.lines.append(line)
                self.file.write(line + "\n")
        self.file.flush()

    def mark(self, text):
        self.lines.append("#! " + text)
        self.file.write("#! " + text + "\n")

    def close(self):
        self.file.close()
        self.ser.close()


def stamp(kind):
    return os.path.join(CAL_DIR, f"{kind}-{datetime.datetime.now():%Y%m%d-%H%M}.log")


def ask_float(prompt, default=None, ask=None):
    ask = ask or input
    while True:
        s = ask(prompt).strip()
        if not s and default is not None:
            return default
        try:
            return float(s)
        except ValueError:
            print("Please type a number, like 12.6")


def report(title, values, info, notes, step, write=True, ask=None):
    print(f"\n== {title}: the numbers ==")
    for k, v in info.items():
        print(f"  {k}: {v if not isinstance(v, float) else round(v, 3)}")
    for n in notes:
        print("  NOTE: " + n)
    # A step only counts as done once its numbers are in config.h.
    if write and show_and_write(values, ask=ask):
        save_progress(step, values)


# ---------------------------------------------------------------- the steps
def cmd_bench(args):
    print(__doc__.split("\n")[0])
    print("""
BENCH (build book part 10): sets the solenoid current sensors.
  1. Everything connected on the table as in part 10, bench 12 V on, ON/OFF on, only ECO on.
  2. Put your meter (volts) across J1a and J1c and note the reading.""")
    supply = ask_float("  What does the meter read? (e.g. 12.3) ")
    ohms = ask_float(f"  Meter on ohms across one big resistor: what does it read? [Enter = {DUMMY_OHMS:g}] ",
                     DUMMY_OHMS)
    log = Log(open(args.file)) if args.file else None
    if not log:
        rec = Recorder(args.port, stamp("bench"))
        print("""
  3. Now hold TEST for 2 seconds and do the self-test, following the screen.
     Waiting for the self-test to finish (up to 3 minutes)...""")
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            rec.pump(1)
            if any(line.startswith("# IBIT done") or line.startswith("# IBIT aborted") for line in rec.lines):
                rec.pump(2)
                break
        rec.close()
        log = Log(rec.lines)
    try:
        values, info, notes = bench_result(log, supply, ohms)
    except CalError as e:
        sys.exit("\nPROBLEM: " + str(e))
    report("Bench", values, info, notes, "bench")


def cmd_parked(args):
    print("""
PARKED: sets the gas pedal sensor and the battery reading.
  In the car, lever in P, engine OFF, key ON (the screen is on), ON/OFF off.""")
    cfg = read_config(open(CONFIG).read())
    meter = None
    if args.file:
        log = Log(open(args.file))
        meter = ask_float("What did your meter read on the battery? ")
    else:
        rec = Recorder(args.port, stamp("parked"))
        rec.pump(3)
        input("\n  1. Foot OFF the gas pedal. Press Enter, then keep still for 3 seconds... ")
        rec.mark("pedal_up begin")
        rec.pump(3)
        rec.mark("pedal_up end")
        input("  2. Now press the gas pedal ALL the way down and hold it. Press Enter while holding... ")
        rec.pump(0.5)
        rec.mark("pedal_down begin")
        rec.pump(3)
        rec.mark("pedal_down end")
        print("     OK, let go.")
        input("  3. Foot off. Put the meter (volts) on the battery's two terminals. Press Enter... ")
        rec.mark("battery begin")
        rec.pump(3)
        rec.mark("battery end")
        meter = ask_float("     What does the meter read? (e.g. 12.4) ")
        rec.close()
        log = Log(rec.lines)
    try:
        values, info, notes = parked_result(log, meter, cfg)
    except CalError as e:
        sys.exit("\nPROBLEM: " + str(e))
    report("Parked", values, info, notes, "parked")


def cmd_drive(args):
    cfg = read_config(open(CONFIG).read())
    if args.file:
        log = Log(open(args.file))
    else:
        print("""
DRIVE: sets the speed and the engine-rpm-per-mph numbers. ON/OFF stays OFF.
  Put your phone on the dash with a GPS speed app. Somebody else should watch the laptop.
  On a quiet road, at any time during the drive:
    a) hold a steady 30 mph on the GPS for 15 seconds
    b) press the O/D OFF button on the gear lever, hold a steady 45 mph for 20 seconds,
       then press O/D OFF again to turn it back off
    c) hold a steady 60 mph on the GPS for 20 seconds (on a highway)
  Then drive normally for a while. When you're parked again, press Ctrl+C here.""")
        rec = Recorder(args.port, stamp("drive"))
        try:
            while True:
                rec.pump(5)
                n = sum(1 for line in rec.lines if line[:1].isdigit())
                print(f"\r  recording... {n // 600} min", end="", flush=True)
        except KeyboardInterrupt:
            print()
        rec.close()
        log = Log(rec.lines)
    speeds = input("\nWhich GPS speeds did you hold? [Enter = 30 60] ").split() or ["30", "60"]
    try:
        values, info, notes = drive_result(log, [float(s) for s in speeds], cfg)
    except CalError as e:
        sys.exit("\nPROBLEM: " + str(e))
    shifts = shift_summary(log)
    if shifts:
        print("\nFor reference, the factory computer's upshifts (gas pedal %, mph):")
        for k, v in sorted(shifts.items()):
            print(f"  {k}: " + ", ".join(f"{t}% {m} mph" for t, m in sorted(v)))
    report("Drive", values, info, notes, "drive")


def cmd_status(args):
    p = load_progress()
    for step in ("bench", "parked", "drive"):
        print(f"  {step:7s} {'done ' + p[step]['when'] if step in p else 'not done yet'}")
    print(f"  CALIBRATED = {read_config(open(CONFIG).read()).get('CALIBRATED')}")


def cmd_unlock(args):
    p = load_progress()
    missing = [s for s in ("bench", "parked", "drive") if s not in p]
    if missing:
        sys.exit("Not yet: do these steps first: " + ", ".join(missing))
    cfg = read_config(open(CONFIG).read())
    for step in ("bench", "parked", "drive"):
        for k, v in p[step]["values"].items():
            have = cfg.get(k)
            if not isinstance(v, list) and have is not None and abs(have - v) > 1e-6:
                sys.exit(f"config.h's {k} ({have}) isn't the calibrated value ({v}): run the '{step}' "
                         "step again and answer y to write it.")
    print("""
Unlocking lets the TCU take over when you flip ON/OFF on. Before you do:
  - the bench self-test passed, and the car drove exactly as stock with ON/OFF off
  - you've read the build book's part 13 test steps (jack stands first)""")
    if input("Type UNLOCK to unlock: ").strip() != "UNLOCK":
        sys.exit("Not unlocked.")
    text = set_value(open(CONFIG).read(), "CALIBRATED", True)
    open(CONFIG, "w").write(text)
    print("Unlocked. Upload the firmware again. The self-test's CALIBRATED line will now say OK.")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Calibrate the TCU for your car, one step at a time.")
    ap.add_argument("step", choices=["bench", "parked", "drive", "unlock", "status"])
    ap.add_argument("--port", help="the TCU's USB port, if it isn't found by itself (e.g. COM3, /dev/ttyACM0)")
    ap.add_argument("--file", help="work from a saved log instead of the live USB connection")
    args = ap.parse_args(argv)
    {"bench": cmd_bench, "parked": cmd_parked, "drive": cmd_drive, "unlock": cmd_unlock,
     "status": cmd_status}[args.step](args)


if __name__ == "__main__":
    main()
