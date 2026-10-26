CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -O1 -g

.PHONY: test avr clean

test: build/test_tcu_logic
	./build/test_tcu_logic

build/test_tcu_logic: test/test_tcu_logic.cpp firmware/tcu/tcu_logic.cpp firmware/tcu/tcu_logic.h firmware/tcu/config.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ test/test_tcu_logic.cpp firmware/tcu/tcu_logic.cpp

avr:
	tools/avr_build.sh

clean:
	rm -rf build
