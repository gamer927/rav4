# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -O1 -g
LOGIC := firmware/tcu/tcu_logic.cpp firmware/tcu/self_test.cpp
HEADERS := $(wildcard firmware/tcu/*.h)

.PHONY: test system screens calibrate-test check avr pcb clean

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

# The screen pictures in docs/oled-screens.png, from the system test's car.
screens: build/system_test
	@mkdir -p build/screens
	./build/system_test --screens build/screens
	python3 tools/render_screens.py build/screens docs/oled-screens.png

# The calibration helper (tools/calibrate.py), on the simulated car's USB log.
calibrate-test: build/system_test
	./build/system_test --callog build/cal.log
	python3 test/test_calibrate.py build/cal.log

# Everything: unit tests, system test, calibration helper, and the
# cross-checks between firmware, circuit, circuit board, housings and docs.
check: test system calibrate-test
	python3 tools/system_check.py

avr:
	tools/avr_build.sh

# The circuit board: place, route, DRC, Gerbers, build-book steps (needs KiCad 7+).
# Then tools/build_stl.sh, so the box's fit check sees the new board.
pcb:
	python3 tools/pcb.py

clean:
	rm -rf build
