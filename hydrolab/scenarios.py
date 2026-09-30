"""Experiments that characterise the Hydroponinator firmware.

Every experiment drives the unmodified firmware on the simulated rig the way
an operator would (key presses, moving the probe between buffers, topping
the tank up, breaking things). Results are plain dicts so they can be written
to JSON for the website and checked by the tests.
"""
from __future__ import annotations

import random
from multiprocessing import get_context

from .native import Rig, load

DAY = 86400.0
PERFECT_BOARD = dict(board_offset_pH=0.0, board_slope=3.5)  # matches the factory calibration
BAND = (5.5, 6.5)


def _commissioned(seed: int = 1, quantum_ms: float = 50.0, **params) -> Rig:
    """Power up, calibrate the probe, select automatic mode."""
    r = Rig(seed=seed, quantum_ms=quantum_ms, **params)
    r.run(5.0)
    r.calibrate()
    r.key("A")
    return r


# ------------------------------------------------------------ 14-day grow

def grow(days: float = 14.0, seed: int = 1, refill_every_days: float = 7.0, sample_s: float = 600.0) -> dict:
    """A two-week grow; the operator tops the reservoir up once a week."""
    r = _commissioned(seed=seed)
    r.set("reset_stats")
    t0 = r["t_s"]
    keys = ("t_h", "ph", "ph_fw", "level", "temp", "rh", "loads", "biomass", "acid_mL", "kWh", "gain")
    series = {k: [] for k in keys}
    alarms_seen = 0
    lights_on_s = pump_on_s = fan_on_s = hum_on_s = 0.0
    next_refill = t0 + refill_every_days * DAY + 3 * 3600.0
    end = t0 + days * DAY
    doses = 0
    last_acid = 0.0
    while r["t_s"] < end:
        r.run(sample_s)
        if refill_every_days and r["t_s"] >= next_refill:
            r.set("refill")
            next_refill += refill_every_days * DAY
        bits = int(r["loads"])
        alarms_seen |= int(r["alarms"])
        lights_on_s += sample_s if bits & (1 << 3) else 0.0
        pump_on_s += sample_s if bits & 1 else 0.0
        fan_on_s += sample_s if bits & (1 << 5) else 0.0
        hum_on_s += sample_s if bits & (1 << 6) else 0.0
        acid = r["acid_used_mL"]
        if acid > last_acid + 0.5:
            doses += 1
        last_acid = acid
        series["t_h"].append(round((r["t_s"] - t0) / 3600.0, 3))
        series["ph"].append(round(r["ph_bulk"], 3))
        series["ph_fw"].append(round(r["ph_reported"], 3))
        series["level"].append(round(r["level_frac"] * 100.0, 1))
        series["temp"].append(round(r["air_T"], 2))
        series["rh"].append(round(r["air_RH"], 1))
        series["loads"].append(bits)
        series["biomass"].append(round(r["biomass"], 4))
        series["acid_mL"].append(round(acid, 1))
        series["kWh"].append(round(r["energy_Wh"] / 1000.0, 4))
        series["gain"].append(round(r["gain"], 4))
    summary = dict(
        days=days,
        in_band_pct=round(100.0 * r["ph_in_band_s"] / max(r["ph_time_s"], 1.0), 1),
        ph_min=round(r["ph_min"], 2),
        ph_max=round(r["ph_max"], 2),
        ph_end=round(r["ph_bulk"], 2),
        acid_mL=round(r["acid_used_mL"]),
        acid_mL_per_day=round(r["acid_used_mL"] / days, 1),
        corrections=doses,
        energy_kWh=round(r["energy_Wh"] / 1000.0, 2),
        growth_index=round(r["biomass"], 2),
        dry_run_min=round(r["dry_run_s"] / 60.0, 1),
        lights_h_per_day=round(lights_on_s / 3600.0 / days, 1),
        pump_h_per_day=round(pump_on_s / 3600.0 / days, 1),
        fan_h_per_day=round(fan_on_s / 3600.0 / days, 1),
        humidifier_h_per_day=round(hum_on_s / 3600.0 / days, 1),
        learned_gain=round(r["gain"], 3),
        alarms_seen=alarms_seen,
        wdt_resets=int(r["wdt_resets"]),
        loop_max_ms=round(r["loop_max_ms"], 1),
    )
    return dict(summary=summary, series=series)


# ------------------------------------------------- dosing step response

def acid_to_reach(ph_from: float, ph_to: float, volume_L: float = 12.0, acid_mM: float = 50.0,
                  buffer_mM: float = 3.0) -> float:
    """mL of pH-down that moves the tank from ph_from to ph_to right after
    dosing (closed carbonate system: the CO2 has no time to leave)."""
    lib = load()
    alk0 = lib.chem_alk(ph_from, buffer_mM, 6.8)
    dic = lib.chem_dic_eq(ph_from)
    lo, hi = 0.0, alk0 + 5.0
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        if lib.chem_ph_closed(alk0 - mid, buffer_mM, 6.8, dic) > ph_to:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi) * volume_L / acid_mM * 1000.0


def dosing_step(start_ph: float = 7.9, hours: float = 3.0, seed: int = 1) -> dict:
    """The reservoir has just been filled with hard tap water (pH 7.9).
    Watch dose-and-wait bring it back to 6.0."""
    r = Rig(seed=seed, quantum_ms=5.0, **PERFECT_BOARD)
    r.run(35.0)            # past the electrode warm-up
    r.set("set_ph", start_ph)
    r.set("reset_stats")
    t0 = r["t_s"]
    s = {k: [] for k in ("t_min", "ph", "ph_zone", "ph_fw", "acid_mL", "dosing", "gain")}
    first_in_band = None
    doses = 0
    was_dosing = False
    while r["t_s"] - t0 < hours * 3600.0:
        dt = 2.0 if r["t_s"] - t0 < 3600 else 20.0
        r.run(dt)
        t = (r["t_s"] - t0) / 60.0
        ph = r["ph_bulk"]
        dosing = bool(int(r["loads"]) & 2) or r["dose_state"] == 1
        if dosing and not was_dosing:
            doses += 1
        was_dosing = dosing
        s["t_min"].append(round(t, 3))
        s["ph"].append(round(ph, 4))
        s["ph_zone"].append(round(r["ph_zone"], 3))
        s["ph_fw"].append(round(r["ph_reported"], 3))
        s["acid_mL"].append(round(r["acid_used_mL"], 1))
        s["dosing"].append(1 if dosing else 0)
        s["gain"].append(round(r["gain"], 4))
        if first_in_band is None and ph <= BAND[1]:
            first_in_band = t
    summary = dict(
        start_ph=start_ph,
        ph_min=round(min(s["ph"]), 2),
        ph_end=round(s["ph"][-1], 2),
        acid_mL=round(s["acid_mL"][-1]),
        acid_theory_mL=round(acid_to_reach(start_ph, 6.0)),
        doses=doses,
        minutes_to_band=None if first_in_band is None else round(first_in_band, 1),
        learned_gain=round(s["gain"][-1], 3),
        initial_gain=0.15,
        loop_max_ms=round(r["loop_max_ms"], 1),
    )
    return dict(summary=summary, series=s)


# ---------------------------------------------------------- probe faults

FAULTS = {
    "stuck_high": dict(fault=1, stuck_V=3.2, label="amplifier stuck high (reads pH 11.2)"),
    "stuck_plausible": dict(fault=1, stuck_V=1.97, label="probe frozen at a believable 6.9"),
    "unplugged": dict(fault=2, stuck_V=0.0, label="electrode unplugged"),
}


def probe_fault(kind: str, hours: float = 6.0, seed: int = 1) -> dict:
    """One hour in, the pH electrode fails. How much acid goes in, and how
    soon does the operator hear about it?"""
    f = FAULTS[kind]
    r = Rig(seed=seed, quantum_ms=5.0, **PERFECT_BOARD)
    r.run(5.0)
    r.run(3600.0)
    r.set("reset_stats")
    r.set("stuck_V", f["stuck_V"])
    r.set("probe_fault", f["fault"])
    t0 = r["t_s"]
    s = {k: [] for k in ("t_min", "ph", "acid_mL")}
    alarms = 0
    alarm_at = None
    while r["t_s"] - t0 < hours * 3600.0:
        r.run(10.0 if r["t_s"] - t0 < 1200 else 60.0)
        a = int(r["alarms"])
        alarms |= a
        if alarm_at is None and a & (2 | 4):
            alarm_at = (r["t_s"] - t0) / 60.0
        s["t_min"].append(round((r["t_s"] - t0) / 60.0, 2))
        s["ph"].append(round(r["ph_bulk"], 3))
        s["acid_mL"].append(round(r["acid_used_mL"], 1))
    return dict(kind=kind, series=s, summary=dict(
        label=f["label"],
        acid_mL=round(r["acid_used_mL"]),
        ph_min=round(min(s["ph"]), 2),
        ph_end=round(s["ph"][-1], 2),
        alarm_after_min=None if alarm_at is None else round(alarm_at, 1),
        alarms_seen=alarms,
        lcd=list(_alarm_lcd(r)),
    ))


def _alarm_lcd(r: Rig) -> tuple[str, str]:
    """The home screen alternates every 2 s; return the frame with the alarm."""
    for _ in range(6):
        top, bottom = r.lcd()
        if bottom.startswith("!"):
            return top, bottom
        r.run(1.0)
    return r.lcd()


# -------------------------------------------------------------- low water

def low_water(days: float = 8.0, evap_L_day: float = 1.6, seed: int = 1) -> dict:
    """Nobody tops the tank up during a hot spell."""
    r = Rig(seed=seed, quantum_ms=50.0, evap_L_day=evap_L_day, **PERFECT_BOARD)
    r.run(5.0)
    r.set("reset_stats")
    t0 = r["t_s"]
    s = {k: [] for k in ("t_h", "level", "pump")}
    alarm_at = None
    while r["t_s"] - t0 < days * DAY:
        r.run(900.0)
        s["t_h"].append(round((r["t_s"] - t0) / 3600.0, 2))
        s["level"].append(round(r["level_frac"] * 100.0, 1))
        s["pump"].append(int(r["loads"]) & 1)
        if alarm_at is None and int(r["alarms"]) & 1:
            alarm_at = (r["t_s"] - t0) / 3600.0
    intake_pct = 3.0 / 14.0 * 100.0
    level_at_alarm = next((lv for t, lv in zip(s["t_h"], s["level"]) if alarm_at is not None and t >= alarm_at), None)
    return dict(series=s, summary=dict(
        dry_run_h=round(r["dry_run_s"] / 3600.0, 1),
        low_water_alarm_h=None if alarm_at is None else round(alarm_at, 1),
        level_at_alarm_pct=level_at_alarm,
        pump_intake_pct=round(intake_pct, 1),
        lcd=list(_alarm_lcd(r)),
    ))


# --------------------------------------------------------- responsiveness

def responsiveness(presses: int = 60, seed: int = 3) -> dict:
    """An operator taps keys (150 ms each) at random moments while the
    controller is in the middle of correcting a high pH. Also records how
    long each pass through loop() takes."""
    r = Rig(seed=seed, quantum_ms=1.0, **PERFECT_BOARD)
    r.run(5.0)
    r.set("set_ph", 7.8).set("acid_left_mL", 0)   # keep it in the dosing branch
    r.run(40.0)
    r.set("reset_stats")
    rng = random.Random(seed)
    for _ in range(presses):
        r.run(rng.uniform(2.0, 20.0))
        r.key(rng.choice("C123456789*0#CC"), hold_ms=150.0, settle_s=0.0)
    r.run(10.0)
    # loop-time histogram over one minute
    edges = [0.25, 0.5, 1, 2, 5, 10, 20, 50]
    counts = [0] * (len(edges) + 1)
    loops0 = r["loops"]
    t_end = r["t_s"] + 60.0
    while r["t_s"] < t_end:
        r.run(0.001)
        ms = r["last_loop_ms"]
        counts[sum(1 for e in edges if ms > e)] += 1
    loops = r["loops"] - loops0
    pressed = int(r["keys_pressed"])
    caught = int(r["keys_caught"])
    return dict(summary=dict(
        pressed=pressed,
        caught=caught,
        caught_pct=round(100.0 * caught / max(pressed, 1), 1),
        latency_mean_ms=round(r["key_latency_mean_ms"], 2),
        latency_max_ms=round(r["key_latency_max_ms"], 2),
        loop_max_ms=round(r["loop_max_ms"], 1),
        loops_per_s=round(loops / 60.0),
    ), histogram=dict(edges_ms=edges, counts=counts))


# ------------------------------------------------------- reading accuracy

def sensor_sweep(calibrate: bool, seed: int = 1) -> dict:
    """What pH does the firmware report for a true pH between 4 and 9, on a
    real (imperfect) amplifier board, with and without the two-point
    calibration? The acid bottle is empty so nothing doses during the sweep."""
    r = Rig(seed=seed, quantum_ms=5.0)
    r.run(5.0)
    if calibrate:
        r.calibrate()
    r.set("acid_left_mL", 0)
    r.key("B")          # manual: no automatic dosing during the sweep
    true, rep = [], []
    ph = 4.0
    while ph <= 8.9001:
        r.set("set_ph", ph)
        r.run(90.0)     # electrode settles
        true.append(round(ph, 2))
        v = r["ph_reported"]
        rep.append(round(v, 3) if v >= 0 else None)
        ph += 0.1
    err = [abs(a - b) for a, b in zip(true, rep) if b is not None]
    return dict(true=true, reported=rep, summary=dict(
        label="two-point calibration" if calibrate else "factory default (3.5 pH/V)",
        max_error=round(max(err), 2),
        mean_error=round(sum(err) / len(err), 3),
    ))


# ------------------------------------------------------------- runner

def _job(args):
    key, fn, kwargs = args
    return key, fn(**kwargs)


def run_all(processes: int | None = None, quick: bool = False) -> dict:
    jobs = [("grow", grow, dict(days=3.0 if quick else 14.0)),
            ("dosing", dosing_step, {}),
            ("low_water", low_water, {}),
            ("responsiveness", responsiveness, {}),
            ("sweep_default", sensor_sweep, dict(calibrate=False)),
            ("sweep_calibrated", sensor_sweep, dict(calibrate=True))]
    jobs += [(f"fault_{k}", probe_fault, dict(kind=k)) for k in FAULTS]
    with get_context("fork").Pool(processes) as pool:
        done = pool.map(_job, jobs, chunksize=1)
    out = dict(done)
    out["faults"] = {k: out.pop(f"fault_{k}") for k in FAULTS}
    out["calibration"] = {"default": out.pop("sweep_default"), "calibrated": out.pop("sweep_calibrated")}
    return out
