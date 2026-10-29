#!/usr/bin/env bash
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
# Compile-check the firmware for an Arduino Mega 2560 without the Arduino IDE.
# Needs avr-gcc + the Arduino AVR core (Debian/Ubuntu: apt install gcc-avr
# avr-libc arduino-core-avr). For flashing, use the Arduino IDE/CLI or
# PlatformIO (platformio.ini at the repo root).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="${ARDUINO_AVR:-/usr/share/arduino/hardware/arduino/avr}"
OUT="${OUT:-$ROOT/build/avr}"
mkdir -p "$OUT/core"

FLAGS=(-mmcu=atmega2560 -DF_CPU=16000000L -DARDUINO=10819 -DARDUINO_AVR_MEGA2560 -DARDUINO_ARCH_AVR
       -Os -Wall -Wextra -ffunction-sections -fdata-sections -flto
       -I"$CORE/cores/arduino" -I"$CORE/variants/mega")
CXXFLAGS=(-std=gnu++11 -fno-exceptions -fno-threadsafe-statics -fpermissive)

objs=()
WIRE="$CORE/libraries/Wire/src"
if [ -n "${PREBUILT_CORE:-}" ] && [ -d "$PREBUILT_CORE" ]; then
  # The Arduino core and Wire, already compiled (tools/mutate.py builds many
  # sketches in a row; the core never changes).
  mapfile -t objs < "$PREBUILT_CORE/link-order"  # the same order as a full build: the same .hex
else
for src in "$CORE"/cores/arduino/*.c; do
  o="$OUT/core/$(basename "$src").o"; avr-gcc "${FLAGS[@]}" -w -c "$src" -o "$o"; objs+=("$o")
done
for src in "$CORE"/cores/arduino/*.cpp; do
  o="$OUT/core/$(basename "$src").o"; avr-g++ "${FLAGS[@]}" "${CXXFLAGS[@]}" -w -c "$src" -o "$o"; objs+=("$o")
done
for src in "$CORE"/cores/arduino/*.S; do
  [ -e "$src" ] || continue
  o="$OUT/core/$(basename "$src").o"; avr-gcc "${FLAGS[@]}" -x assembler-with-cpp -c "$src" -o "$o"; objs+=("$o")
done

# Wire library (console OLED).
avr-g++ "${FLAGS[@]}" "${CXXFLAGS[@]}" -I"$WIRE" -w -c "$WIRE/Wire.cpp" -o "$OUT/core/Wire.o"
avr-gcc "${FLAGS[@]}" -I"$WIRE" -w -c "$WIRE/utility/twi.c" -o "$OUT/core/twi.o"
objs+=("$OUT/core/Wire.o" "$OUT/core/twi.o")
printf '%s\n' "${objs[@]}" > "$OUT/core/link-order"
fi

SK="${SK:-$ROOT/firmware/tcu}"  # a copy with other settings, for the chip test

sk_objs=()
for src in "$SK/tcu.ino" "$SK"/*.cpp; do
  o="$OUT/$(basename "$src").o"
  avr-g++ "${FLAGS[@]}" "${CXXFLAGS[@]}" -I"$WIRE" -x c++ -c "$src" -o "$o"
  sk_objs+=("$o")
done
# The core goes in as an archive, as the Arduino IDE does it: only what the
# sketch uses gets linked (not Serial1-3), so the sizes below match the IDE's.
rm -f "$OUT/core.a"
avr-gcc-ar rcs "$OUT/core.a" "${objs[@]}"
avr-gcc "${FLAGS[@]}" -Wl,--gc-sections -o "$OUT/tcu.elf" "${sk_objs[@]}" "$OUT/core.a" -lm
avr-objcopy -O ihex -R .eeprom "$OUT/tcu.elf" "$OUT/tcu.hex"
avr-size -C --mcu=atmega2560 "$OUT/tcu.elf" 2>/dev/null || avr-size "$OUT/tcu.elf"

# The self-test's results and the watchdog mark live in .noinit RAM, which
# must survive the bootloader: it runs on a stack at the top of RAM (about
# 300 bytes) before it starts the sketch. Keep 1 KB clear below RAMEND.
NOINIT_END=$(avr-nm "$OUT/tcu.elf" | awk '$3 == "_end" {print $1}')
if [ -z "$NOINIT_END" ] || [ $((16#$NOINIT_END - 0x800000)) -gt $((0x21FF - 1024)) ]; then
  echo "FAIL: .noinit ends at 0x${NOINIT_END:-?}: too close to the bootloader's stack" >&2
  exit 1
fi
echo "noinit ends at 0x$(printf %04X $((16#$NOINIT_END - 0x800000))) (bootloader stack starts near 0x21FF)"
echo "OK: $OUT/tcu.hex"
