// avr/wdt.h (simulation mock) - the watchdog resets the virtual CPU.
#pragma once
#include <stdint.h>
#define WDTO_1S 6
#define WDTO_2S 7
#define WDTO_4S 8
#define WDTO_8S 9
void wdt_enable(uint8_t timeout);
void wdt_disable();
void wdt_reset();
