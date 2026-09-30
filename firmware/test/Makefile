# Host tests for the controller core. The core is compiled as C++11 with
# every warning on, like avr-gcc in the Arduino IDE would see it.
CXX ?= g++
CXXFLAGS = -std=c++11 -O1 -g -Wall -Wextra -Wpedantic -Werror -Wconversion -Wno-sign-conversion \
           -ffp-contract=off -fsanitize=address,undefined
test: build/test_core
	./build/test_core
build/test_core: test_core.cpp ../Hydroponics/src/hydro_core.cpp ../Hydroponics/src/hydro_core.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp ../Hydroponics/src/hydro_core.cpp
clean:
	rm -rf build
.PHONY: test clean
