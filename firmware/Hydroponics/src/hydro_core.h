// hydro_core.h - hardware-independent control logic for the Hydroponinator.
//
// Nothing in here touches a pin, a timer or a library: the sketch samples the
// sensors, hands the readings to Controller::step() together with millis()
// and the last key press, and applies the outputs it gets back. That keeps
// the logic unit-testable on a PC and lets the exact same code run in the
// browser simulator (compiled to WebAssembly).
//
// Author: Nitish Sundarraj
#pragma once
#include <stdint.h>

namespace hydro {

enum Channel : uint8_t {
  CH_PUMP,        // 40 W circulation pump (relay)
  CH_PH_DOWN,     // pH-down dosing pump (transistor)
  CH_NUTRIENT,    // nutrient dosing pump (transistor)
  CH_GROW_LIGHT,  // full-spectrum grow light (relay)
  CH_FLUO_LIGHT,  // supplemental fluorescent tubes (relay)
  CH_FAN,         // ventilation fan (relay)
  CH_HUMIDIFIER,  // humidifier (transistor)
  CH_COUNT
};

enum Program : uint8_t { PROG_AUTO = 0, PROG_MANUAL = 1, PROG_ECONOMY = 2 };
enum Screen : uint8_t { SCR_SPLASH, SCR_HOME, SCR_MONITOR, SCR_CALIBRATE, SCR_SET_CLOCK };
enum DoseState : uint8_t { DOSE_IDLE, DOSE_PUMPING, DOSE_MIXING, DOSE_LOCKED };

enum Alarm : uint16_t {
  ALM_LOW_WATER = 1u << 0,     // reservoir below the pump intake margin
  ALM_PH_PROBE = 1u << 1,      // pH reading missing, at an ADC rail or absurd
  ALM_NO_RESPONSE = 1u << 2,   // doses did not move the pH: probe or pump fault
  ALM_PH_LOW = 1u << 3,        // below the low limit (no pH-up pump fitted)
  ALM_DOSE_BUDGET = 1u << 4,   // daily pH-down allowance used up
  ALM_CLIMATE = 1u << 5,       // no valid DHT11 reading for 30 s
};

// Two-point calibration: amplifier output (volts) in pH 7.00 and 4.00 buffers.
struct Calibration {
  float v7;
  float v4;
};

struct Settings {
  float ph_target;          // a correction doses down to this pH
  float ph_start;           // ...and starts when the pH rises above this
  float ph_low_alarm;
  float gain_initial;       // pH drop per second of pH-down pump (first guess)
  uint16_t dose_min_ms, dose_max_ms;
  uint32_t mix_ms;          // circulate this long before judging a dose
  uint8_t max_unanswered;   // doses with no response before dosing locks out
  uint16_t dose_budget_ms_day;
  uint8_t lights_on_hour, light_hours, eco_light_hours;
  uint8_t pump_on_min, pump_off_min_day, pump_off_min_night;
  uint8_t eco_pump_on_min, eco_pump_off_min_day, eco_pump_off_min_night;
  int8_t fan_on_c, fan_off_c;
  uint8_t fan_on_rh, fan_off_rh, hum_on_rh, hum_off_rh;
  uint8_t level_low_pct, level_ok_pct;
  uint16_t manual_dose_limit_ms;
  Calibration cal;
};

Settings default_settings();
Calibration default_calibration();  // DFRobot-style board: pH = 3.5 * V
float volts_to_ph(float volts, const Calibration& c);
bool calibration_plausible(const Calibration& c);

struct Inputs {
  uint32_t now_ms;
  char key;                 // 0 when no key was pressed
  bool ph_ready;            // a new averaged pH-board voltage is available
  float ph_volts;
  uint16_t ph_adc_min, ph_adc_max;  // extremes of the raw samples behind it
  bool dht_ready;           // a DHT11 read was attempted
  bool dht_ok;
  int8_t temp_c;
  uint8_t rh;
  bool level_ready;
  uint8_t level_pct;
};

struct Outputs {
  bool on[CH_COUNT];
  char lcd[2][17];
  bool save;                // program or calibration changed: persist it
};

class Controller {
 public:
  void begin(const Settings& s, Program p, uint32_t now_ms);
  void step(const Inputs& in, Outputs& out);

  Program program() const { return prog_; }
  Screen screen() const { return scr_; }
  DoseState dose_state() const { return dose_; }
  uint16_t alarms() const { return alarms_; }
  bool ph_valid() const { return ph_ok_; }
  float ph() const { return ph_; }
  float volts() const { return volts_; }
  float gain() const { return gain_; }
  uint32_t dose_ms_today() const { return dose_ms_today_; }
  uint16_t doses_today() const { return doses_today_; }
  uint8_t unanswered() const { return unanswered_; }
  int8_t temp_c() const { return temp_; }
  uint8_t rh() const { return rh_; }
  uint8_t level_pct() const { return level_; }
  const Settings& settings() const { return s_; }
  uint32_t clock_s(uint32_t now_ms) const;   // seconds since midnight
  bool lights_period(uint32_t now_ms) const;

 private:
  void handle_key(char k, uint32_t now);
  void update_sensors(const Inputs& in);
  void update_dosing(uint32_t now);
  void compute_outputs(uint32_t now, Outputs& out);
  void render(uint32_t now, const Outputs& out, char lcd[2][17]);
  void set_program(Program p);
  void finish_calibration(uint32_t now);
  bool dosing_allowed(uint32_t now) const;

  Settings s_;
  Program prog_;
  Screen scr_;
  uint8_t page_;
  uint32_t boot_ms_, splash_until_;
  bool save_;

  uint32_t clock_base_s_, clock_anchor_ms_, last_tod_;

  float ph_hist_[5];
  uint8_t ph_n_;
  uint32_t ph_count_, ph_last_ms_, settle_until_;
  float ph_, volts_;
  bool ph_ok_, probe_fault_;

  int8_t temp_;
  uint8_t rh_;
  bool climate_ok_;
  uint32_t climate_last_ms_;
  bool fan_on_, hum_on_;

  uint8_t level_;
  bool level_seen_, level_low_;
  uint32_t level_last_ms_;

  DoseState dose_;
  uint32_t dose_start_, dose_len_, mix_start_;
  float ph_before_, gain_;
  uint8_t unanswered_;
  bool correcting_, dose_eval_valid_, budget_hit_;
  uint32_t dose_ms_today_;
  uint16_t doses_today_;

  bool manual_[CH_COUNT];
  uint32_t manual_since_[CH_COUNT];
  bool last_on_[CH_COUNT];

  uint8_t cal_step_;
  float cal_v7_;
  float cal_hist_[6];
  uint8_t cal_n_;
  bool cal_ok_;
  uint32_t cal_result_until_;
  uint8_t cal_slope_pct_;

  char digits_[4];
  uint8_t ndig_;

  uint16_t alarms_;
};

}  // namespace hydro
