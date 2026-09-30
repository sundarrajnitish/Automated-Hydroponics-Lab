// Compiles firmware/Hydroponics/Hydroponics.ino, unmodified, against the mocks.
#include "arduino_mock/Arduino.h"
#include "arduino_mock/Keypad.h"
#include "arduino_mock/dht.h"
#include "arduino_mock/LiquidCrystal.h"
#include "arduino_mock/EEPROM.h"
#include "arduino_mock/avr/wdt.h"
#include "../firmware/Hydroponics/src/hydro_core.h"
#include "sketch.h"

namespace fw {
#include "../firmware/Hydroponics/Hydroponics.ino"

static void reset_globals() {
  lcd = LiquidCrystal(LCD_PINS[0], LCD_PINS[1], LCD_PINS[2], LCD_PINS[3], LCD_PINS[4], LCD_PINS[5]);
  keypad = Keypad(makeKeymap(KEYMAP), (byte*)KEYPAD_ROW_PINS, (byte*)KEYPAD_COL_PINS, 4, 4);
  DHT = dht();
  controller = hydro::Controller();
  hal = Hal();
}
static double reported_ph() { return controller.ph_valid() ? (double)controller.ph() : -1.0; }
static int program() { return (int)controller.program(); }
static int alarms() { return (int)controller.alarms(); }
static int dose_state() { return (int)controller.dose_state(); }
static double gain() { return (double)controller.gain(); }
static double dose_ms() { return (double)controller.dose_ms_today(); }
static int screen() { return (int)controller.screen(); }
}  // namespace fw

const sim::SketchIface sim::FIRMWARE = {
    "hydroponinator", fw::reset_globals, fw::setup, fw::loop, fw::reported_ph, fw::program,
    fw::alarms, fw::dose_state, fw::gain, fw::dose_ms, fw::screen};
