CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -O1 -g
LOGIC := firmware/tcu/tcu_logic.cpp firmware/tcu/self_test.cpp
HEADERS := $(wildcard firmware/tcu/*.h)

.PHONY: test system screens check avr clean

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

# Everything: unit tests, system test, and the cross-checks between firmware,
# boards, housings and docs.
check: test system
	python3 tools/system_check.py

avr:
	tools/avr_build.sh

clean:
	rm -rf build
