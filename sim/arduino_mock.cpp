// Implementation of the mock Arduino core on top of sim::Board.
#include "arduino_mock/Arduino.h"
#include "arduino_mock/Keypad.h"
#include "arduino_mock/dht.h"
#include "arduino_mock/LiquidCrystal.h"
#include "arduino_mock/EEPROM.h"
#include "arduino_mock/avr/wdt.h"

using sim::g_board;

HardwareSerial Serial;
EEPROMClass EEPROM;

void pinMode(uint8_t pin, uint8_t mode) {
  if (pin >= sim::NUM_PINS) return;
  g_board->pin_mode[pin] = mode == OUTPUT ? 1 : 0;
  if (mode == INPUT_PULLUP) g_board->pin_level[pin] = 1;
  g_board->sync_loads();
}

void digitalWrite(uint8_t pin, uint8_t val) {
  if (pin >= sim::NUM_PINS) return;
  g_board->pin_level[pin] = val ? 1 : 0;
  g_board->sync_loads();
  g_board->advance_us(4);
}

int digitalRead(uint8_t pin) { return pin < sim::NUM_PINS ? g_board->pin_level[pin] : 0; }

int analogRead(uint8_t pin) {
  if (pin < 54) pin += 54;
  int v = 0;
  if (pin == sim::PIN_PH) v = g_board->plant.adc_ph();
  else if (pin == sim::PIN_LEVEL) v = g_board->plant.adc_level();
  g_board->advance_us(112);  // 13 ADC clocks at 125 kHz + overhead
  return v;
}

unsigned long millis() { return (unsigned long)(uint32_t)(g_board->now_us / 1000ULL); }
unsigned long micros() { return (unsigned long)(uint32_t)g_board->now_us; }
void delay(unsigned long ms) { g_board->advance_us((uint64_t)ms * 1000ULL); }
void delayMicroseconds(unsigned int us) { g_board->advance_us(us); }

void HardwareSerial::begin(unsigned long baud) { g_board->baud = (uint32_t)baud; }
size_t HardwareSerial::write(uint8_t c) { g_board->serial_write(c); return 1; }
void HardwareSerial::flush() {
  if (g_board->tx_busy_until_us > g_board->now_us) g_board->advance_us(g_board->tx_busy_until_us - g_board->now_us);
}

char Keypad::getKey() {
  g_board->advance_us(60);  // column/row scan
  return g_board->poll_key();
}

int dht::read11(uint8_t pin) {
  (void)pin;
  g_board->advance_us(23000);  // 18 ms start pulse + 40 bits
  int t, h;
  if (!g_board->plant.dht_read(&t, &h)) {
    humidity = DHTLIB_INVALID_VALUE;
    temperature = DHTLIB_INVALID_VALUE;
    return DHTLIB_ERROR_CHECKSUM;
  }
  humidity = h; temperature = t;
  return DHTLIB_OK;
}

void LiquidCrystal::begin(uint8_t, uint8_t) {
  g_board->advance_us(50000);  // power-up wait in LiquidCrystal::begin
  g_board->lcd_begun = true;
  clear();
}
void LiquidCrystal::clear() {
  for (int i = 0; i < 128; ++i) g_board->ddram[i] = ' ';
  g_board->lcd_addr = 0;
  g_board->advance_us(2250);
}
void LiquidCrystal::home() { g_board->lcd_addr = 0; g_board->advance_us(2250); }
void LiquidCrystal::setCursor(uint8_t col, uint8_t row) {
  if (row > 1) row = 1;
  g_board->lcd_addr = (uint8_t)(col + (row ? 0x40 : 0x00));
  g_board->advance_us(250);
}
size_t LiquidCrystal::write(uint8_t c) { g_board->lcd_write(c); return 1; }

uint8_t EEPROMClass::read(int addr) { return (addr >= 0 && addr < 4096) ? g_board->eeprom[addr] : 0xFF; }
void EEPROMClass::write(int addr, uint8_t v) {
  if (addr < 0 || addr >= 4096) return;
  g_board->eeprom[addr] = v;
  ++g_board->eeprom_writes;
  g_board->advance_us(3300);
}

static const uint32_t WDT_MS[] = {15, 30, 60, 120, 250, 500, 1000, 2000, 4000, 8000};
void wdt_enable(uint8_t t) {
  g_board->wdt_on = true;
  g_board->wdt_ms = WDT_MS[t < 10 ? t : 9];
  g_board->wdt_last_us = g_board->now_us;
}
void wdt_disable() { g_board->wdt_on = false; }
void wdt_reset() { g_board->wdt_last_us = g_board->now_us; }
