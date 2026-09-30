// Host unit tests for hydro::Controller (no Arduino needed).
//   make -C firmware/test
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../Hydroponics/src/hydro_core.h"

using namespace hydro;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) CHECK(((a) - (b)) < (tol) && ((b) - (a)) < (tol))

// A tiny rig: feeds the controller the way the sketch does.
struct Rig {
  Controller c;
  Outputs out;
  Inputs in;
  uint32_t now;
  float volts;      // what the pH board outputs
  int level;
  int temp, rh;
  bool dht_ok;
  uint16_t adc_min, adc_max;
  uint32_t next_ph, next_level, next_dht;
  bool saved = false;       // controller asked for an EEPROM write at some point

  explicit Rig(uint32_t start = 0, Program p = PROG_AUTO, Settings s = default_settings()) {
    now = start;
    volts = 6.0f / 3.5f;   // pH 6.0 with the default calibration
    level = 80; temp = 24; rh = 60; dht_ok = true;
    adc_min = 300; adc_max = 400;
    next_ph = next_level = next_dht = start;
    c.begin(s, p, now);
    std::memset(&out, 0, sizeof(out));
  }
  void tick(char key = 0) {
    std::memset(&in, 0, sizeof(in));
    in.now_ms = now;
    in.key = key;
    if ((int32_t)(now - next_ph) >= 0) {
      in.ph_ready = true; in.ph_volts = volts; in.ph_adc_min = adc_min; in.ph_adc_max = adc_max;
      next_ph = now + 1000;
    }
    if ((int32_t)(now - next_level) >= 0) { in.level_ready = true; in.level_pct = (uint8_t)level; next_level = now + 500; }
    if ((int32_t)(now - next_dht) >= 0) {
      in.dht_ready = true; in.dht_ok = dht_ok; in.temp_c = (int8_t)temp; in.rh = (uint8_t)rh; next_dht = now + 2000;
    }
    c.step(in, out);
    saved |= out.save;
  }
  void run(uint32_t ms, uint32_t dt = 50) { for (uint32_t t = 0; t < ms; t += dt) { tick(); now += dt; } }
  void key(char k) { tick(k); now += 50; tick(); now += 50; }
  void set_ph(float ph) { volts = ph / 3.5f; }
  bool line_is(int row, const char* s) { return std::strncmp(out.lcd[row], s, 16) == 0; }
};

static void test_conversion() {
  std::puts("conversion + calibration");
  Calibration d = default_calibration();
  NEAR(volts_to_ph(2.0f, d), 7.0f, 1e-5f);
  NEAR(volts_to_ph(1.0f, d), 3.5f, 1e-5f);      // DFRobot: pH = 3.5 V
  Calibration c = {1.80f, 1.00f};               // slope 3.75 pH/V, offset
  NEAR(volts_to_ph(1.80f, c), 7.0f, 1e-5f);
  NEAR(volts_to_ph(1.00f, c), 4.0f, 1e-5f);
  CHECK(calibration_plausible(d));
  CHECK(calibration_plausible(c));
  Calibration swapped = {1.0f, 1.8f};
  CHECK(!calibration_plausible(swapped));
  Calibration flat = {1.80f, 1.75f};             // buffers mixed up / dead probe
  CHECK(!calibration_plausible(flat));
}

static void test_boot_and_lights() {
  std::puts("boot, splash, photoperiod, pump cycle");
  Rig r;
  r.next_level = r.now + 1000;                    // no level reading yet
  r.tick();
  CHECK(r.line_is(0, " HYDROPONINATOR "));
  CHECK(!r.out.on[CH_PUMP]);                      // unknown level: pump stays off
  r.run(2100);
  CHECK(r.c.screen() == SCR_HOME);
  CHECK(r.out.on[CH_GROW_LIGHT] && r.out.on[CH_FLUO_LIGHT]);   // boots at 06:00 = lights on
  CHECK(r.out.on[CH_PUMP]);                       // 06:00 is inside a 15-min ON slot
  CHECK(r.line_is(0, "AUT pH6.00 06:00"));
  r.run(16 * 60 * 1000, 1000);                    // 06:16 -> OFF slot (15 on / 15 off by day)
  CHECK(!r.out.on[CH_PUMP]);
  r.run(15 * 60 * 1000, 1000);                    // 06:31 -> ON again
  CHECK(r.out.on[CH_PUMP]);
  // 22:05: lights off after 16 h, night cycle is 15 on / 45 off
  r.run((uint32_t)(15 * 3600 + 34 * 60) * 1000, 1000);
  CHECK(r.c.clock_s(r.now) / 60 == 22 * 60 + 5);
  CHECK(!r.out.on[CH_GROW_LIGHT] && !r.out.on[CH_FLUO_LIGHT]);
  // economy: 12 h of grow light only
  Rig e(0, PROG_ECONOMY);
  e.run(3000);
  CHECK(e.out.on[CH_GROW_LIGHT] && !e.out.on[CH_FLUO_LIGHT]);
  e.run(12UL * 3600 * 1000 + 60000, 1000);
  CHECK(!e.out.on[CH_GROW_LIGHT]);
}

static void test_low_water() {
  std::puts("low-water interlock");
  Rig r;
  r.run(3000);
  CHECK(r.out.on[CH_PUMP]);
  r.level = 25; r.run(1000);
  CHECK(!r.out.on[CH_PUMP]);
  CHECK(r.c.alarms() & ALM_LOW_WATER);
  r.level = 30; r.run(1000);                      // inside the hysteresis band
  CHECK(!r.out.on[CH_PUMP]);
  r.level = 33; r.run(1000);
  CHECK(r.out.on[CH_PUMP]);
  CHECK(!(r.c.alarms() & ALM_LOW_WATER));
  // sensor goes silent -> treated as low
  Rig s;
  s.run(3000);
  for (int i = 0; i < 300; ++i) { s.next_level = s.now + 100000; s.tick(); s.now += 50; }
  CHECK(!s.out.on[CH_PUMP]);
  // manual mode cannot run the pump dry either
  Rig m;
  m.run(3000);
  m.key('B'); m.key('1');
  CHECK(m.out.on[CH_PUMP]);
  m.level = 10; m.run(1000);
  CHECK(!m.out.on[CH_PUMP]);
}

// Drive a dose cycle; returns the dose length the controller chose.
static uint32_t pump_one_dose(Rig& r, float ph_after) {
  uint32_t before = r.c.dose_ms_today();
  for (int i = 0; i < 2000 && r.c.dose_state() != DOSE_MIXING; ++i) { r.tick(); r.now += 10; }
  uint32_t on_ms = r.c.dose_ms_today() - before;
  CHECK(r.c.dose_state() == DOSE_MIXING);
  r.set_ph(ph_after);
  for (int i = 0; i < 400 && r.c.dose_state() == DOSE_MIXING; ++i) {
    CHECK(r.out.on[CH_PUMP]);                     // circulation forced on while mixing
    CHECK(!r.out.on[CH_PH_DOWN]);
    r.run(500);
  }
  return on_ms;
}

static void test_dosing() {
  std::puts("dose-and-wait pH control");
  Rig r;
  r.set_ph(6.60f);
  r.run(25000);
  CHECK(r.c.dose_state() == DOSE_IDLE);            // 30 s electrode warm-up after boot
  r.set_ph(6.25f);
  r.run(10000);
  CHECK(r.c.dose_state() == DOSE_IDLE);            // below the 6.3 start threshold
  r.set_ph(6.60f);
  r.run(3000);
  CHECK(r.c.dose_state() == DOSE_PUMPING);
  // first dose: (6.6 - 6.0) / 0.15 pH/s * 0.8 = 3.2 s -> capped at 3.0 s
  uint32_t first = pump_one_dose(r, 6.30f);
  NEAR((float)first, 3000.0f, 60.0f);
  // response 0.30 pH / 3 s = 0.1 pH/s -> gain = (0.15 + 0.1) / 2
  NEAR(r.c.gain(), 0.125f, 0.01f);
  r.run(1500);
  CHECK(r.c.dose_state() == DOSE_PUMPING);         // still above target+0.05: keep correcting
  uint32_t second = pump_one_dose(r, 6.02f);
  NEAR((float)second, 0.30f / r.c.gain() * 800.0f, 400.0f);
  r.run(10000);
  CHECK(r.c.dose_state() == DOSE_IDLE);            // 6.02 <= 6.05: done
  CHECK(r.c.doses_today() == 2);
  CHECK(r.c.alarms() == 0);
}

static void test_no_response_lockout() {
  std::puts("no-response lock-out (stuck probe / empty bottle)");
  Rig r;
  r.set_ph(7.20f);                                 // probe stuck reading high
  r.run(31000);
  for (int i = 0; i < 3; ++i) pump_one_dose(r, 7.20f);
  r.run(2000);
  CHECK(r.c.dose_state() == DOSE_LOCKED);
  CHECK(r.c.alarms() & ALM_NO_RESPONSE);
  bool pumped = false;
  for (int i = 0; i < 600; ++i) { r.run(1000); pumped |= r.out.on[CH_PH_DOWN]; }
  CHECK(!pumped);                                  // the acid bottle is safe
  r.key('A');                                      // operator acknowledges
  CHECK(r.c.dose_state() != DOSE_LOCKED);
}

static void test_budget_and_midnight() {
  std::puts("daily acid allowance");
  Settings s = default_settings();
  s.dose_budget_ms_day = 4000;
  Rig r(0, PROG_AUTO, s);
  r.set_ph(7.0f);
  r.run(31000);
  pump_one_dose(r, 6.8f);                          // 3 s used
  r.run(3000);
  pump_one_dose(r, 6.7f);                          // only 1 s left
  r.run(5000);
  CHECK(r.c.dose_ms_today() <= 4000);
  CHECK(r.c.alarms() & ALM_DOSE_BUDGET);
  // run past midnight (boot is 06:00)
  r.run(18UL * 3600 * 1000 + 60000, 2000);
  CHECK(!(r.c.alarms() & ALM_DOSE_BUDGET) || r.c.dose_ms_today() > 0);
}

static void test_probe_fault() {
  std::puts("pH probe fault");
  Rig r;
  r.adc_min = 0; r.adc_max = 2;                    // electrode unplugged: ADC at the rail
  r.volts = 0.005f;
  r.run(8000);
  CHECK(!r.c.ph_valid());
  CHECK(r.c.alarms() & ALM_PH_PROBE);
  CHECK(r.c.dose_state() == DOSE_IDLE);
  CHECK(!r.out.on[CH_PH_DOWN]);
}

static void test_implausible_reading() {
  std::puts("implausible reading (amplifier stuck high)");
  Rig r;
  r.run(31000);
  r.volts = 3.2f;                                  // reads pH 11.2: impossible in an aerated tank
  r.run(20000);
  CHECK(r.c.alarms() & ALM_PH_PROBE);
  CHECK(r.c.dose_state() == DOSE_IDLE);
  CHECK(r.c.dose_ms_today() == 0);
}

static void test_retry_doses_are_small() {
  std::puts("retries after an unanswered dose are small");
  Rig r;
  r.set_ph(7.0f);
  r.run(31000);
  uint32_t first = pump_one_dose(r, 7.0f);
  r.run(1500);
  uint32_t second = pump_one_dose(r, 7.0f);
  CHECK(first >= 2900);
  CHECK(second <= 400);
}

static void test_manual() {
  std::puts("manual mode");
  Rig r;
  r.run(3000);
  r.key('B');
  CHECK(r.c.program() == PROG_MANUAL);
  CHECK(r.out.on[CH_PUMP] && r.out.on[CH_GROW_LIGHT]);   // bumpless: nothing clicked off
  r.key('4'); CHECK(!r.out.on[CH_PUMP]);
  r.key('1'); CHECK(r.out.on[CH_PUMP]);
  r.key('7'); CHECK(r.out.on[CH_FAN]);
  r.key('*'); CHECK(!r.out.on[CH_FAN]);
  r.key('0'); CHECK(!r.out.on[CH_FLUO_LIGHT]);
  r.key('8'); CHECK(r.out.on[CH_FLUO_LIGHT]);
  r.key('#'); CHECK(!r.out.on[CH_GROW_LIGHT]);
  r.key('3'); CHECK(r.out.on[CH_PH_DOWN]);
  r.run(9000); CHECK(r.out.on[CH_PH_DOWN]);
  r.run(1200); CHECK(!r.out.on[CH_PH_DOWN]);       // 10 s safety limit
  r.key('3'); r.key('6'); CHECK(!r.out.on[CH_PH_DOWN]);
  // automatic pH control stays out of the way of the operator
  r.set_ph(7.5f);
  r.run(30000);
  CHECK(!r.out.on[CH_PH_DOWN]);
  CHECK(r.line_is(1, "P+N-A-F-L+G-    ") || (r.c.alarms() != 0));
}

static void test_calibration_wizard() {
  std::puts("two-point calibration wizard");
  Rig r;
  r.run(3000);
  r.key('C'); CHECK(r.c.screen() == SCR_MONITOR);
  r.key('0'); CHECK(r.c.screen() == SCR_CALIBRATE);
  r.volts = 1.80f;                                 // this board reads 1.80 V in pH 7
  r.run(2000);
  r.key('#');                                      // too early: not enough stable samples
  CHECK(r.line_is(0, "CAL: put in pH7 "));
  r.run(7000);
  CHECK(std::strstr(r.out.lcd[1], "#=OK") != nullptr);
  r.key('#');
  CHECK(r.line_is(0, "CAL: put in pH4 "));
  r.volts = 1.00f;
  r.run(8000);
  r.saved = false;
  r.key('#');
  CHECK(r.saved);
  CHECK(r.line_is(0, "CAL OK slope107%"));
  NEAR(r.c.settings().cal.v7, 1.80f, 0.002f);
  NEAR(r.c.settings().cal.v4, 1.00f, 0.002f);
  r.run(5000);
  CHECK(r.c.screen() == SCR_MONITOR);
  // an implausible pair is refused and the old calibration kept
  r.key('0'); r.volts = 1.5f; r.run(8000); r.key('#');
  r.volts = 1.45f; r.run(8000); r.key('#');
  CHECK(r.line_is(0, "CAL REJECTED    "));
  NEAR(r.c.settings().cal.v7, 1.80f, 0.002f);
}

static void test_calibration_blocks_dosing() {
  std::puts("no dosing while the probe is in a buffer");
  Rig r;
  r.run(6000);
  r.key('C'); r.key('0');
  r.set_ph(7.0f);                                  // pH 7 buffer is "high"
  r.run(60000);
  CHECK(r.c.dose_state() == DOSE_IDLE);
  CHECK(!r.out.on[CH_PH_DOWN]);
}

static void test_clock() {
  std::puts("clock setting");
  Rig r;
  r.run(3000);
  r.key('C'); r.key('*');
  CHECK(r.c.screen() == SCR_SET_CLOCK);
  r.key('2'); r.key('2'); r.key('3'); r.key('0');
  CHECK(r.line_is(1, "  22:30  #OK *X "));
  r.key('#');
  CHECK(r.c.clock_s(r.now) / 60 == 22 * 60 + 30);
  r.run(1000);
  CHECK(!r.out.on[CH_GROW_LIGHT]);
  r.key('*');
  r.key('2'); r.key('5'); r.key('0'); r.key('0'); r.key('#');   // 25:00 is refused
  CHECK(r.c.screen() == SCR_SET_CLOCK);
}

static void test_climate() {
  std::puts("fan / humidifier hysteresis, DHT11 fault");
  Rig r;
  r.run(3000);
  CHECK(!r.out.on[CH_FAN]);
  r.temp = 31; r.run(3000); CHECK(r.out.on[CH_FAN]);
  r.temp = 29; r.run(3000); CHECK(r.out.on[CH_FAN]);      // hysteresis
  r.temp = 27; r.run(3000); CHECK(!r.out.on[CH_FAN]);
  r.rh = 40; r.run(3000); CHECK(r.out.on[CH_HUMIDIFIER]);
  r.rh = 50; r.run(3000); CHECK(r.out.on[CH_HUMIDIFIER]);
  r.rh = 56; r.run(3000); CHECK(!r.out.on[CH_HUMIDIFIER]);
  r.dht_ok = false; r.run(35000);
  CHECK(r.c.alarms() & ALM_CLIMATE);
  CHECK(r.out.on[CH_FAN] && !r.out.on[CH_HUMIDIFIER]);
}

static void test_millis_wrap() {
  std::puts("millis() wrap-around");
  Rig r(0xFFFFFFFFu - 20000u);
  r.set_ph(6.6f);
  r.run(40000);                                    // crosses the wrap while dosing starts
  CHECK(r.c.dose_state() == DOSE_PUMPING || r.c.dose_state() == DOSE_MIXING);
  NEAR((float)(r.c.clock_s(r.now)), 6.0f * 3600.0f + 40.0f, 2.0f);
  r.run(200000);
  CHECK(r.c.dose_state() != DOSE_LOCKED);
}

static void test_invariants_fuzz() {
  std::puts("randomised invariants");
  std::srand(7);
  const char keys[] = "0123456789ABCD*#";
  for (int run = 0; run < 40; ++run) {
    Rig r((uint32_t)std::rand() * 4096u);
    for (int i = 0; i < 20000; ++i) {
      if (std::rand() % 40 == 0) r.level = std::rand() % 100;
      if (std::rand() % 50 == 0) r.set_ph(4.0f + (float)(std::rand() % 500) / 100.0f);
      if (std::rand() % 200 == 0) r.dht_ok = !r.dht_ok;
      char k = std::rand() % 25 == 0 ? keys[std::rand() % 16] : 0;
      r.tick(k);
      r.now += 20 + std::rand() % 400;
      for (int row = 0; row < 2; ++row) {
        CHECK(std::strlen(r.out.lcd[row]) == 16);
        for (int c = 0; c < 16; ++c) CHECK(r.out.lcd[row][c] >= 32 && r.out.lcd[row][c] < 127);
      }
      if (r.c.alarms() & ALM_LOW_WATER) {
        CHECK(!r.out.on[CH_PUMP]);
        CHECK(!r.out.on[CH_PH_DOWN]);
      }
      if (r.c.program() != PROG_MANUAL) CHECK(!r.out.on[CH_NUTRIENT]);
      if (r.c.screen() == SCR_CALIBRATE) CHECK(!r.out.on[CH_PH_DOWN]);
      CHECK(!(r.out.on[CH_FAN] && r.out.on[CH_HUMIDIFIER]));
      CHECK(r.c.dose_ms_today() <= (uint32_t)r.c.settings().dose_budget_ms_day + 500u);
    }
  }
}

int main() {
  test_conversion();
  test_boot_and_lights();
  test_low_water();
  test_dosing();
  test_no_response_lockout();
  test_budget_and_midnight();
  test_probe_fault();
  test_implausible_reading();
  test_retry_doses_are_small();
  test_manual();
  test_calibration_wizard();
  test_calibration_blocks_dosing();
  test_clock();
  test_climate();
  test_millis_wrap();
  test_invariants_fuzz();
  std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
