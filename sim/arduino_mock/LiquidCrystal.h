// LiquidCrystal.h (simulation mock) - HD44780 16x2 in 4-bit mode.
#pragma once
#include "Arduino.h"
class LiquidCrystal : public Print {
 public:
  LiquidCrystal() {}
  LiquidCrystal(uint8_t rs, uint8_t en, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7) {
    (void)rs; (void)en; (void)d4; (void)d5; (void)d6; (void)d7;
  }
  void begin(uint8_t cols, uint8_t rows);
  void clear();
  void home();
  void setCursor(uint8_t col, uint8_t row);
  size_t write(uint8_t c) override;
  using Print::write;
  void display() {}
  void noDisplay() {}
  void createChar(uint8_t, uint8_t*) {}
};
