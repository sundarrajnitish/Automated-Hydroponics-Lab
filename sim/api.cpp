// api.cpp - C interface to the simulator, shared by the Python analysis
// (ctypes, native build) and the website (WebAssembly build).
//
// One instance holds one rig: a virtual Arduino Mega running the firmware in
// firmware/Hydroponics, wired to the simulated reservoir. (The WebAssembly
// page creates several instances when it needs several rigs.)
// Field/parameter names are exported too, so the Python and JavaScript
// wrappers look indices up instead of hard-coding them.
#include "vboard.h"
#include "sketch.h"
#include "../firmware/Hydroponics/src/hydro_core.h"

#define API extern "C" __attribute__((visibility("default")))

namespace {

struct Rig {
  sim::Board board;
  const sim::SketchIface* sketch;
  sim::PlantParams params;
  uint64_t quantum_us = 1000;
  bool booted = false;
};

Rig g_rig;

Rig* rig() {
  g_rig.sketch = &sim::FIRMWARE;
  sim::g_board = &g_rig.board;
  return &g_rig;
}

// ---- tunable plant parameters (set before sim_init)
struct ParamDef { const char* name; double sim::PlantParams::*field; };
const ParamDef PARAMS[] = {
    {"volume0_L", &sim::PlantParams::volume0_L},
    {"tank_L", &sim::PlantParams::tank_L},
    {"pump_intake_L", &sim::PlantParams::pump_intake_L},
    {"ph0", &sim::PlantParams::ph0},
    {"buffer_mM", &sim::PlantParams::buffer_mM},
    {"buffer_pKa", &sim::PlantParams::buffer_pKa},
    {"drift_mM_day", &sim::PlantParams::drift_mM_day},
    {"evap_L_day", &sim::PlantParams::evap_L_day},
    {"dose_flow_mL_s", &sim::PlantParams::dose_flow_mL_s},
    {"acid_mM", &sim::PlantParams::acid_mM},
    {"acid_stock_mL", &sim::PlantParams::acid_stock_mL},
    {"mix_tau_on_s", &sim::PlantParams::mix_tau_on_s},
    {"mix_tau_off_s", &sim::PlantParams::mix_tau_off_s},
    {"gas_tau_on_s", &sim::PlantParams::gas_tau_on_s},
    {"gas_tau_off_s", &sim::PlantParams::gas_tau_off_s},
    {"probe_tau_s", &sim::PlantParams::probe_tau_s},
    {"board_offset_pH", &sim::PlantParams::board_offset_pH},
    {"board_slope", &sim::PlantParams::board_slope},
    {"adc_noise_V", &sim::PlantParams::adc_noise_V},
    {"room_T", &sim::PlantParams::room_T},
    {"room_RH", &sim::PlantParams::room_RH},
    {"growth_per_day", &sim::PlantParams::growth_per_day},
};
const int NPARAMS = sizeof(PARAMS) / sizeof(PARAMS[0]);

// ---- readable state
const char* const FIELDS[] = {
    "t_s", "ph_bulk", "ph_electrode", "ph_zone", "ph_reported", "air_T", "air_RH", "vol_L", "level_frac",
    "biomass", "ec", "acid_used_mL", "acid_left_mL", "energy_Wh", "dry_run_s", "loads", "ph_in_band_s",
    "ph_time_s", "ph_min", "ph_max", "keys_pressed", "keys_caught", "keys_missed", "key_latency_max_ms",
    "key_latency_mean_ms", "wdt_resets", "eeprom_writes", "growth_now", "alarms", "dose_state", "gain",
    "dose_ms_today", "program", "alk_bulk", "probe_volts", "light_today_h", "nutrient_used_mL",
    "serial_total", "pump_off_s", "loops", "loop_max_ms", "last_loop_ms", "screen", "lcd_bytes",
    "probe_where", "probe_fault", "day", "dic",
};
const int NFIELDS = sizeof(FIELDS) / sizeof(FIELDS[0]);

// ---- things an experimenter can do to a running rig
const char* const ACTIONS[] = {
    "quantum_ms",    // idle time the driver inserts between loop() calls
    "probe_fault",   // 0 none, 1 stuck, 2 unplugged
    "stuck_V",
    "probe_where",   // 0 tank, 4 pH-4 buffer, 7 pH-7 buffer
    "refill",        // top the reservoir up with tap water
    "drift_mM_day",
    "evap_L_day",
    "dht_fail_ppm",
    "set_ph",        // force the reservoir chemistry to this pH (scenario setup)
    "acid_left_mL",
    "reset_stats",   // zero the accounting counters
    "reboot",        // press the reset button: RAM and pins reset, EEPROM kept
};
const int NACTIONS = sizeof(ACTIONS) / sizeof(ACTIONS[0]);

uint8_t g_lcd[32];
char g_serial[4096];

}  // namespace

API int sim_param_count() { return NPARAMS; }
API const char* sim_param_name(int i) { return (i >= 0 && i < NPARAMS) ? PARAMS[i].name : ""; }
API int sim_field_count() { return NFIELDS; }
API const char* sim_field_name(int i) { return (i >= 0 && i < NFIELDS) ? FIELDS[i] : ""; }
API int sim_action_count() { return NACTIONS; }
API const char* sim_action_name(int i) { return (i >= 0 && i < NACTIONS) ? ACTIONS[i] : ""; }

API void sim_param(int i, double v) {
  Rig* r = rig();
  if (!r) return;
  if (i < 0) { r->params = sim::PlantParams(); return; }
  if (i < NPARAMS) r->params.*(PARAMS[i].field) = v;
}

API double sim_param_get(int i) {
  Rig* r = rig();
  if (!r || i < 0 || i >= NPARAMS) return 0;
  return r->params.*(PARAMS[i].field);
}

// Power the virtual board on and run the sketch's setup().
API int sim_init(double seed) {
  Rig* r = rig();
  if (!r) return -1;
  r->board.power_on(r->params, (uint64_t)seed);
  r->sketch->reset();
  r->sketch->setup();
  r->booted = true;
  return 0;
}

// Run loop() repeatedly until `ms` of simulated time has passed.
API void sim_run(double ms) {
  Rig* r = rig();
  if (!r || !r->booted) return;
  sim::Board& b = r->board;
  uint64_t target = b.now_us + (uint64_t)(ms * 1000.0);
  while (b.now_us < target) {
    uint64_t t0 = b.now_us;
    r->sketch->loop();
    ++b.loops;
    b.last_loop_ms = (double)(b.now_us - t0) / 1000.0;
    if (b.last_loop_ms > b.loop_max_ms) b.loop_max_ms = b.last_loop_ms;
    b.advance_us(r->quantum_us);
    if (b.wdt_on && b.now_us - b.wdt_last_us > (uint64_t)b.wdt_ms * 1000ULL) {
      ++b.wdt_resets;
      uint32_t resets = b.wdt_resets;
      b.cpu_reset();
      b.wdt_resets = resets;
      r->sketch->reset();
      r->sketch->setup();
    }
  }
}

API void sim_key(int key, double dur_ms) {
  Rig* r = rig();
  if (r) r->board.press_key((char)key, dur_ms);
}

// Schedule a press that starts delay_ms from now (e.g. during a delay()).
API void sim_key_later(int key, double delay_ms, double dur_ms) {
  Rig* r = rig();
  if (r) r->board.press_key((char)key, dur_ms, delay_ms);
}

API double sim_get(int f) {
  Rig* r = rig();
  if (!r) return 0;
  const sim::Board& b = r->board;
  const sim::Plant& p = b.plant;
  const sim::SketchIface* s = r->sketch;
  switch (f) {
    case 0: return p.t_s + p.pending_s;
    case 1: return p.ph_bulk;
    case 2: return p.ph_electrode;
    case 3: return p.ph_zone;
    case 4: return s->reported_ph();
    case 5: return p.air_T;
    case 6: return p.air_RH;
    case 7: return p.vol_L;
    case 8: return p.level_frac();
    case 9: return p.biomass;
    case 10: return p.ec;
    case 11: return p.acid_used_mL;
    case 12: return p.acid_left_mL;
    case 13: return p.energy_Wh;
    case 14: return p.dry_run_s;
    case 15: return (double)b.load_bits();
    case 16: return p.ph_in_band_s;
    case 17: return p.ph_time_s;
    case 18: return p.min_ph;
    case 19: return p.max_ph;
    case 20: return b.keys_pressed;
    case 21: return b.keys_caught;
    case 22: return b.keys_missed;
    case 23: return b.key_latency_max_ms;
    case 24: return b.keys_caught ? b.key_latency_sum_ms / b.keys_caught : 0.0;
    case 25: return b.wdt_resets;
    case 26: return b.eeprom_writes;
    case 27: return p.growth_rate_now();
    case 28: return s->alarms();
    case 29: return s->dose_state();
    case 30: return s->gain();
    case 31: return s->dose_ms_today();
    case 32: return s->program();
    case 33: return p.alk_bulk;
    case 34: return p.probe_volts_clean();
    case 35: return p.light_today_h;
    case 36: return p.nutrient_used_mL;
    case 37: return (double)b.serial_total;
    case 38: return p.pump_off_s;
    case 39: return (double)b.loops;
    case 40: return b.loop_max_ms;
    case 41: return b.last_loop_ms;
    case 42: return s->screen();
    case 43: return b.lcd_bytes;
    case 44: return p.probe_where;
    case 45: return p.probe_fault;
    case 46: return p.day;
    case 47: return p.dic;
  }
  return 0;
}

API void sim_set(int a, double v) {
  Rig* r = rig();
  if (!r) return;
  sim::Plant& p = r->board.plant;
  switch (a) {
    case 0: r->quantum_us = (uint64_t)(v * 1000.0); if (r->quantum_us < 50) r->quantum_us = 50; break;
    case 1: p.probe_fault = (int)v; break;
    case 2: p.stuck_V = v; break;
    case 3: p.probe_where = (int)v; break;
    case 4: p.refill(); break;
    case 5: p.p.drift_mM_day = v; break;
    case 6: p.p.evap_L_day = v; break;
    case 7: p.dht_fail_ppm = (int)v; break;
    case 8:
      p.alk_bulk = p.alk_zone = sim::alk_for_ph(v, p.ct, p.p.buffer_pKa, p.p.co2_M);
      p.dic = sim::dic_eq(v, p.p.co2_M);
      p.ph_bulk = p.ph_zone = v;
      break;
    case 9: p.acid_left_mL = v; break;
    case 10:
      p.acid_used_mL = p.nutrient_used_mL = p.energy_Wh = p.dry_run_s = 0;
      p.ph_in_band_s = p.ph_time_s = 0; p.min_ph = 14; p.max_ph = 0;
      r->board.keys_pressed = r->board.keys_caught = r->board.keys_missed = 0;
      r->board.key_latency_sum_ms = r->board.key_latency_max_ms = 0;
      r->board.loop_max_ms = 0;
      break;
    case 11:
      r->board.cpu_reset();
      r->sketch->reset();
      r->sketch->setup();
      break;
  }
}

// The 32 visible LCD cells (row 0 then row 1), raw HD44780 character codes.
API const uint8_t* sim_lcd() {
  Rig* r = rig();
  for (int i = 0; i < 16; ++i) { g_lcd[i] = r->board.ddram[i]; g_lcd[16 + i] = r->board.ddram[0x40 + i]; }
  return g_lcd;
}

// Serial output from index `from` (see field serial_total). Returns length.
API int sim_serial(double from_d) {
  Rig* r = rig();
  if (!r) return 0;
  const sim::Board& b = r->board;
  uint32_t from = (uint32_t)from_d;
  if (b.serial_total > sim::Board::SERIAL_CAP && from < b.serial_total - sim::Board::SERIAL_CAP)
    from = b.serial_total - sim::Board::SERIAL_CAP;
  int n = 0;
  for (uint32_t i = from; i < b.serial_total && n < (int)sizeof(g_serial) - 1; ++i)
    g_serial[n++] = b.serial_log[i % sim::Board::SERIAL_CAP];
  g_serial[n] = 0;
  return n;
}
API const char* sim_serial_buf() { return g_serial; }

// ---- pure helpers used by the lessons on the website
API double chem_ph(double alk_mM, double ct_mM, double pKa) { return sim::solve_ph_open(alk_mM, ct_mM, pKa, 1.4e-5); }
API double chem_ph_closed(double alk_mM, double ct_mM, double pKa, double dic_mM) { return sim::solve_ph(alk_mM, ct_mM, pKa, dic_mM); }
API double chem_dic_eq(double ph) { return sim::dic_eq(ph, 1.4e-5); }
API double chem_alk(double ph, double ct_mM, double pKa) { return sim::alk_for_ph(ph, ct_mM, pKa, 1.4e-5); }
API double chem_beta(double ph, double ct_mM, double pKa) { return sim::buffer_capacity(ph, ct_mM, pKa, 1.4e-5); }
API double chem_growth_ph(double ph) { return sim::growth_factor_ph(ph); }
API double core_volts_to_ph(double v, double v7, double v4) {
  hydro::Calibration c;
  c.v7 = (float)v7;
  c.v4 = (float)v4;
  return (double)hydro::volts_to_ph((float)v, c);
}
