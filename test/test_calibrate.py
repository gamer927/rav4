#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""Tests for tools/calibrate.py, using the USB log of the simulated car
(build/system_test --callog, run by `make calibrate-test`). That car's speed
sensor gives 6 % more pulses per mile than the firmware's setting and its
engine turns 5 % more rpm per mph, so the right answers are known."""
import builtins
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import calibrate as cal  # noqa: E402

LOG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "cal.log")
checks = failures = 0


def check(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("  FAIL: " + what)


def near(a, b, rel=0.003):
    return abs(a / b - 1) <= rel


def raises(fn, words):
    try:
        fn()
    except cal.CalError as e:
        return words.lower() in str(e).lower()
    return False


def answers(*items):
    it = iter(items)
    return lambda prompt="": next(it)


lines = open(LOG).read().splitlines()
log = cal.Log(lines)
cfg = cal.read_config(open(cal.CONFIG).read())
TRUE_PPM = cfg["VSS_PULSES_PER_MILE"] * 1.06
TRUE_K = [k * 1.05 for k in (0.0, 151.0, 83.2, 53.8, 39.4)]   # config's RPM_PER_MPH x the sim's 5 %

print("the log")
check(log.cal.get("VSS_PULSES_PER_MILE") == cfg["VSS_PULSES_PER_MILE"], "# cal line read")
check(len(log.rows) > 1000, "data rows read")
check("tps_v" in log.rows[0], "tps_v column")
check(len(log.between("pedal_up")) >= 20, "markers")

print("bench: current sensors")
values, info, notes = cal.bench_result(log, 12.6)
check(near(values["IS_AMPS_PER_VOLT"], cfg["IS_AMPS_PER_VOLT"] * (12.6 / 15) / 0.94, 0.02), "IS_AMPS_PER_VOLT")
check(raises(lambda: cal.bench_result(cal.Log([]), 12.6), "# cal"), "no log: says so")
no_ibit = cal.Log([line for line in lines if "SOLENOID" not in line and "LOCKUP SOL" not in line])
check(raises(lambda: cal.bench_result(no_ibit, 12.6), "self-test"), "no self-test: says so")

print("parked: gas pedal and battery")
values, info, notes = cal.parked_result(log, 12.85, cfg)
check(near(values["TPS_CLOSED_V"], cfg["TPS_CLOSED_V"], 0.02), "pedal up")
check(near(values["TPS_WOT_V"], cfg["TPS_WOT_V"], 0.02), "pedal down")
check(near(values["VBAT_DIVIDER"], cfg["VBAT_DIVIDER"] * 12.85 / 12.6, 0.005), "battery divider")
stuck = cal.Log([re.sub(r",[\d.]+$", ",0.60", line) if line[:1].isdigit() else line for line in lines])
check(raises(lambda: cal.parked_result(stuck, 12.6, cfg), "pedal only changed"), "pedal that doesn't move")
check(raises(lambda: cal.parked_result(log, 14.5, cfg), "10 %"), "meter far off")

print("drive: speed and rpm per mph")
values, info, notes = cal.drive_result(log, [30, 60], cfg)
check(near(values["VSS_PULSES_PER_MILE"], TRUE_PPM), f"pulses per mile {values['VSS_PULSES_PER_MILE']} vs {TRUE_PPM:.1f}")
for g in (1, 2, 3, 4):
    check(near(values["RPM_PER_MPH"][g], TRUE_K[g], 0.005), f"RPM_PER_MPH[{g}] {values['RPM_PER_MPH'][g]} vs {TRUE_K[g]:.1f}")
check(raises(lambda: cal.drive_result(log, [32, 60], cfg), "disagree"), "a wrong GPS speed is caught")
check(raises(lambda: cal.drive_result(log, [75], cfg), "75 mph"), "a speed never held is caught")
no3 = cal.Log([line for line in lines if not re.match(r"\d+,0,\d,3,1,", line)])
check(raises(lambda: cal.drive_result(no3, [30, 60], cfg), "O/D OFF"), "no 3rd-gear run: asks for it")
no4 = cal.Log([line for line in lines if not re.match(r"\d+,0,\d,4,1,", line)])
check(raises(lambda: cal.drive_result(no4, [30], cfg), "60 mph"), "no 4th-gear run: asks for it")

print("the whole flow, as a user, on a copy of config.h")
tmp = tempfile.mkdtemp()
try:
    cal.CONFIG = os.path.join(tmp, "config.h")
    cal.PROGRESS = os.path.join(tmp, "progress.json")
    shutil.copy(os.path.join(ROOT, "firmware", "tcu", "config.h"), cal.CONFIG)
    real_input = builtins.input
    try:
        builtins.input = answers("12.6", "", "n")             # answer no: nothing written, not done
        cal.main(["bench", "--file", LOG])
        check(cal.load_progress() == {}, "a step answered 'n' isn't counted as done")
        builtins.input = answers("12.6", "", "y")             # supply volts, ohms (default), write
        cal.main(["bench", "--file", LOG])
        builtins.input = answers("12.85", "y")                # battery meter, write
        cal.main(["parked", "--file", LOG])
        builtins.input = answers("", "y")                     # speeds (default 30 60), write
        cal.main(["drive", "--file", LOG])
        builtins.input = answers("no")
        try:
            cal.main(["unlock"])
            check(False, "unlock without typing UNLOCK")
        except SystemExit:
            pass
        check(cal.read_config(open(cal.CONFIG).read())["CALIBRATED"] is False, "still locked")
        builtins.input = answers("UNLOCK")
        cal.main(["unlock"])
    finally:
        builtins.input = real_input
    text = open(cal.CONFIG).read()
    new = cal.read_config(text)
    check(new["CALIBRATED"] is True, "unlocked")
    check(near(new["VSS_PULSES_PER_MILE"], TRUE_PPM), "written: pulses per mile")
    check(near(new["IS_AMPS_PER_VOLT"], cfg["IS_AMPS_PER_VOLT"] * (12.6 / 15) / 0.94, 0.02),
          "written: IS_AMPS_PER_VOLT (replacing 1.0f / 0.185f)")
    k = re.search(r"RPM_PER_MPH\[5\]\s*=\s*\{([^}]*)\}", text).group(1)
    check(near(float(k.split(",")[3].strip().rstrip("f")), TRUE_K[3], 0.005), "written: RPM_PER_MPH")
    # The edited config.h must still compile.
    src = os.path.join(tmp, "t.cpp")
    open(src, "w").write('#include "config.h"\nint main() { return cfg::CALIBRATED ? 0 : 1; }\n')
    r = subprocess.run(["g++", "-std=c++11", "-fsyntax-only", "-I", tmp, src], capture_output=True, text=True)
    check(r.returncode == 0, "edited config.h compiles: " + r.stderr[:300])
    # Unlock refuses if a calibrated value was changed afterwards.
    open(cal.CONFIG, "w").write(cal.set_value(text, "TPS_WOT_V", 4.44))
    builtins.input = answers("UNLOCK")
    try:
        cal.main(["unlock"])
        check(False, "unlock with a changed value")
    except SystemExit as e:
        check("TPS_WOT_V" in str(e), "unlock names the changed value")
    finally:
        builtins.input = real_input
finally:
    shutil.rmtree(tmp)

print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
