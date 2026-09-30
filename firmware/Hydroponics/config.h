// config.h - wiring and board-specific constants for the Hydroponinator.
// Change pins or relay polarity here; nothing else in the firmware needs to know.
#pragma once
#include "src/hydro_core.h"

// Output channels, in hydro::Channel order.
static const uint8_t CHANNEL_PIN[hydro::CH_COUNT] = {
  10,  // CH_PUMP        40 W circulation pump    (relay module)
  11,  // CH_PH_DOWN     pH-down mini pump        (transistor driver)
  12,  // CH_NUTRIENT    nutrient mini pump       (transistor driver)
  13,  // CH_GROW_LIGHT  full-spectrum grow light (relay module)
  14,  // CH_FLUO_LIGHT  fluorescent tubes        (relay module)
  15,  // CH_FAN         ventilation fan          (relay module)
  16,  // CH_HUMIDIFIER  humidifier               (transistor driver)
};

// The common blue 4-channel relay boards switch ON when their input is pulled
// LOW, while the transistor drivers switch ON when it is HIGH. Every channel
// says which convention it uses, and setup() parks each pin at its OFF level
// before enabling it, so no load clicks on during boot.
static const bool CHANNEL_ACTIVE_LOW[hydro::CH_COUNT] = {
  true, false, false, true, true, true, false,
};

// Sensors
static const uint8_t PIN_DHT = A0;     // DHT11 data
static const uint8_t PIN_PH = A1;      // analog pH amplifier board
static const uint8_t PIN_LEVEL = A2;   // resistive water-level strip

// 16x2 LCD (RS, E, D4..D7) and 4x4 keypad
static const uint8_t LCD_PINS[6] = {43, 45, 47, 49, 51, 53};
static const byte KEYPAD_ROW_PINS[4] = {9, 8, 7, 6};
static const byte KEYPAD_COL_PINS[4] = {5, 4, 3, 2};
static const char KEYMAP[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'},
};

// Analog front end
static const float ADC_REF_V = 5.0f;
static const uint16_t PH_SAMPLE_MS = 100;    // 10 samples -> one reading per second
static const uint16_t LEVEL_SAMPLE_MS = 500;
static const uint16_t DHT_SAMPLE_MS = 2000;  // DHT11: at most one read per second
static const int LEVEL_ADC_EMPTY = 40;       // strip dry
static const int LEVEL_ADC_FULL = 640;       // strip fully submerged (tank full)

// Serial telemetry (CSV) for logging or plotting
static const unsigned long SERIAL_BAUD = 115200;
static const uint16_t TELEMETRY_MS = 5000;

// EEPROM
static const int EEPROM_ADDR = 0;
static const uint16_t EEPROM_MAGIC = 0x4859;  // "HY"
static const uint8_t EEPROM_VERSION = 1;
