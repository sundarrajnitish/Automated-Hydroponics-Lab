#include "plant.h"

namespace sim {

using namespace hm;

static const double K1_CO2 = 4.4668359215096346e-07;  // 10^-6.35
static const double K2_CO2 = 4.6773514128719810e-11;  // 10^-10.33
static const double KW = 1e-14;

// Carbonate fractions for a given [H+]
static void carbonate_split(double h, double* f0, double* f1, double* f2) {
  double d = h * h + K1_CO2 * h + K1_CO2 * K2_CO2;
  *f0 = h * h / d;              // CO2(aq)
  *f1 = K1_CO2 * h / d;         // HCO3-
  *f2 = K1_CO2 * K2_CO2 / d;    // CO3--
}

// Charge balance of the nutrient solution, positive when there is more
// positive charge than the anions can balance at this pH:
//   Alk + [H+] - [OH-] - [A-] - [HCO3-] - 2[CO3--]
// with a fixed amount of dissolved inorganic carbon (closed system: what
// the solution does in the minutes after a dose).
static double balance_closed(double ph, double alk_M, double ct_M, double ka, double dic_M) {
  double h = pow10_(-ph);
  double f0, f1, f2;
  carbonate_split(h, &f0, &f1, &f2);
  return alk_M + h - KW / h - ct_M * ka / (ka + h) - dic_M * (f1 + 2.0 * f2);
}

// Same, with CO2 in equilibrium with air (open system: where it ends up
// after hours of aeration).
static double balance_open(double ph, double alk_M, double ct_M, double ka, double co2_M) {
  double h = pow10_(-ph);
  double hco3 = K1_CO2 * co2_M / h;
  double co3 = K2_CO2 * hco3 / h;
  return alk_M + h - KW / h - ct_M * ka / (ka + h) - hco3 - 2.0 * co3;
}

static double bisect(double lo, double hi, int iters, double alk_M, double ct_M, double ka, double x, bool open) {
  for (int i = 0; i < iters; ++i) {  // both balances fall monotonically with pH
    double mid = 0.5 * (lo + hi);
    double f = open ? balance_open(mid, alk_M, ct_M, ka, x) : balance_closed(mid, alk_M, ct_M, ka, x);
    if (f > 0) lo = mid; else hi = mid;
  }
  return 0.5 * (lo + hi);
}

double solve_ph(double alk_mM, double ct_mM, double pKa, double dic_mM) {
  return bisect(1.0, 13.0, 48, alk_mM * 1e-3, ct_mM * 1e-3, pow10_(-pKa), dic_mM * 1e-3, false);
}

// Same answer to ~1e-8 pH, found near the previous value (the chemistry
// changes slowly between 0.25 s steps). Falls back to the full search.
static double solve_ph_near(double prev, double alk_mM, double ct_mM, double pKa, double dic_mM) {
  double ka = pow10_(-pKa), a = alk_mM * 1e-3, c = ct_mM * 1e-3, d = dic_mM * 1e-3;
  double lo = prev - 0.04, hi = prev + 0.04;
  if (balance_closed(lo, a, c, ka, d) > 0 && balance_closed(hi, a, c, ka, d) <= 0) return bisect(lo, hi, 24, a, c, ka, d, false);
  return solve_ph(alk_mM, ct_mM, pKa, dic_mM);
}

double solve_ph_open(double alk_mM, double ct_mM, double pKa, double co2_M) {
  return bisect(1.0, 13.0, 48, alk_mM * 1e-3, ct_mM * 1e-3, pow10_(-pKa), co2_M, true);
}

double alk_for_ph(double ph, double ct_mM, double pKa, double co2_M) {
  // alkalinity that puts the open-system balance at zero at this pH
  return -balance_open(ph, 0.0, ct_mM * 1e-3, pow10_(-pKa), co2_M) * 1e3;
}

double dic_eq(double ph, double co2_M) {
  double h = pow10_(-ph);
  return co2_M * (1.0 + K1_CO2 / h + K1_CO2 * K2_CO2 / (h * h)) * 1e3;
}

// open-system buffer capacity (mM per pH unit)
double buffer_capacity(double ph, double ct_mM, double pKa, double co2_M) {
  const double d = 0.01;
  return (alk_for_ph(ph + d, ct_mM, pKa, co2_M) - alk_for_ph(ph - d, ct_mM, pKa, co2_M)) / (2 * d);
}

double growth_factor_ph(double ph) {
  // Plateau where most nutrients stay available, falling off either side
  // (iron/manganese lock-out above ~6.5, root stress below ~5.3).
  double d = 0;
  if (ph < 5.6) d = 5.6 - ph;
  else if (ph > 6.4) d = ph - 6.4;
  return exp_(-(d / 0.8) * (d / 0.8));
}

void Plant::init(const PlantParams& params, uint64_t seed) {
  *this = Plant();
  p = params;
  rng = Rng(seed * 2654435761ULL + 12345);
  vol_L = p.volume0_L;
  ct = p.buffer_mM;
  alk_bulk = alk_zone = alk_for_ph(p.ph0, ct, p.buffer_pKa, p.co2_M);
  dic = dic_eq(p.ph0, p.co2_M);
  ph_bulk = ph_zone = ph_electrode = p.ph0;
  ec = p.ec0;
  air_T = p.room_T - p.room_T_amp * 0.5;  // 06:00 is on the cool side
  air_RH = p.room_RH + p.room_RH_amp * 0.5;
  acid_left_mL = p.acid_stock_mL;
}

void Plant::advance(double dt_s) {
  if (dt_s <= 0) return;
  // Dosing is integrated exactly for the time each pump is on.
  if (on[L_PH_DOWN] && acid_left_mL > 0) {
    double mL = min_(p.dose_flow_mL_s * dt_s, acid_left_mL);
    acid_left_mL -= mL; acid_used_mL += mL;
    alk_zone -= (mL * 1e-3 * p.acid_mM) / p.zone_L;
    vol_L += mL * 1e-3;
  }
  if (on[L_NUTRIENT]) {
    double mL = p.dose_flow_mL_s * dt_s;
    nutrient_used_mL += mL;
    double L = mL * 1e-3;
    ec = (ec * vol_L + p.nutrient_ec * L) / (vol_L + L);
    alk_zone -= (L * p.nutrient_acid_mM) / p.zone_L;
    vol_L += L;
  }
  if (vol_L > p.tank_L) vol_L = p.tank_L;  // the tank overflows onto the floor
  double w = p.board_watts;
  for (int i = 0; i < L_COUNT; ++i) if (on[i]) w += p.watts[i];
  energy_Wh += w * dt_s / 3600.0;
  if (on[L_PUMP] && vol_L < p.pump_intake_L) dry_run_s += dt_s;

  pending_s += dt_s;
  const double STEP = 0.25;
  while (pending_s >= STEP) { slow_step(STEP); pending_s -= STEP; }
}

double Plant::growth_rate_now() const {
  double light = 0;
  if (on[L_GROW] && on[L_FLUO]) light = 1.15;
  else if (on[L_GROW]) light = 1.0;
  else if (on[L_FLUO]) light = 0.45;
  if (light_today_h > 16.0) light *= 0.25;  // beyond a useful daily light integral
  double fT = 1.0;
  if (air_T < 18) fT = max_(0.3, 1.0 - (18 - air_T) * 0.07);
  if (air_T > 28) fT = max_(0.3, 1.0 - (air_T - 28) * 0.1);
  double fEC = 1.0;
  if (ec < 1.0) fEC = max_(0.3, 1.0 - (1.0 - ec) * 0.6);
  if (ec > 2.6) fEC = max_(0.3, 1.0 - (ec - 2.6) * 0.4);
  double froot = 1.0;
  if (pump_off_s > 1800) froot = max_(0.2, 1.0 - (pump_off_s - 1800) / 10800.0 * 0.8);
  return light * growth_factor_ph(ph_bulk) * fT * fEC * froot;
}

void Plant::slow_step(double dt) {
  t_s += dt;
  bool circulating = on[L_PUMP] && vol_L >= p.pump_intake_L;
  bool lights = on[L_GROW] || on[L_FLUO];

  // --- two-zone mixing (mass conserving)
  double tau = circulating ? p.mix_tau_on_s : p.mix_tau_off_s;
  double vb = vol_L - p.zone_L;
  if (vb < 0.1) vb = 0.1;
  double total = alk_zone * p.zone_L + alk_bulk * vb;
  double vt = p.zone_L + vb;
  double d = (alk_zone - alk_bulk) * exp_(-dt / tau * (1.0 + p.zone_L / vb));
  double mean = total / vt;
  alk_bulk = mean - d * p.zone_L / vt;
  alk_zone = alk_bulk + d;

  // --- evaporation/transpiration concentrates everything
  double evap = p.evap_L_day / 86400.0 * dt * (0.6 + 0.4 * min_(biomass, 4.0)) * (lights ? 1.2 : 0.8);
  double v_new = vol_L - evap;
  if (v_new < 0.5) v_new = 0.5;
  double c = vol_L / v_new;
  alk_bulk *= c; alk_zone *= c; ct *= c; ec *= c; dic *= c;
  vol_L = v_new;

  // --- roots take up nitrate and push alkalinity out (pH creeps up)
  double wet = circulating ? 1.0 : (pump_off_s < 1800 ? 1.0 : 0.3);
  // nutrient-locked plants (pH far off) grow slowly and take up less nitrate
  double uptake = (0.5 + 0.5 * min_(biomass, 3.0)) * (lights ? 1.3 : 0.6) * wet *
                  (0.25 + 0.75 * growth_factor_ph(ph_bulk));
  alk_bulk += p.drift_mM_day / 86400.0 * dt * uptake;
  ec -= ec * 0.02 / 86400.0 * dt * uptake;

  // --- CO2 exchange with the air: slow, faster while the channels cascade
  {
    double h = pow10_(-ph_bulk), f0, f1, f2;
    carbonate_split(h, &f0, &f1, &f2);
    double co2_now = dic * f0;                       // mM
    double tau_gas = circulating ? p.gas_tau_on_s : p.gas_tau_off_s;
    dic += (p.co2_M * 1e3 - co2_now) * (1.0 - exp_(-dt / tau_gas));
  }

  ph_bulk = solve_ph_near(ph_bulk, alk_bulk, ct, p.buffer_pKa, dic);
  double gap = alk_zone - alk_bulk;
  ph_zone = (gap > 1e-4 || gap < -1e-4) ? solve_ph_near(ph_zone, alk_zone, ct, p.buffer_pKa, dic) : ph_bulk;

  // --- the glass electrode is a first-order lag
  double target = solution_ph_at_probe();
  ph_electrode += (target - ph_electrode) * (1.0 - exp_(-dt / p.probe_tau_s));

  // --- room climate
  double tod_h = 6.0 + t_s / 3600.0;
  int new_day = (int)(tod_h / 24.0);
  if (new_day != day) { day = new_day; light_today_h = 0; }
  tod_h -= 24.0 * (double)new_day;
  double phase = sin_(6.28318530717958647692 * (tod_h - 9.0) / 24.0);
  double T_target = p.room_T + p.room_T_amp * phase + (on[L_GROW] ? 1.5 : 0) + (on[L_FLUO] ? 0.8 : 0) -
                    (on[L_FAN] ? 2.5 : 0);
  double RH_target = p.room_RH - p.room_RH_amp * phase + (on[L_HUMID] ? 22.0 : 0) - (on[L_FAN] ? 12.0 : 0) -
                     (lights ? 3.0 : 0);
  air_T += (T_target - air_T) * (1.0 - exp_(-dt / 1200.0));
  air_RH += (clamp_(RH_target, 10, 97) - air_RH) * (1.0 - exp_(-dt / 900.0));

  // --- crop
  if (circulating) pump_off_s = 0; else pump_off_s += dt;
  double g = growth_rate_now();
  biomass += p.growth_per_day / 86400.0 * dt * biomass * (1.0 - biomass / p.biomass_max) * g;
  if (on[L_GROW] || on[L_FLUO]) light_today_h += dt / 3600.0;

  ph_time_s += dt;
  if (ph_bulk >= 5.5 && ph_bulk <= 6.5) ph_in_band_s += dt;
  if (ph_bulk < min_ph) min_ph = ph_bulk;
  if (ph_bulk > max_ph) max_ph = ph_bulk;
}

double Plant::solution_ph_at_probe() const {
  if (probe_where == PROBE_BUF4) return 4.0;
  if (probe_where == PROBE_BUF7) return 7.0;
  return ph_bulk;
}

double Plant::probe_volts_clean() const {
  if (probe_fault == PF_UNPLUGGED) return 0.0;
  if (probe_fault == PF_STUCK) return stuck_V;
  return (ph_electrode - p.board_offset_pH) / p.board_slope;
}

int Plant::adc_ph() {
  double v = probe_volts_clean() + p.adc_noise_V * rng.gauss();
  int a = (int)(v / 5.0 * 1024.0);
  return a < 0 ? 0 : (a > 1023 ? 1023 : a);
}

int Plant::adc_level() {
  double f = clamp_(level_frac(), 0.0, 1.0);
  // conductive strips respond a little non-linearly: approximate f^0.85
  double shaped = exp_(0.85 * log_(f > 1e-6 ? f : 1e-6));
  double a = p.level_adc_empty + (p.level_adc_full - p.level_adc_empty) * shaped + 3.0 * rng.gauss();
  return a < 0 ? 0 : (a > 1023 ? 1023 : (int)a);
}

bool Plant::dht_read(int* t, int* rh) {
  if ((int)(rng.uniform() * 1e6) < dht_fail_ppm) return false;
  double tt = air_T + 0.6 + 0.3 * rng.gauss();
  double hh = air_RH + 2.0 + 1.0 * rng.gauss();
  hh = clamp_(hh, 20.0, 90.0);   // DHT11 range
  *t = (int)tt;                  // DHT11 reports whole units
  *rh = (int)hh;
  return true;
}

void Plant::refill() {
  // top up with tap water: ~1 mM bicarbonate alkalinity, EC 0.3
  double add = p.volume0_L - vol_L;
  if (add <= 0) return;
  double vn = vol_L + add;
  alk_bulk = (alk_bulk * vol_L + 1.0 * add) / vn;
  dic = (dic * vol_L + (1.0 + p.co2_M * 1e3) * add) / vn;
  alk_zone = alk_bulk;
  ct = ct * vol_L / vn;
  ec = (ec * vol_L + 0.3 * add) / vn;
  vol_L = vn;
}

}  // namespace sim
