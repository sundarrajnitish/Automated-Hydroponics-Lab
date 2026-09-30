// hydro_core.cpp - see hydro_core.h. Author: Nitish Sundarraj
#include "hydro_core.h"

namespace hydro {

// ---------------------------------------------------------------- settings

Calibration default_calibration() {
  Calibration c;
  c.v7 = 2.0f;               // 7 / 3.5
  c.v4 = 4.0f / 3.5f;        // 4 / 3.5
  return c;
}

Settings default_settings() {
  Settings s;
  s.ph_target = 6.0f;
  s.ph_start = 6.3f;
  s.ph_low_alarm = 5.2f;
  s.gain_initial = 0.15f;    // deliberately high: the first dose comes out small
  s.dose_min_ms = 200;
  s.dose_max_ms = 3000;
  s.mix_ms = 150000u;
  s.max_unanswered = 3;
  s.dose_budget_ms_day = 40000;   // 800 mL of pH-down at 20 mL/s
  s.lights_on_hour = 6;
  s.light_hours = 16;
  s.eco_light_hours = 12;
  s.pump_on_min = 15;
  s.pump_off_min_day = 15;
  s.pump_off_min_night = 45;
  s.eco_pump_on_min = 10;
  s.eco_pump_off_min_day = 20;
  s.eco_pump_off_min_night = 50;
  s.fan_on_c = 30;
  s.fan_off_c = 28;
  s.fan_on_rh = 80;
  s.fan_off_rh = 72;
  s.hum_on_rh = 45;
  s.hum_off_rh = 55;
  s.level_low_pct = 25;
  s.level_ok_pct = 32;
  s.manual_dose_limit_ms = 10000;
  s.cal = default_calibration();
  return s;
}

float volts_to_ph(float volts, const Calibration& c) {
  return 7.0f + (volts - c.v7) * (3.0f / (c.v7 - c.v4));
}

bool calibration_plausible(const Calibration& c) {
  float span = c.v7 - c.v4;
  if (!(span > 0.0f)) return false;
  float slope = 3.0f / span;                    // pH per volt
  if (slope < 3.5f * 0.7f || slope > 3.5f * 1.3f) return false;
  return c.v7 > 0.5f && c.v7 < 4.5f;
}

// ----------------------------------------------------------- small helpers

static void fill(char* line, char c) {
  for (int i = 0; i < 16; ++i) line[i] = c;
  line[16] = 0;
}

static int put(char* line, int col, const char* s) {
  while (*s && col < 16) line[col++] = *s++;
  return col;
}

static int put_uint(char* line, int col, uint32_t v, int width, char pad = ' ') {
  char buf[10];
  int n = 0;
  do { buf[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
  for (int i = n; i < width && col < 16; ++i) line[col++] = pad;
  while (n && col < 16) line[col++] = buf[--n];
  return col;
}

static int put_int(char* line, int col, int32_t v, int width) {
  if (v < 0) {
    uint32_t m = (uint32_t)(-v);
    int digits = 1;
    for (uint32_t t = m; t >= 10; t /= 10) ++digits;
    for (int i = digits + 1; i < width && col < 16; ++i) line[col++] = ' ';
    if (col < 16) line[col++] = '-';
    return put_uint(line, col, m, 0);
  }
  return put_uint(line, col, (uint32_t)v, width);
}

// fixed-point print, e.g. put_fixed(l, c, 6.018, 2) -> "6.02"
static int put_fixed(char* line, int col, float v, int decimals) {
  if (v < 0) { if (col < 16) line[col++] = '-'; v = -v; }
  uint32_t scale = 1;
  for (int i = 0; i < decimals; ++i) scale *= 10;
  uint32_t fixed = (uint32_t)(v * (float)scale + 0.5f);
  col = put_uint(line, col, fixed / scale, 1);
  if (decimals) {
    if (col < 16) line[col++] = '.';
    col = put_uint(line, col, fixed % scale, decimals, '0');
  }
  return col;
}

static int put_ph(char* line, int col, bool ok, float ph) {
  if (!ok) return put(line, col, "----");
  if (ph >= 9.995f) return put_fixed(line, col, ph, 1);   // "10.4"
  return put_fixed(line, col, ph, 2);                      // "6.02"
}

static int put_hhmm(char* line, int col, uint32_t tod) {
  col = put_uint(line, col, tod / 3600, 2, '0');
  line[col++] = ':';
  return put_uint(line, col, (tod / 60) % 60, 2, '0');
}

// ------------------------------------------------------------------ begin

void Controller::begin(const Settings& s, Program p, uint32_t now) {
  s_ = s;
  if (!calibration_plausible(s_.cal)) s_.cal = default_calibration();
  prog_ = p > PROG_ECONOMY ? PROG_AUTO : p;
  scr_ = SCR_SPLASH;
  page_ = 0;
  boot_ms_ = now;
  splash_until_ = now + 2000;
  save_ = false;

  clock_base_s_ = (uint32_t)s_.lights_on_hour * 3600u;  // no RTC: boot = lights-on
  clock_anchor_ms_ = now;
  last_tod_ = clock_base_s_;

  ph_n_ = 0; ph_count_ = 0; ph_last_ms_ = now; settle_until_ = now + 30000u;  // electrode warm-up
  ph_ = 0; volts_ = 0; ph_ok_ = false; probe_fault_ = false;

  temp_ = 0; rh_ = 0; climate_ok_ = false; climate_last_ms_ = now;
  fan_on_ = false; hum_on_ = false;

  level_ = 0; level_seen_ = false; level_low_ = true; level_last_ms_ = now;

  dose_ = DOSE_IDLE;
  dose_start_ = dose_len_ = mix_start_ = 0;
  ph_before_ = 0; gain_ = s_.gain_initial;
  unanswered_ = 0;
  correcting_ = false; dose_eval_valid_ = false; budget_hit_ = false;
  dose_ms_today_ = 0; doses_today_ = 0;

  for (int i = 0; i < CH_COUNT; ++i) { manual_[i] = false; manual_since_[i] = now; last_on_[i] = false; }

  cal_step_ = 0; cal_v7_ = 0; cal_n_ = 0; cal_ok_ = false; cal_result_until_ = 0; cal_slope_pct_ = 0;
  ndig_ = 0;
  alarms_ = 0;
}

// ------------------------------------------------------------------ clock

uint32_t Controller::clock_s(uint32_t now) const {
  return (clock_base_s_ + (now - clock_anchor_ms_) / 1000u) % 86400u;
}

bool Controller::lights_period(uint32_t now) const {
  uint32_t hours = prog_ == PROG_ECONOMY ? s_.eco_light_hours : s_.light_hours;
  uint32_t since_on = (clock_s(now) + 86400u - (uint32_t)s_.lights_on_hour * 3600u) % 86400u;
  return since_on < hours * 3600u;
}

// ------------------------------------------------------------------- step

void Controller::step(const Inputs& in, Outputs& out) {
  const uint32_t now = in.now_ms;

  // Re-anchor the software clock every hour so millis() wrap-around
  // (49.7 days) never matters.
  uint32_t elapsed = now - clock_anchor_ms_;
  if (elapsed >= 3600000u) {
    uint32_t whole = elapsed / 1000u;
    clock_base_s_ = (clock_base_s_ + whole) % 86400u;
    clock_anchor_ms_ += whole * 1000u;
  }
  uint32_t tod = clock_s(now);
  if (tod < last_tod_) {           // midnight: new daily allowance
    dose_ms_today_ = 0;
    doses_today_ = 0;
    budget_hit_ = false;
  }
  last_tod_ = tod;

  update_sensors(in);
  if (in.key) handle_key(in.key, now);
  if (scr_ == SCR_SPLASH && (int32_t)(now - splash_until_) >= 0) scr_ = SCR_HOME;
  if (scr_ == SCR_CALIBRATE && cal_step_ == 2 && (int32_t)(now - cal_result_until_) >= 0) {
    scr_ = SCR_MONITOR;
    page_ = 3;
  }
  update_dosing(now);

  // alarms
  uint16_t a = 0;
  if (level_low_) a |= ALM_LOW_WATER;
  if (probe_fault_ || (!ph_ok_ && now - boot_ms_ > 5000u)) a |= ALM_PH_PROBE;
  if (dose_ == DOSE_LOCKED) a |= ALM_NO_RESPONSE;
  if (ph_ok_ && ph_ < s_.ph_low_alarm && scr_ != SCR_CALIBRATE && (int32_t)(now - settle_until_) >= 0)
    a |= ALM_PH_LOW;
  if (budget_hit_) a |= ALM_DOSE_BUDGET;
  if (!climate_ok_ && now - boot_ms_ > 30000u) a |= ALM_CLIMATE;
  alarms_ = a;

  compute_outputs(now, out);
  for (int i = 0; i < CH_COUNT; ++i) last_on_[i] = out.on[i];
  render(now, out, out.lcd);
  out.save = save_;
  save_ = false;
}

// --------------------------------------------------------------- sensors

void Controller::update_sensors(const Inputs& in) {
  const uint32_t now = in.now_ms;
  if (in.ph_ready) {
    ph_last_ms_ = now;
    volts_ = in.ph_volts;
    // A disconnected electrode or a shorted amplifier pins the ADC to a rail.
    bool rail = in.ph_adc_min <= 3 || in.ph_adc_max >= 1020;
    float p = volts_to_ph(in.ph_volts, s_.cal);
    // Outside a buffer, a nutrient tank open to the air cannot sit above
    // ~pH 9 (dissolved CO2 holds it down): a reading up there is a sick
    // electrode or amplifier, and dosing on it would empty the acid bottle.
    bool implausible = scr_ != SCR_CALIBRATE && p > 9.0f;
    if (rail || implausible || p < 0.5f || p > 13.5f) {
      probe_fault_ = true;
      ph_ok_ = false;
      ph_n_ = 0;
    } else {
      probe_fault_ = false;
      // median of the last five one-second readings rejects single spikes
      if (ph_n_ < 5) ph_hist_[ph_n_++] = p;
      else { for (int i = 0; i < 4; ++i) ph_hist_[i] = ph_hist_[i + 1]; ph_hist_[4] = p; }
      float sorted[5];
      for (int i = 0; i < ph_n_; ++i) sorted[i] = ph_hist_[i];
      for (int i = 1; i < ph_n_; ++i)
        for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; --j) { float t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t; }
      ph_ = sorted[ph_n_ / 2];
      ph_ok_ = true;
      ++ph_count_;
    }
    if (scr_ == SCR_CALIBRATE && cal_step_ < 2) {
      if (cal_n_ < 6) cal_hist_[cal_n_++] = in.ph_volts;
      else { for (int i = 0; i < 5; ++i) cal_hist_[i] = cal_hist_[i + 1]; cal_hist_[5] = in.ph_volts; }
    }
  } else if (now - ph_last_ms_ > 5000u) {
    ph_ok_ = false;   // the sampler went quiet
  }

  if (in.dht_ready && in.dht_ok) {
    temp_ = in.temp_c;
    rh_ = in.rh;
    climate_ok_ = true;
    climate_last_ms_ = now;
  } else if (now - climate_last_ms_ > 30000u) {
    climate_ok_ = false;
  }

  if (in.level_ready) {
    level_ = in.level_pct;
    level_seen_ = true;
    level_last_ms_ = now;
    if (level_ <= s_.level_low_pct) level_low_ = true;
    else if (level_ >= s_.level_ok_pct) level_low_ = false;
  } else if (!level_seen_ || now - level_last_ms_ > 10000u) {
    level_low_ = true;  // unknown level is treated as low: never run the pump dry
  }
}

// ------------------------------------------------------------------- keys

void Controller::set_program(Program p) {
  if (p != prog_) save_ = true;
  if (p == PROG_MANUAL && prog_ != PROG_MANUAL) {
    // bumpless: keep pump/lights/fan as they are, never inherit a dosing pump
    for (int i = 0; i < CH_COUNT; ++i) manual_[i] = last_on_[i];
    manual_[CH_PH_DOWN] = manual_[CH_NUTRIENT] = manual_[CH_HUMIDIFIER] = false;
  }
  if (p != PROG_MANUAL) {
    // selecting an automatic program acknowledges a dosing lock-out
    if (dose_ == DOSE_LOCKED) { dose_ = DOSE_IDLE; gain_ = s_.gain_initial; }
    unanswered_ = 0;
  }
  prog_ = p;
  scr_ = SCR_HOME;
}

void Controller::handle_key(char k, uint32_t now) {
  if (scr_ == SCR_SPLASH) scr_ = SCR_HOME;

  if (scr_ == SCR_CALIBRATE) {
    if (k == '*') { scr_ = SCR_MONITOR; page_ = 3; settle_until_ = now + 30000u; ph_n_ = 0; }
    else if (k == '#' && cal_step_ < 2) {
      bool stable = false;
      if (cal_n_ >= 6 && !probe_fault_) {
        float lo = cal_hist_[0], hi = cal_hist_[0];
        for (int i = 1; i < 6; ++i) { if (cal_hist_[i] < lo) lo = cal_hist_[i]; if (cal_hist_[i] > hi) hi = cal_hist_[i]; }
        stable = (hi - lo) < 0.010f;
      }
      if (!stable) return;
      float v = 0;
      for (int i = 0; i < 6; ++i) v += cal_hist_[i];
      v /= 6.0f;
      if (cal_step_ == 0) { cal_v7_ = v; cal_step_ = 1; cal_n_ = 0; }
      else {
        Calibration c;
        c.v7 = cal_v7_;
        c.v4 = v;
        cal_ok_ = calibration_plausible(c);
        if (cal_ok_) {
          s_.cal = c;
          save_ = true;
          float slope = 3.0f / (c.v7 - c.v4);
          cal_slope_pct_ = (uint8_t)(slope / 3.5f * 100.0f + 0.5f);
        }
        cal_step_ = 2;
        cal_result_until_ = now + 4000u;
        settle_until_ = now + 30000u + 4000u;
        ph_n_ = 0;
      }
    }
    return;
  }

  if (scr_ == SCR_SET_CLOCK) {
    if (k >= '0' && k <= '9' && ndig_ < 4) digits_[ndig_++] = k;
    else if (k == '*') { scr_ = SCR_MONITOR; }
    else if (k == '#' && ndig_ == 4) {
      uint32_t hh = (uint32_t)(digits_[0] - '0') * 10 + (uint32_t)(digits_[1] - '0');
      uint32_t mm = (uint32_t)(digits_[2] - '0') * 10 + (uint32_t)(digits_[3] - '0');
      if (hh < 24 && mm < 60) {
        clock_base_s_ = hh * 3600u + mm * 60u;
        clock_anchor_ms_ = now;
        last_tod_ = clock_base_s_;
        scr_ = SCR_MONITOR;
        page_ = 1;
      } else {
        ndig_ = 0;
      }
    }
    return;
  }

  switch (k) {
    case 'A': set_program(PROG_AUTO); return;
    case 'B': set_program(PROG_MANUAL); return;
    case 'D': set_program(PROG_ECONOMY); return;
    case 'C':
      if (scr_ == SCR_MONITOR) scr_ = SCR_HOME;
      else { scr_ = SCR_MONITOR; page_ = 0; }
      return;
    default: break;
  }

  if (scr_ == SCR_MONITOR) {
    if (k == '#') page_ = (uint8_t)((page_ + 1) % 4);
    else if (k == '*') { scr_ = SCR_SET_CLOCK; ndig_ = 0; }
    else if (k == '0') { scr_ = SCR_CALIBRATE; cal_step_ = 0; cal_n_ = 0; }
    return;
  }

  if (scr_ == SCR_HOME && prog_ == PROG_MANUAL) {
    // Top key switches a load on, the key below it switches it off.
    struct Map { char key; uint8_t ch; bool on; };
    static const Map MAP[] = {
      {'1', CH_PUMP, true},       {'4', CH_PUMP, false},
      {'2', CH_NUTRIENT, true},   {'5', CH_NUTRIENT, false},
      {'3', CH_PH_DOWN, true},    {'6', CH_PH_DOWN, false},
      {'7', CH_FAN, true},        {'*', CH_FAN, false},
      {'8', CH_FLUO_LIGHT, true}, {'0', CH_FLUO_LIGHT, false},
      {'9', CH_GROW_LIGHT, true}, {'#', CH_GROW_LIGHT, false},
    };
    for (unsigned i = 0; i < sizeof(MAP) / sizeof(MAP[0]); ++i) {
      if (MAP[i].key == k) {
        manual_[MAP[i].ch] = MAP[i].on;
        manual_since_[MAP[i].ch] = now;
      }
    }
  }
}

// ---------------------------------------------------------------- dosing

bool Controller::dosing_allowed(uint32_t now) const {
  return prog_ != PROG_MANUAL && scr_ != SCR_CALIBRATE && ph_ok_ && !level_low_ &&
         !budget_hit_ && ph_count_ >= 5 && (int32_t)(now - settle_until_) >= 0;
}

void Controller::update_dosing(uint32_t now) {
  // a dose judged while the probe is in a buffer, or while someone is
  // switching pumps by hand, would teach the controller nonsense
  if (scr_ == SCR_CALIBRATE || prog_ == PROG_MANUAL) dose_eval_valid_ = false;

  switch (dose_) {
    case DOSE_IDLE: {
      if (!dosing_allowed(now)) { correcting_ = false; break; }
      float trigger = correcting_ ? s_.ph_target + 0.05f : s_.ph_start;
      if (!(ph_ > trigger)) { correcting_ = false; break; }
      correcting_ = true;
      // Size the dose from the learned response, aiming 20 % short so a
      // wrong estimate undershoots instead of overshooting.
      float ms = (ph_ - s_.ph_target) / gain_ * 1000.0f * 0.8f;
      uint32_t len = ms < (float)s_.dose_min_ms ? s_.dose_min_ms
                   : ms > (float)s_.dose_max_ms ? s_.dose_max_ms : (uint32_t)ms;
      // after a dose that changed nothing, only probe with the smallest
      // useful pulse until the tank answers again
      if (unanswered_ > 0 && len > 2u * s_.dose_min_ms) len = 2u * s_.dose_min_ms;
      uint32_t left = s_.dose_budget_ms_day > dose_ms_today_ ? s_.dose_budget_ms_day - dose_ms_today_ : 0;
      if (len > left) len = left;
      if (len < s_.dose_min_ms) { budget_hit_ = true; correcting_ = false; break; }
      dose_len_ = len;
      dose_start_ = now;
      ph_before_ = ph_;
      dose_eval_valid_ = true;
      dose_ = DOSE_PUMPING;
      break;
    }
    case DOSE_PUMPING: {
      uint32_t ran = now - dose_start_;
      bool abort = prog_ == PROG_MANUAL || scr_ == SCR_CALIBRATE || level_low_;
      if (ran >= dose_len_ || abort) {
        if (ran > dose_len_) ran = dose_len_;
        dose_ms_today_ += ran;
        ++doses_today_;
        dose_len_ = ran;
        mix_start_ = now;
        dose_ = DOSE_MIXING;
      }
      break;
    }
    case DOSE_MIXING: {
      if (now - mix_start_ < s_.mix_ms) break;
      if (dose_eval_valid_ && ph_ok_ && dose_len_ > 0) {
        float drop = ph_before_ - ph_;
        float secs = (float)dose_len_ / 1000.0f;
        float expected = gain_ * secs;
        float needed = expected * 0.25f;
        if (needed < 0.02f) needed = 0.02f;
        if (drop < needed) {
          ++unanswered_;   // the pH did not move: empty bottle, dead pump or stuck probe
        } else {
          unanswered_ = 0;
          float g = drop / secs;
          gain_ = 0.5f * gain_ + 0.5f * g;
          if (gain_ < 0.002f) gain_ = 0.002f;
          if (gain_ > 1.0f) gain_ = 1.0f;
        }
      }
      dose_ = unanswered_ >= s_.max_unanswered ? DOSE_LOCKED : DOSE_IDLE;
      break;
    }
    case DOSE_LOCKED:
      break;
  }
}

// --------------------------------------------------------------- outputs

void Controller::compute_outputs(uint32_t now, Outputs& out) {
  for (int i = 0; i < CH_COUNT; ++i) out.on[i] = false;
  out.save = false;

  if (prog_ == PROG_MANUAL) {
    for (int i = 0; i < CH_COUNT; ++i) out.on[i] = manual_[i];
    // a dosing pump left on by hand switches itself off
    const uint8_t doses[2] = {CH_PH_DOWN, CH_NUTRIENT};
    for (int j = 0; j < 2; ++j) {
      uint8_t c = doses[j];
      if (manual_[c] && now - manual_since_[c] >= s_.manual_dose_limit_ms) manual_[c] = out.on[c] = false;
    }
    out.on[CH_HUMIDIFIER] = false;
    if (level_low_) out.on[CH_PUMP] = out.on[CH_PH_DOWN] = out.on[CH_NUTRIENT] = false;
    if (scr_ == SCR_CALIBRATE) out.on[CH_PH_DOWN] = out.on[CH_NUTRIENT] = false;
    return;
  }

  bool eco = prog_ == PROG_ECONOMY;
  bool day = lights_period(now);
  out.on[CH_GROW_LIGHT] = day;
  out.on[CH_FLUO_LIGHT] = day && !eco;

  uint32_t on_min = eco ? s_.eco_pump_on_min : s_.pump_on_min;
  uint32_t off_min = eco ? (day ? s_.eco_pump_off_min_day : s_.eco_pump_off_min_night)
                         : (day ? s_.pump_off_min_day : s_.pump_off_min_night);
  uint32_t minute = clock_s(now) / 60u;
  bool scheduled = (minute % (on_min + off_min)) < on_min;
  bool dosing = dose_ == DOSE_PUMPING || dose_ == DOSE_MIXING;  // circulate to mix
  out.on[CH_PUMP] = (scheduled || dosing) && !level_low_;
  out.on[CH_PH_DOWN] = dose_ == DOSE_PUMPING && !level_low_;

  if (climate_ok_) {
    int8_t on_c = (int8_t)(s_.fan_on_c + (eco ? 2 : 0));
    int8_t off_c = (int8_t)(s_.fan_off_c + (eco ? 2 : 0));
    if (temp_ >= on_c || rh_ >= s_.fan_on_rh) fan_on_ = true;
    else if (temp_ <= off_c && rh_ <= s_.fan_off_rh) fan_on_ = false;
    if (fan_on_) hum_on_ = false;
    else if (rh_ <= s_.hum_on_rh) hum_on_ = true;
    else if (rh_ >= s_.hum_off_rh) hum_on_ = false;
  } else {
    fan_on_ = true;    // blind: ventilate, never humidify
    hum_on_ = false;
  }
  out.on[CH_FAN] = fan_on_;
  out.on[CH_HUMIDIFIER] = hum_on_;
}

// ---------------------------------------------------------------- display

static const char* alarm_text(uint16_t a) {
  if (a & ALM_LOW_WATER) return "!LOW WATER      ";
  if (a & ALM_PH_PROBE) return "!PH PROBE FAULT ";
  if (a & ALM_NO_RESPONSE) return "!DOSING HALTED  ";
  if (a & ALM_PH_LOW) return "!PH TOO LOW     ";
  if (a & ALM_DOSE_BUDGET) return "!ACID LIMIT/DAY ";
  if (a & ALM_CLIMATE) return "!DHT11 FAULT    ";
  return "                ";
}

void Controller::render(uint32_t now, const Outputs& out, char lcd[2][17]) {
  char* l0 = lcd[0];
  char* l1 = lcd[1];
  fill(l0, ' ');
  fill(l1, ' ');
  uint32_t tod = clock_s(now);
  bool show_alarm = alarms_ && ((now / 2000u) & 1u);

  switch (scr_) {
    case SCR_SPLASH:
      put(l0, 0, " HYDROPONINATOR ");
      put(l1, 0, "  N. Sundarraj  ");
      return;

    case SCR_HOME: {
      put(l0, 0, prog_ == PROG_AUTO ? "AUT" : prog_ == PROG_ECONOMY ? "ECO" : "MAN");
      put(l0, 4, "pH");
      put_ph(l0, 6, ph_ok_, ph_);
      put_hhmm(l0, 11, tod);
      if (show_alarm) { put(l1, 0, alarm_text(alarms_)); return; }
      if (prog_ == PROG_MANUAL) {
        static const char LETTER[6] = {'P', 'N', 'A', 'F', 'L', 'G'};
        static const uint8_t CH[6] = {CH_PUMP, CH_NUTRIENT, CH_PH_DOWN, CH_FAN, CH_FLUO_LIGHT, CH_GROW_LIGHT};
        for (int i = 0; i < 6; ++i) {
          l1[i * 2] = LETTER[i];
          l1[i * 2 + 1] = out.on[CH[i]] ? '+' : '-';
        }
        return;
      }
      if (climate_ok_) { put_int(l1, 0, temp_, 2); } else { put(l1, 0, "--"); }
      l1[2] = 'C';
      if (climate_ok_) { put_uint(l1, 4, rh_, 2); } else { put(l1, 4, "--"); }
      l1[6] = '%';
      l1[8] = 'W';
      put_uint(l1, 9, level_, 3);
      l1[12] = '%';
      l1[14] = dose_ == DOSE_PUMPING ? 'D' : dose_ == DOSE_MIXING ? 'M' : dose_ == DOSE_LOCKED ? 'X' : ' ';
      l1[15] = out.on[CH_PUMP] ? 'P' : '.';
      return;
    }

    case SCR_MONITOR:
      if (page_ == 0) {
        put(l0, 0, "pH ");
        put_ph(l0, 3, ph_ok_, ph_);
        put(l0, 8, "V ");
        put_fixed(l0, 10, volts_, 3);
        l1[0] = 'T';
        if (climate_ok_) put_int(l1, 1, temp_, 2); else put(l1, 1, "--");
        l1[3] = 'C';
        put(l1, 5, "RH");
        if (climate_ok_) put_uint(l1, 7, rh_, 2); else put(l1, 7, "--");
        l1[9] = '%';
        l1[11] = 'W';
        put_uint(l1, 12, level_, 3);
        l1[15] = '%';
      } else if (page_ == 1) {
        int c = put(l0, 0, "Clock ");
        c = put_hhmm(l0, c, tod);
        l0[c++] = ':';
        put_uint(l0, c, tod % 60, 2, '0');
        bool lit = lights_period(now);
        put(l1, 0, lit ? "Light ON" : "Light OFF");
        uint32_t hours = prog_ == PROG_ECONOMY ? s_.eco_light_hours : s_.light_hours;
        c = put_uint(l1, 10, s_.lights_on_hour, 2, '0');
        l1[c++] = '-';
        put_uint(l1, c, (s_.lights_on_hour + hours) % 24, 2, '0');
      } else if (page_ == 2) {
        int c = put(l0, 0, "Acid ");
        float secs = (float)dose_ms_today_ / 1000.0f;
        if (secs < 10.0f) l0[c++] = ' ';
        c = put_fixed(l0, c, secs, 1);
        c = put(l0, c, "s n=");
        put_uint(l0, c, doses_today_, 1);
        c = put(l1, 0, "Gain ");
        c = put_fixed(l1, c, gain_, 3);
        put(l1, c, " pH/s");
      } else {
        int c = put(l0, 0, "Cal V7=");
        put_fixed(l0, c, s_.cal.v7, 3);
        c = put(l1, 0, "slope ");
        c = put_fixed(l1, c, 3.0f / (s_.cal.v7 - s_.cal.v4), 2);
        put(l1, c, " pH/V");
      }
      return;

    case SCR_CALIBRATE: {
      if (cal_step_ == 2) {
        if (cal_ok_) {
          int c = put(l0, 0, "CAL OK slope");
          c = put_uint(l0, c, cal_slope_pct_, 3);
          if (c < 16) l0[c] = '%';
          put(l1, 0, "probe -> tank");
        } else {
          put(l0, 0, "CAL REJECTED");
          put(l1, 0, "check buffers");
        }
        return;
      }
      put(l0, 0, cal_step_ == 0 ? "CAL: put in pH7" : "CAL: put in pH4");
      if (probe_fault_) { put(l1, 0, "V ----- check"); return; }
      l1[0] = 'V';
      put_fixed(l1, 1, volts_, 3);
      bool stable = false;
      if (cal_n_ >= 6) {
        float lo = cal_hist_[0], hi = cal_hist_[0];
        for (int i = 1; i < 6; ++i) { if (cal_hist_[i] < lo) lo = cal_hist_[i]; if (cal_hist_[i] > hi) hi = cal_hist_[i]; }
        stable = (hi - lo) < 0.010f;
      }
      put(l1, 7, stable ? "#=OK *=X" : "settling");
      return;
    }

    case SCR_SET_CLOCK: {
      put(l0, 0, "SET CLOCK  HH:MM");
      char d[5] = {'_', '_', ':', '_', '_'};
      for (int i = 0; i < ndig_; ++i) d[i < 2 ? i : i + 1] = digits_[i];
      for (int i = 0; i < 5; ++i) l1[2 + i] = d[i];
      put(l1, 9, "#OK *X");
      return;
    }
  }
}

}  // namespace hydro
