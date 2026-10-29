# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -O1 -g
LOGIC := firmware/tcu/tcu_logic.cpp firmware/tcu/self_test.cpp
HEADERS := $(wildcard firmware/tcu/*.h)

.PHONY: test system screens calibrate-test check avr chip mutate pcb clean

test: build/test_tcu_logic build/test_self_test
	./build/test_tcu_logic
	./build/test_self_test

build/%: test/%.cpp $(LOGIC) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $< $(LOGIC)

# System test: the real tcu.ino against a simulated car and TCU hardware,
# with one failure injected per scenario (test/system/system_test.cpp).
SIM_FW := build/sim_fw
system: build/system_test
	./build/system_test

build/system_test: test/system/system_test.cpp test/system/mock/*.h test/system/mock/avr/*.h firmware/tcu/*
	@mkdir -p $(SIM_FW)
	cp firmware/tcu/* $(SIM_FW)/
	sed -i -e 's/^constexpr bool IBIT_WATCHDOG_TEST = true;/constexpr bool IBIT_WATCHDOG_TEST = false;/' \
	       -e 's/^constexpr bool CALIBRATED = false;/constexpr bool CALIBRATED = true;/' \
	       -e 's/^constexpr AtfSensor ATF_SENSOR = ATF_DS18B20;/constexpr AtfSensor ATF_SENSOR = ATF_NTC;/' $(SIM_FW)/config.h
	$(CXX) -std=gnu++17 -O1 -w -Itest/system/mock -I$(SIM_FW) -o $@ test/system/system_test.cpp \
	    $(SIM_FW)/tcu_logic.cpp $(SIM_FW)/self_test.cpp $(SIM_FW)/oled.cpp $(SIM_FW)/ds18b20.cpp $(SIM_FW)/imu.cpp

# Chip test: the .hex as flashed, started by the real Mega bootloader, on a
# simulated ATmega2560 (simavr: apt install simavr libsimavr-dev), wired to a
# simulated board and car (test/chip/chip_test.cpp). Two builds: as shipped,
# and with CALIBRATED = true as tools/calibrate.py writes it.
CHIP := build/chip
BOOTLOADER ?= /usr/share/arduino/hardware/arduino/avr/bootloaders/stk500v2/stk500boot_v2_mega2560.hex
chip: $(CHIP)/chip_test $(CHIP)/shipped/tcu.hex $(CHIP)/cal/tcu.hex $(CHIP)/bootloader.hex
	$(CHIP)/chip_test $(CHIP)/shipped/tcu.hex $(CHIP)/cal/tcu.hex $(CHIP)/bootloader.hex

# The Mega's bootloader, minus its start-address record (simavr's reader
# doesn't know that record type; it carries no code).
$(CHIP)/bootloader.hex: $(BOOTLOADER)
	@mkdir -p $(CHIP)
	grep -v '^:04000003' $< > $@

$(CHIP)/shipped/tcu.hex: firmware/tcu/* tools/avr_build.sh
	OUT=$(CHIP)/shipped tools/avr_build.sh > /dev/null

$(CHIP)/cal/tcu.hex: firmware/tcu/* tools/avr_build.sh
	@mkdir -p $(CHIP)/cal_src
	cp firmware/tcu/* $(CHIP)/cal_src/
	sed -i 's/^constexpr bool CALIBRATED = false;/constexpr bool CALIBRATED = true;/' $(CHIP)/cal_src/config.h
	grep -q '^constexpr bool CALIBRATED = true;' $(CHIP)/cal_src/config.h
	SK=$(abspath $(CHIP)/cal_src) OUT=$(CHIP)/cal tools/avr_build.sh > /dev/null

$(CHIP)/chip_test: test/chip/chip_test.cpp firmware/tcu/font5x7.h firmware/tcu/tcu.ino tools/chip_pins.py
	@mkdir -p $(CHIP)
	python3 tools/chip_pins.py > $(CHIP)/pins.h
	$(CXX) -std=gnu++17 -O2 -Wall -I$(CHIP) -Ifirmware/tcu -Itest/system/mock -o $@ test/chip/chip_test.cpp $$(pkg-config --libs simavr)

# The screen pictures in docs/oled-screens.png, from the system test's car.
screens: build/system_test
	@mkdir -p build/screens
	./build/system_test --screens build/screens
	python3 tools/render_screens.py build/screens docs/oled-screens.png

# The calibration helper (tools/calibrate.py), on the simulated car's USB log.
calibrate-test: build/system_test
	./build/system_test --callog build/cal.log
	python3 test/test_calibrate.py build/cal.log

# Everything: unit tests, system test, the chip test, calibration helper, and
# the cross-checks between firmware, circuit, circuit board, housings and docs.
check: test system chip calibrate-test
	python3 tools/system_check.py

# How good are the tests? Plants one bug at a time in the firmware and counts
# how many the tests catch (build/mutants/report.md). About 40 minutes.
mutate:
	python3 tools/mutate.py

avr:
	tools/avr_build.sh

# The circuit board: place, route, DRC, Gerbers, build-book steps (needs KiCad 7+).
# Then tools/build_stl.sh, so the box's fit check sees the new board.
pcb:
	python3 tools/pcb.py

clean:
	rm -rf build
