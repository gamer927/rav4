#!/usr/bin/env bash
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
avr-g++ "${FLAGS[@]}" "${CXXFLAGS[@]}" -x c++ -c "$SK/tcu.ino" -o "$OUT/tcu.o"
avr-g++ "${FLAGS[@]}" "${CXXFLAGS[@]}" -c "$SK/tcu_logic.cpp" -o "$OUT/tcu_logic.o"
avr-gcc "${FLAGS[@]}" -Wl,--gc-sections -o "$OUT/tcu.elf" "$OUT/tcu.o" "$OUT/tcu_logic.o" "${objs[@]}" -lm
avr-objcopy -O ihex -R .eeprom "$OUT/tcu.elf" "$OUT/tcu.hex"
avr-size -C --mcu=atmega2560 "$OUT/tcu.elf" 2>/dev/null || avr-size "$OUT/tcu.elf"
echo "OK: $OUT/tcu.hex"
