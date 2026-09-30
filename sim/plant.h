// plant.h - the physical world around the controller.
//
// A 12-litre NFT reservoir (weak-acid chemistry + atmospheric CO2), a two-zone
// mixing model, a glass pH electrode on an analog amplifier board, a DHT11 in
// a room with a daily temperature/humidity cycle, a resistive water-level
// strip and a simple crop-growth index. Everything is illustrative: the
// parameters are plausible for a small indoor rig, not measured on the
// prototype. The model exists to exercise the controller under realistic,
// repeatable conditions, not to predict yields.
#pragma once
#include <stdint.h>
#include "hmath.h"

namespace sim {

enum Load : uint8_t { L_PUMP, L_PH_DOWN, L_NUTRIENT, L_GROW, L_FLUO, L_FAN, L_HUMID, L_COUNT };

struct PlantParams {
  // reservoir
  double tank_L = 14.0;            // capacity (level sensor spans full depth)
  double volume0_L = 12.0;         // start volume
  double pump_intake_L = 3.0;      // below this the circulation pump sucks air
  double ph0 = 6.0;                // starting pH
  double buffer_mM = 3.0;          // phosphate-like weak acid, total
  double buffer_pKa = 6.8;
  double co2_M = 1.4e-5;           // dissolved CO2 in equilibrium with air
  double drift_mM_day = 0.30;      // alkalinity pushed out by nitrate uptake
  double evap_L_day = 0.55;        // evaporation + transpiration (at biomass 1)
  // dosing
  double dose_flow_mL_s = 20.0;    // mini sump pump
  double acid_mM = 50.0;           // diluted pH-down, mmol H+ per litre
  double acid_stock_mL = 2500.0;
  double nutrient_ec = 40.0;       // concentrate EC (mS/cm)
  double nutrient_acid_mM = 8.0;   // concentrates are mildly acidic
  double ec0 = 1.6;
  // mixing
  double zone_L = 0.4;             // where doses land
  double mix_tau_on_s = 35.0;      // circulation running
  double mix_tau_off_s = 900.0;    // still water
  double gas_tau_on_s = 5400.0;    // CO2 exchange with air, channels cascading
  double gas_tau_off_s = 28800.0;  // ...and with the pump off
  // pH electrode + amplifier board (DFRobot-style: pH = 3.5*V + offset)
  double probe_tau_s = 12.0;
  double board_offset_pH = 0.30;   // this particular board's true offset
  double board_slope = 3.5 * 1.06; // and its true slope (pH per volt)
  double adc_noise_V = 0.004;
  // climate
  double room_T = 26.0, room_T_amp = 4.0;
  double room_RH = 58.0, room_RH_amp = 14.0;
  // growth
  double growth_per_day = 0.32;
  double biomass_max = 12.0;
  // level strip
  double level_adc_empty = 40.0, level_adc_full = 640.0;
  // electrical loads (W)
  double watts[L_COUNT] = {40.0, 3.0, 3.0, 45.0, 36.0, 12.0, 24.0};
  double board_watts = 2.0;
};

enum ProbeFault : int { PF_NONE = 0, PF_STUCK = 1, PF_UNPLUGGED = 2 };
enum ProbeWhere : int { PROBE_TANK = 0, PROBE_BUF4 = 4, PROBE_BUF7 = 7 };

// pH of a solution with net strong-base alkalinity alk (mM), weak acid ct (mM)
// and dissolved inorganic carbon dic (mM, closed system)
double solve_ph(double alk_mM, double ct_mM, double pKa, double dic_mM);
// same, with CO2 in equilibrium with air
double solve_ph_open(double alk_mM, double ct_mM, double pKa, double co2_M);
double alk_for_ph(double ph, double ct_mM, double pKa, double co2_M);
double dic_eq(double ph, double co2_M);
// buffer capacity (mM per pH unit) by finite difference
double buffer_capacity(double ph, double ct_mM, double pKa, double co2_M);
double growth_factor_ph(double ph);

struct Plant {
  PlantParams p;
  hm::Rng rng;
  double t_s = 0;           // seconds since start (start = 06:00)
  double vol_L = 0;
  double alk_bulk = 0, alk_zone = 0, ct = 0, ec = 0, dic = 0;
  double ph_bulk = 0, ph_zone = 0, ph_electrode = 0;
  double air_T = 0, air_RH = 0;
  double biomass = 1.0;
  double light_today_h = 0; int day = 0;
  double pump_off_s = 0;
  // accounting
  double acid_left_mL = 0, acid_used_mL = 0, nutrient_used_mL = 0;
  double energy_Wh = 0, dry_run_s = 0, ph_in_band_s = 0, ph_time_s = 0;
  double min_ph = 14, max_ph = 0;
  // faults / experiment knobs
  int probe_fault = PF_NONE; double stuck_V = 3.2;
  int probe_where = PROBE_TANK;
  int dht_fail_ppm = 10000;  // 1% of reads fail a checksum
  double pending_s = 0;
  bool on[L_COUNT] = {};

  void init(const PlantParams& params, uint64_t seed);
  // integrate dt seconds with loads fixed at their current state
  void advance(double dt_s);
  // sensors
  double probe_volts_clean() const;
  int adc_ph();               // one analogRead of the pH board
  int adc_level();
  bool dht_read(int* t, int* rh);
  // operator actions
  void refill();
  double level_frac() const { return vol_L / p.tank_L; }
  double growth_rate_now() const;  // relative growth factor 0..1
 private:
  void slow_step(double dt);
  double solution_ph_at_probe() const;
};

}  // namespace sim
