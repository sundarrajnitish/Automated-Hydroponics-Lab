// EEPROM.h (simulation mock) - 4 KiB, 3.3 ms per programmed byte.
#pragma once
#include "Arduino.h"
class EEPROMClass {
 public:
  uint8_t read(int addr);
  void write(int addr, uint8_t v);
  void update(int addr, uint8_t v) { if (read(addr) != v) write(addr, v); }
  uint16_t length() { return 4096; }
  template <typename T> T& get(int addr, T& t) {
    uint8_t* p = (uint8_t*)&t;
    for (unsigned i = 0; i < sizeof(T); ++i) p[i] = read(addr + (int)i);
    return t;
  }
  template <typename T> const T& put(int addr, const T& t) {
    const uint8_t* p = (const uint8_t*)&t;
    for (unsigned i = 0; i < sizeof(T); ++i) update(addr + (int)i, p[i]);
    return t;
  }
};
extern EEPROMClass EEPROM;
