// Hydroponinator - automated hydroponics controller for Arduino Mega 2560
// Author: Nitish Sundarraj
//
// Hardware: 4x4 keypad, 16x2 LCD, DHT11, analog pH board, water-level strip,
// circulation pump + two mini dosing pumps, grow light, fluorescent tubes,
// fan and humidifier. Pin map in config.h.
//
// Libraries (Library Manager): "Keypad" (Mark Stanley, Alexander Brevig),
// "DHTlib" (Rob Tillaart), LiquidCrystal and EEPROM (bundled).
//
// This file is only the hardware layer: it samples sensors on a schedule,
// never blocks, and passes everything to hydro::Controller (src/), which
// holds all of the decisions and is unit-tested on a PC.
//
// Keys
//   A automatic   B manual   C monitor pages   D economy
//   manual mode:  1/4 pump on/off   2/5 nutrient   3/6 pH-down
//                 7/* fan           8/0 fluorescent 9/# grow light
//   monitor:      # next page   * set clock   0 calibrate pH (7 then 4)

#include <Keypad.h>
#include <LiquidCrystal.h>
#include <dht.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <stddef.h>

#include "config.h"
#include "src/hydro_core.h"

LiquidCrystal lcd(LCD_PINS[0], LCD_PINS[1], LCD_PINS[2], LCD_PINS[3], LCD_PINS[4], LCD_PINS[5]);
Keypad keypad(makeKeymap(KEYMAP), (byte*)KEYPAD_ROW_PINS, (byte*)KEYPAD_COL_PINS, 4, 4);
dht DHT;
hydro::Controller controller;

struct Persist {
  uint16_t magic;
  uint8_t version;
  uint8_t program;
  hydro::Calibration cal;
  uint8_t crc;
};

// Everything the hardware layer remembers between loop() passes.
struct Hal {
  bool applied[hydro::CH_COUNT];
  char shown[2][17];
  uint32_t lastPhSample, lastDht, lastLevel, lastTelemetry, lastLcd;
  int phRaw[10];
  uint8_t phCount;
  hydro::Outputs out;
  bool keyPending;
};
Hal hal;

// --------------------------------------------------------------- EEPROM

static uint8_t crc8(const uint8_t* p, uint8_t n) {  // CRC-8/SMBUS
  uint8_t c = 0;
  while (n--) {
    c ^= *p++;
    for (uint8_t i = 0; i < 8; ++i) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

static void loadSettings(hydro::Settings& s, hydro::Program& prog) {
  s = hydro::default_settings();
  prog = hydro::PROG_AUTO;
  Persist p;
  EEPROM.get(EEPROM_ADDR, p);
  if (p.magic != EEPROM_MAGIC || p.version != EEPROM_VERSION) return;
  if (crc8((const uint8_t*)&p, (uint8_t)offsetof(Persist, crc)) != p.crc) return;
  if (p.program <= hydro::PROG_ECONOMY) prog = (hydro::Program)p.program;
  if (hydro::calibration_plausible(p.cal)) s.cal = p.cal;
}

static void saveSettings() {
  Persist p;
  for (uint8_t i = 0; i < sizeof(p); ++i) ((uint8_t*)&p)[i] = 0;
  p.magic = EEPROM_MAGIC;
  p.version = EEPROM_VERSION;
  p.program = controller.program();
  p.cal = controller.settings().cal;
  p.crc = crc8((const uint8_t*)&p, (uint8_t)offsetof(Persist, crc));
  EEPROM.put(EEPROM_ADDR, p);  // put() only rewrites bytes that changed
}

// -------------------------------------------------------------- sensors

// One ADC sample every PH_SAMPLE_MS; after ten, drop the two highest and two
// lowest and average the rest (the classic pH-board filter, done without
// stalling the loop).
static void samplePh(uint32_t now, hydro::Inputs& in) {
  if (now - hal.lastPhSample < PH_SAMPLE_MS) return;
  hal.lastPhSample = now;
  hal.phRaw[hal.phCount++] = analogRead(PIN_PH);
  if (hal.phCount < 10) return;
  hal.phCount = 0;
  int s[10];
  for (uint8_t i = 0; i < 10; ++i) s[i] = hal.phRaw[i];
  for (uint8_t i = 1; i < 10; ++i)
    for (uint8_t j = i; j > 0 && s[j - 1] > s[j]; --j) { int t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
  long sum = 0;
  for (uint8_t i = 2; i < 8; ++i) sum += s[i];
  in.ph_ready = true;
  in.ph_volts = (float)sum / 6.0f * ADC_REF_V / 1024.0f;
  in.ph_adc_min = (uint16_t)s[0];
  in.ph_adc_max = (uint16_t)s[9];
}

static void readClimate(uint32_t now, hydro::Inputs& in) {
  if (now - hal.lastDht < DHT_SAMPLE_MS) return;
  hal.lastDht = now;
  int rc = DHT.read11(PIN_DHT);
  in.dht_ready = true;
  in.dht_ok = rc == DHTLIB_OK && DHT.temperature > -20 && DHT.temperature < 70 && DHT.humidity >= 0 &&
              DHT.humidity <= 100;
  if (in.dht_ok) {
    in.temp_c = (int8_t)DHT.temperature;
    in.rh = (uint8_t)DHT.humidity;
  }
}

static void readLevel(uint32_t now, hydro::Inputs& in) {
  if (now - hal.lastLevel < LEVEL_SAMPLE_MS) return;
  hal.lastLevel = now;
  long a = analogRead(PIN_LEVEL);
  long pct = (a - LEVEL_ADC_EMPTY) * 100L / (LEVEL_ADC_FULL - LEVEL_ADC_EMPTY);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  in.level_ready = true;
  in.level_pct = (uint8_t)pct;
}

// -------------------------------------------------------------- outputs

static void writeChannel(uint8_t ch, bool on) {
  bool level = CHANNEL_ACTIVE_LOW[ch] ? !on : on;
  digitalWrite(CHANNEL_PIN[ch], level ? HIGH : LOW);
  hal.applied[ch] = on;
}

static void applyOutputs(const hydro::Outputs& out) {
  for (uint8_t ch = 0; ch < hydro::CH_COUNT; ++ch)
    if (out.on[ch] != hal.applied[ch]) writeChannel(ch, out.on[ch]);
}

// Rewrite only the characters that changed: a full 32-character refresh
// costs ~9 ms on a 4-bit HD44780, a typical update well under 1 ms.
static void refreshLcd(uint32_t now, bool urgent) {
  if (!urgent && now - hal.lastLcd < 200) return;
  hal.lastLcd = now;
  for (uint8_t row = 0; row < 2; ++row) {
    int8_t cursor = -1;
    for (uint8_t col = 0; col < 16; ++col) {
      char c = hal.out.lcd[row][col];
      if (c == hal.shown[row][col]) { cursor = -1; continue; }
      if (cursor != (int8_t)col) lcd.setCursor(col, row);
      lcd.write((uint8_t)c);
      hal.shown[row][col] = c;
      cursor = (int8_t)(col + 1);
    }
  }
}

static void telemetry(uint32_t now) {
  if (now - hal.lastTelemetry < TELEMETRY_MS) return;
  hal.lastTelemetry = now;
  uint8_t bits = 0;
  for (uint8_t ch = 0; ch < hydro::CH_COUNT; ++ch) if (hal.applied[ch]) bits |= (uint8_t)(1u << ch);
  Serial.print(now / 1000UL);
  Serial.print(',');
  Serial.print((int)controller.program());
  Serial.print(',');
  if (controller.ph_valid()) Serial.print(controller.ph(), 2);
  Serial.print(',');
  Serial.print(controller.volts(), 3);
  Serial.print(',');
  Serial.print((int)controller.temp_c());
  Serial.print(',');
  Serial.print((int)controller.rh());
  Serial.print(',');
  Serial.print((int)controller.level_pct());
  Serial.print(',');
  Serial.print((int)bits);
  Serial.print(',');
  Serial.print((unsigned int)controller.alarms());
  Serial.print(',');
  Serial.print((int)controller.dose_state());
  Serial.print(',');
  Serial.println(controller.dose_ms_today());
}

// ---------------------------------------------------------- setup/loop

void setup() {
  wdt_disable();
  // Park every output at OFF before it becomes an output: relay loads must
  // not click on during boot.
  for (uint8_t ch = 0; ch < hydro::CH_COUNT; ++ch) {
    digitalWrite(CHANNEL_PIN[ch], CHANNEL_ACTIVE_LOW[ch] ? HIGH : LOW);
    pinMode(CHANNEL_PIN[ch], OUTPUT);
    hal.applied[ch] = false;
  }
  Serial.begin(SERIAL_BAUD);
  Serial.println(F("# hydroponinator: t_s,program,ph,volts,temp_c,rh,level_pct,outputs,alarms,dose_state,acid_ms_today"));
  lcd.begin(16, 2);
  for (uint8_t r = 0; r < 2; ++r) {
    for (uint8_t c = 0; c < 16; ++c) hal.shown[r][c] = ' ';
    hal.shown[r][16] = 0;
  }
  hydro::Settings s;
  hydro::Program prog;
  loadSettings(s, prog);
  uint32_t now = millis();
  controller.begin(s, prog, now);
  hal.lastPhSample = hal.lastDht = hal.lastLevel = hal.lastLcd = now;
  hal.lastTelemetry = now;
  hal.phCount = 0;
  wdt_enable(WDTO_4S);  // a hung loop reboots into the last program
}

void loop() {
  wdt_reset();
  const uint32_t now = millis();
  hydro::Inputs in;
  in.now_ms = now;
  in.key = keypad.getKey();
  in.ph_ready = in.dht_ready = in.dht_ok = in.level_ready = false;
  in.ph_volts = 0;
  in.ph_adc_min = in.ph_adc_max = 0;
  in.temp_c = 0;
  in.rh = in.level_pct = 0;
  samplePh(now, in);
  readClimate(now, in);
  readLevel(now, in);

  controller.step(in, hal.out);
  applyOutputs(hal.out);
  refreshLcd(now, in.key != NO_KEY);
  if (hal.out.save) saveSettings();
  telemetry(now);
}
