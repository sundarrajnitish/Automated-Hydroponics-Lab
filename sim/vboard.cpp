#include "vboard.h"

namespace sim {

Board* g_board = nullptr;

void Board::power_on(const PlantParams& p, uint64_t seed) {
  now_us = 0;
  plant.init(p, seed);
  for (int i = 0; i < 4096; ++i) eeprom[i] = 0xFF;  // factory-fresh
  eeprom_writes = 0;
  nkeys = 0; keys_pressed = keys_caught = keys_missed = 0;
  key_latency_sum_ms = key_latency_max_ms = 0;
  serial_total = 0;
  wdt_resets = 0;
  loops = 0; loop_max_ms = last_loop_ms = 0;
  cpu_reset();
}

void Board::cpu_reset() {
  for (int i = 0; i < NUM_PINS; ++i) { pin_mode[i] = 0; pin_level[i] = 0; }
  for (int i = 0; i < 128; ++i) ddram[i] = ' ';
  lcd_addr = 0; lcd_begun = false;
  baud = 0; tx_busy_until_us = now_us;
  wdt_on = false; wdt_ms = 0; wdt_last_us = now_us;
  dht_last_ms = 0;
  sync_loads();
}

bool Board::load_on(int l) const {
  uint8_t pin = LOAD_PIN[l];
  if (pin_mode[pin] != 1) return false;  // floating input: relay/driver stays off
  return LOAD_ACTIVE_LOW[l] ? pin_level[pin] == 0 : pin_level[pin] == 1;
}

uint32_t Board::load_bits() const {
  uint32_t b = 0;
  for (int l = 0; l < L_COUNT; ++l) if (load_on(l)) b |= 1u << l;
  return b;
}

void Board::sync_loads() {
  for (int l = 0; l < L_COUNT; ++l) plant.on[l] = load_on(l);
}

void Board::advance_us(uint64_t us) {
  if (!us) return;
  sync_loads();
  plant.advance((double)us * 1e-6);
  now_us += us;
  for (int i = 0; i < nkeys; ++i) {
    KeyPress& k = keys[i];
    if (k.state == 0 && now_us >= k.end_us) { k.state = 2; ++keys_missed; }
  }
}

void Board::press_key(char k, double dur_ms, double delay_ms) {
  // drop settled presses to keep the list short
  int w = 0;
  for (int i = 0; i < nkeys; ++i) if (keys[i].state == 0) keys[w++] = keys[i];
  nkeys = w;
  if (nkeys >= MAX_KEYS) return;
  KeyPress& p = keys[nkeys++];
  p.t_us = now_us + (uint64_t)(delay_ms * 1000.0); p.end_us = p.t_us + (uint64_t)(dur_ms * 1000.0); p.seen_us = 0;
  p.key = k; p.state = 0;
  ++keys_pressed;
}

// The Keypad library reports a key once, on the scan that first sees it
// down. A press that starts and ends between two scans is never seen.
char Board::poll_key() {
  for (int i = 0; i < nkeys; ++i) {
    KeyPress& k = keys[i];
    if (k.state == 0 && k.t_us <= now_us && now_us < k.end_us) {
      k.state = 1; k.seen_us = now_us; ++keys_caught;
      double lat = (double)(now_us - k.t_us) / 1000.0;
      key_latency_sum_ms += lat;
      if (lat > key_latency_max_ms) key_latency_max_ms = lat;
      return k.key;
    }
  }
  return 0;
}

void Board::serial_write(uint8_t c) {
  if (!baud) return;  // Serial.begin() never called: bytes go nowhere
  uint64_t char_us = 10000000ULL / baud;
  // 64-byte TX ring: block while it is full, like HardwareSerial::write
  while (tx_busy_until_us > now_us && (tx_busy_until_us - now_us) / char_us >= 63) {
    advance_us(tx_busy_until_us - now_us - 62 * char_us);
  }
  uint64_t start = tx_busy_until_us > now_us ? tx_busy_until_us : now_us;
  tx_busy_until_us = start + char_us;
  serial_log[serial_total % SERIAL_CAP] = (char)c;
  ++serial_total;
  advance_us(6);  // ISR + buffer bookkeeping
}

void Board::lcd_write(uint8_t c) {
  ddram[lcd_addr & 0x7F] = c;
  ++lcd_bytes;
  // HD44780 two-line addressing: 0x00-0x27 then 0x40-0x67
  if (lcd_addr == 0x27) lcd_addr = 0x40;
  else if (lcd_addr == 0x67) lcd_addr = 0x00;
  else ++lcd_addr;
  advance_us(250);  // two nibbles, each with a 100 us settle in LiquidCrystal
}

}  // namespace sim
