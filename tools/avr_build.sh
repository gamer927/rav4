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

SK="$ROOT/firmware/tcu"
WIRE="$CORE/libraries/Wire/src"
# Wire library (console OLED).
avr-g++ "${FLAGS[@]}" "${CXXFLAGS[@]}" -I"$WIRE" -w -c "$WIRE/Wire.cpp" -o "$OUT/core/Wire.o"
avr-gcc "${FLAGS[@]}" -I"$WIRE" -w -c "$WIRE/utility/twi.c" -o "$OUT/core/twi.o"
objs+=("$OUT/core/Wire.o" "$OUT/core/twi.o")

sk_objs=()
for src in "$SK/tcu.ino" "$SK"/*.cpp; do
  o="$OUT/$(basename "$src").o"
  avr-g++ "${FLAGS[@]}" "${CXXFLAGS[@]}" -I"$WIRE" -x c++ -c "$src" -o "$o"
  sk_objs+=("$o")
done
avr-gcc "${FLAGS[@]}" -Wl,--gc-sections -o "$OUT/tcu.elf" "${sk_objs[@]}" "${objs[@]}" -lm
avr-objcopy -O ihex -R .eeprom "$OUT/tcu.elf" "$OUT/tcu.hex"
avr-size -C --mcu=atmega2560 "$OUT/tcu.elf" 2>/dev/null || avr-size "$OUT/tcu.elf"
echo "OK: $OUT/tcu.hex"
