CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -O1 -g
LOGIC := firmware/tcu/tcu_logic.cpp firmware/tcu/self_test.cpp
HEADERS := $(wildcard firmware/tcu/*.h)

.PHONY: test avr clean

test: build/test_tcu_logic build/test_self_test
	./build/test_tcu_logic
	./build/test_self_test

build/%: test/%.cpp $(LOGIC) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $< $(LOGIC)

avr:
	tools/avr_build.sh

clean:
	rm -rf build
