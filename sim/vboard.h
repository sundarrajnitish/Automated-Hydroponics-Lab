// vboard.h - a virtual Arduino Mega 2560 wired to the simulated rig.
//
// The mock Arduino headers in arduino_mock/ route every pinMode(),
// digitalWrite(), analogRead(), delay(), LCD write, keypad scan and EEPROM
// access through the Board below. Time only moves when the sketch spends it
// (delay(), an ADC conversion, an LCD transfer, a DHT11 read...) or when the
// driver advances between loop() calls, so blocking code blocks here exactly
// as it would on the real board.
#pragma once
#include <stdint.h>
#include "plant.h"

namespace sim {

static const int NUM_PINS = 70;
static const int PIN_A0 = 54;

// Hardware wiring of the rig (same as firmware/Hydroponics/config.h):
// relay-module loads are active-LOW, the transistor-driven mini pumps and the
// humidifier are active-HIGH.
static const uint8_t LOAD_PIN[L_COUNT] = {10, 11, 12, 13, 14, 15, 16};
static const bool LOAD_ACTIVE_LOW[L_COUNT] = {true, false, false, true, true, true, false};
static const uint8_t PIN_DHT = PIN_A0 + 0, PIN_PH = PIN_A0 + 1, PIN_LEVEL = PIN_A0 + 2;

struct KeyPress {
  uint64_t t_us, end_us, seen_us;
  char key;
  uint8_t state;  // 0 pending, 1 caught, 2 missed
};

struct Board {
  uint64_t now_us = 0;
  uint8_t pin_mode[NUM_PINS] = {};
  uint8_t pin_level[NUM_PINS] = {};
  Plant plant;

  // HD44780 16x2 in 4-bit mode
  uint8_t ddram[128];
  uint8_t lcd_addr = 0;
  bool lcd_begun = false;
  uint32_t lcd_bytes = 0;

  // UART0 (what the Serial Monitor would show)
  static const uint32_t SERIAL_CAP = 16384;
  char serial_log[SERIAL_CAP];
  uint32_t serial_total = 0;  // monotonically increasing write index
  uint32_t baud = 0;
  uint64_t tx_busy_until_us = 0;

  // 4x4 keypad: scheduled presses
  static const int MAX_KEYS = 64;
  KeyPress keys[MAX_KEYS];
  int nkeys = 0;
  uint32_t keys_pressed = 0, keys_caught = 0, keys_missed = 0;
  double key_latency_sum_ms = 0, key_latency_max_ms = 0;

  uint8_t eeprom[4096];
  uint32_t eeprom_writes = 0;

  bool wdt_on = false;
  uint32_t wdt_ms = 0;
  uint64_t wdt_last_us = 0;
  uint32_t wdt_resets = 0;

  uint64_t loops = 0;
  double loop_max_ms = 0, last_loop_ms = 0;
  uint32_t dht_last_ms = 0;

  void power_on(const PlantParams& p, uint64_t seed);  // cold boot
  void cpu_reset();                                     // pins float, RAM gone
  void advance_us(uint64_t us);
  void sync_loads();
  bool load_on(int load) const;
  uint32_t load_bits() const;
  void press_key(char k, double dur_ms, double delay_ms = 0);
  char poll_key();
  void serial_write(uint8_t c);
  void lcd_write(uint8_t c);
};

extern Board* g_board;

}  // namespace sim
