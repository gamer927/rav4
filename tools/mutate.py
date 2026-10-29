#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""Mutation testing: do the tests catch bugs, or just pass?

Plants one small bug at a time in the firmware (a < becomes <=, an && becomes
||, true becomes false, a statement goes missing...), rebuilds, and runs the
unit tests and the system test against it. A bug the tests catch is "killed";
one they don't is a "survivor". Every survivor is either a gap in the tests
or a change that makes no difference (an "equivalent" mutant: listed in
EQUIVALENT below, each with the reason).

  python3 tools/mutate.py               # everything (about 10 minutes on 4 cores)
  python3 tools/mutate.py --file tcu_logic.cpp --jobs 4
  python3 tools/mutate.py --list        # just print the mutants

Writes build/mutants/report.md.
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware", "tcu")
OUT = os.path.join(ROOT, "build", "mutants")
FILES = ["tcu_logic.cpp", "self_test.cpp", "tcu.ino"]
CXX = os.environ.get("CXX", "g++")
SAN = "-g -fsanitize=address,undefined -fno-sanitize-recover=all"  # as the Makefile: out-of-bounds reads fail
CHIP = False  # --chip: tcu.ino mutants also face the chip test (slow: about 1 min each)

# (pattern, replacement, name): applied to one occurrence at a time, outside
# comments and string literals.
OPERATORS = [
    (r"(?<![<>=!])<=", "<", "<= to <"),
    (r"(?<![<>=!])>=", ">", ">= to >"),
    (r"(?<![<>=\-])<(?![<=])", "<=", "< to <="),
    (r"(?<![<>=\-])>(?![>=])", ">=", "> to >="),
    (r"==", "!=", "== to !="),
    (r"!=", "==", "!= to =="),
    (r"&&", "||", "&& to ||"),
    (r"\|\|", "&&", "|| to &&"),
    (r"\btrue\b", "false", "true to false"),
    (r"\bfalse\b", "true", "false to true"),
    (r"(?<=[\w)\]]) \+ (?=[\w(])", " - ", "+ to -"),
    (r"(?<=[\w)\]]) - (?=[\w(])", " + ", "- to +"),
    (r"(?<![\w)\]!=<>])!(?!=)", "", "drop !"),
]

# Surviving mutants that make no difference anything could see, each with
# the reason: (file, operator, text in the line, text in the line before it
# or None, why). Reviewed by hand; a survivor not listed here is a gap.
EQUIVALENT = []


def code_spans(line):
    """(start, end) spans of `line` that are code: not in a string, char or // comment."""
    spans, i, start, n = [], 0, 0, len(line)
    while i < n:
        c = line[i]
        if line.startswith("//", i):
            break
        if c in "\"'":
            spans.append((start, i))
            q = c
            i += 1
            while i < n and line[i] != q:
                i += 2 if line[i] == "\\" else 1
            start = i + 1
        i += 1
    spans.append((start, min(i, n)))
    return [(a, b) for a, b in spans if b > a]


def skip_line(text):
    s = text.strip()
    return (not s or s.startswith(("#", "//", "/*", "*")) or "static_assert" in s or
            s.startswith(("template", "constexpr uint8_t kUnusedPins")))


def prev_code(lines, ln):
    """The nearest non-blank line before line `ln` (0-based), stripped."""
    for i in range(ln - 1, -1, -1):
        if lines[i].strip():
            return lines[i].strip()
    return ""


def equivalent(m):
    for f, op, text, before, why in EQUIVALENT:
        if m["file"] == f and m["op"] == op and text in m["old"] and (before is None or before in m["prev"]):
            return why
    return None


def mutants_of(fname):
    lines = open(os.path.join(FW, fname)).read().split("\n")
    out = []
    in_block = False
    for ln, text in enumerate(lines):
        if in_block:
            if "*/" in text:
                in_block = False
            continue
        if text.strip().startswith("/*") and "*/" not in text:
            in_block = True
            continue
        if skip_line(text):
            continue
        for a, b in code_spans(text):
            seg = text[a:b]
            for pat, rep, name in OPERATORS:
                for m in re.finditer(pat, seg):
                    # leave #include <...>, templates and casts alone
                    if name.startswith(("<", ">")) and re.search(r"(include|template|_cast)\s*$", seg[:m.start()]):
                        continue
                    new = text[:a + m.start()] + rep + text[a + m.end():]
                    out.append(dict(file=fname, line=ln + 1, op=name, old=text, new=new, prev=prev_code(lines, ln)))
        # statement deletion: a plain call or assignment on its own line
        s = text.strip()
        if (s.endswith(";") and not s.startswith(("return", "const ", "static ", "uint", "int", "float", "bool",
                                                  "tcu::", "auto ", "char ", "case ", "break", "default"))
                and re.match(r"^[\w.\[\]>()*&:-]+\s*(\(|=|\+=|-=|\|=|&=|\+\+|--)", s)):
            out.append(dict(file=fname, line=ln + 1, op="delete statement", old=text, prev=prev_code(lines, ln),
                            new=text[:len(text) - len(text.lstrip())] + ";"))
    return out


def sh(cmd, cwd, timeout=120):
    try:
        r = subprocess.run(cmd, cwd=cwd, shell=True, capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return "timeout", ""


SIM_SED = (r"sed -i -e 's/^constexpr bool IBIT_WATCHDOG_TEST = true;/constexpr bool IBIT_WATCHDOG_TEST = false;/' "
           r"-e 's/^constexpr bool CALIBRATED = false;/constexpr bool CALIBRATED = true;/' "
           r"-e 's/^constexpr AtfSensor ATF_SENSOR = ATF_DS18B20;/constexpr AtfSensor ATF_SENSOR = ATF_NTC;/' "
           "config.h")


def run_mutant(k, m):
    d = os.path.join(OUT, "work", f"m{k:04d}")
    shutil.rmtree(d, ignore_errors=True)
    fw, sim = os.path.join(d, "fw"), os.path.join(d, "sim")
    shutil.copytree(FW, fw)
    lines = open(os.path.join(fw, m["file"])).read().split("\n")
    assert lines[m["line"] - 1] == m["old"]
    lines[m["line"] - 1] = m["new"]
    open(os.path.join(fw, m["file"]), "w").write("\n".join(lines))
    shutil.copytree(fw, sim)
    sh(SIM_SED, sim)
    logic = f"{fw}/tcu_logic.cpp {fw}/self_test.cpp"
    steps = []
    if m["file"] != "tcu.ino":
        steps += [f"{CXX} -std=c++11 -O1 {SAN} -w -I{fw} -o t1 {ROOT}/test/test_tcu_logic.cpp {logic} && ./t1",
                  f"{CXX} -std=c++11 -O1 {SAN} -w -I{fw} -o t2 {ROOT}/test/test_self_test.cpp {logic} && ./t2"]
    steps.append(f"{CXX} -std=gnu++17 -O1 {SAN} -w -I{ROOT}/test -I{ROOT}/test/system/mock -I{sim} -o st "
                 f"{ROOT}/test/system/system_test.cpp {sim}/tcu_logic.cpp {sim}/self_test.cpp "
                 f"{sim}/oled.cpp {sim}/ds18b20.cpp {sim}/imu.cpp && ./st")
    # The calibration helper reads the firmware's USB log: it must still work.
    steps.append(f"./st --callog cal.log > /dev/null && python3 {ROOT}/test/test_calibrate.py cal.log")
    if CHIP and m["file"] == "tcu.ino":
        # The chip test: the mutant built for the Mega, on the simulated chip.
        core = f"PREBUILT_CORE={ROOT}/build/avr/core"
        steps.append(f"{core} OUT={d}/avr SK={fw} {ROOT}/tools/avr_build.sh > /dev/null && "
                     f"cp -r {fw} calsrc && sed -i 's/^constexpr bool CALIBRATED = false;/constexpr bool CALIBRATED = true;/' "
                     f"calsrc/config.h && {core} OUT={d}/cal SK={d}/calsrc {ROOT}/tools/avr_build.sh > /dev/null && "
                     f"{ROOT}/build/chip/chip_test {d}/avr/tcu.hex {d}/cal/tcu.hex {ROOT}/build/chip/bootloader.hex")
    result, why = "survived", ""
    for i, cmd in enumerate(steps):
        code, text = sh(cmd, d, timeout=600 if "chip_test" in cmd else 120)
        if code == "timeout":
            result, why = "killed", "timed out (hung)"
            break
        if code != 0:
            built = {"t1", "t2", "st"} & set(os.listdir(d))
            if "error:" in text and i < 3 and not built:
                result, why = "stillborn", "doesn't compile"
            else:
                fails = [ln.strip() for ln in text.splitlines() if "FAIL" in ln][:2]
                result, why = "killed", "; ".join(fails)[:200] or f"exit {code}"
            break
    shutil.rmtree(d, ignore_errors=True)
    return k, result, why


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", action="append", help="only this firmware file (repeatable)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--fresh", action="store_true", help="forget saved results (after changing the tests)")
    ap.add_argument("--report", action="store_true",
                    help="no testing: write the report from the saved results (the chip pass counts too)")
    ap.add_argument("--chip", action="store_true", help="tcu.ino mutants also run the chip test (make chip first)")
    ap.add_argument("--only", help="only the mutants listed (numbers, comma-separated), e.g. the last survivors")
    args = ap.parse_args()
    global CHIP
    CHIP = args.chip
    ms = [m for f in (args.file or FILES) for m in mutants_of(f)]
    if args.only:
        keep = {int(k) for k in args.only.split(",")}
        ms = [m for k, m in enumerate(ms) if k in keep]
    if args.list:
        for k, m in enumerate(ms):
            print(f"{k:4d} {m['file']}:{m['line']} {m['op']}: {m['new'].strip()}")
        return
    os.makedirs(OUT, exist_ok=True)
    if args.report:
        return report_from_saved(ms)
    # Each result is saved as it comes (build/mutants/results.jsonl), so a run
    # that is stopped carries on where it left off: --fresh starts over.
    saved = os.path.join(OUT, "results_chip.jsonl" if CHIP else "results.jsonl")
    if args.fresh and os.path.exists(saved):
        os.remove(saved)
    done = {}
    if os.path.exists(saved):
        for line in open(saved):
            r = json.loads(line)
            done[(r["file"], r["line"], r["op"], r["new"])] = (r["result"], r["why"])
    key = lambda m: (m["file"], m["line"], m["op"], m["new"])  # noqa: E731
    results = {k: done[key(m)] for k, m in enumerate(ms) if key(m) in done}
    todo = [(k, m) for k, m in enumerate(ms) if k not in results]
    print(f"{len(ms)} mutants, {len(results)} already done, {len(todo)} to run", flush=True)
    with cf.ThreadPoolExecutor(args.jobs) as ex, open(saved, "a") as out:
        futs = [ex.submit(run_mutant, k, m) for k, m in todo]
        for n, f in enumerate(cf.as_completed(futs), 1):
            k, res, why = f.result()
            results[k] = (res, why)
            m = ms[k]
            out.write(json.dumps(dict(file=m["file"], line=m["line"], op=m["op"], new=m["new"],
                                      result=res, why=why)) + "\n")
            out.flush()
            if n % 25 == 0 or n == len(todo):
                print(f"  {n}/{len(todo)} done", flush=True)
    write_report(ms, results)


def report_from_saved(ms):
    """The report from build/mutants/results.jsonl, with a survivor there
    that the chip pass (results_chip.jsonl) killed counted as killed."""
    key = lambda m: (m["file"], m["line"], m["op"], m["new"])  # noqa: E731
    saved = {}
    for name in ("results.jsonl", "results_chip.jsonl"):
        path = os.path.join(OUT, name)
        if not os.path.exists(path):
            continue
        for line in open(path):
            r = json.loads(line)
            k = (r["file"], r["line"], r["op"], r["new"])
            if name == "results.jsonl" or (r["result"] == "killed" and saved.get(k, ("",))[0] == "survived"):
                saved[k] = (r["result"], ("chip test: " if name != "results.jsonl" else "") + r["why"])
    results = {k: saved.get(key(m), ("not run", "")) for k, m in enumerate(ms)}
    write_report(ms, results)


def write_report(ms, results):
    rows, tally = [], {}
    for k, m in enumerate(ms):
        res, why = results[k]
        if res == "survived" and equivalent(m):
            res, why = "equivalent", equivalent(m)
        tally.setdefault(m["file"], {}).setdefault(res, 0)
        tally[m["file"]][res] += 1
        rows.append((m, res, why))
    md = ["# Mutation test report", "", "Generated by `tools/mutate.py`. Each row is one planted bug.", "",
          "| File | Killed | Survived | Equivalent | Doesn't compile | Score |", "|---|---|---|---|---|---|"]
    for f, t in tally.items():
        live = t.get("killed", 0) + t.get("survived", 0)
        md.append(f"| {f} | {t.get('killed', 0)} | {t.get('survived', 0)} | {t.get('equivalent', 0)} | "
                  f"{t.get('stillborn', 0)} | {100 * t.get('killed', 0) / max(live, 1):.0f} % |")
    md += ["", "## Survivors", "", "| Where | Bug planted | Line after |", "|---|---|---|"]
    for m, res, why in rows:
        if res == "survived":
            md.append(f"| {m['file']}:{m['line']} | {m['op']} | `{m['new'].strip().replace('|', '\\|')}` |")
    md += ["", "## Equivalent (no test can see them)", "", "| Where | Bug planted | Why it changes nothing |",
           "|---|---|---|"]
    for m, res, why in rows:
        if res == "equivalent":
            md.append(f"| {m['file']}:{m['line']} | {m['op']} | {why} |")
    open(os.path.join(OUT, "report.md"), "w").write("\n".join(md) + "\n")
    print("\n".join(md[4:4 + 2 + len(tally)]))
    print(f"report: {os.path.relpath(os.path.join(OUT, 'report.md'), ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
