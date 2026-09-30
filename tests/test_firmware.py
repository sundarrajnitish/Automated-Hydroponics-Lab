"""firmware/Hydroponics/Hydroponics.ino, unmodified, on the simulated rig."""
import csv
import io

from hydrolab import Rig

PERFECT = dict(board_offset_pH=0.0, board_slope=3.5)


def test_nothing_clicks_on_during_boot():
    r = Rig(quantum_ms=1)
    assert r["loads"] == 0                  # right after setup(): every relay off
    r.run(0.3)
    assert not r.loads()["pump"]            # no level reading yet -> pump held off
    r.run(3)
    assert r.loads()["pump"] and r.loads()["grow_light"]


def test_loop_never_blocks():
    r = Rig(quantum_ms=1).run(5)
    r.keys("CA").key("B").keys("1479").key("A")
    r.run(600)
    assert r["loop_max_ms"] < 80            # worst case: DHT11 read + LCD redraw
    assert r["keys_missed"] == 0
    assert r["wdt_resets"] == 0


def test_every_key_is_caught_while_dosing():
    r = Rig(quantum_ms=1, **PERFECT).run(40)
    r.set("set_ph", 7.8).run(5).set("reset_stats")
    for _ in range(20):
        r.key("C", hold_ms=120, settle_s=1.3)
    assert r["keys_caught"] == 20


def test_telemetry_is_valid_csv():
    r = Rig().run(31)
    lines = [l for l in r.serial().splitlines() if l and not l.startswith("#")]
    rows = list(csv.reader(io.StringIO("\n".join(lines))))
    assert len(rows) >= 5
    assert all(len(row) == 11 for row in rows)
    t = [int(row[0]) for row in rows]
    assert all(b - a == 5 for a, b in zip(t, t[1:]))


def test_calibration_and_program_survive_a_reset():
    r = Rig().run(5).calibrate().key("D")
    cal_screen = r.key("C").keys("###").lcd()
    r.set("reboot").run(3)
    assert r["program"] == 2                # economy mode restored
    r.key("C").keys("###")
    assert r.lcd()[0] == cal_screen[0]      # same "Cal V7=..." line


def test_calibration_fixes_the_board_error():
    r = Rig().run(40)                     # real board: offset + slope error
    uncal = abs(r["ph_reported"] - r["ph_bulk"])
    r.calibrate().run(60)
    cal = abs(r["ph_reported"] - r["ph_bulk"])
    assert uncal > 0.4 and cal < 0.08


def test_holds_ph_in_band_for_three_days():
    r = Rig(quantum_ms=50).run(5).calibrate().key("A").set("reset_stats")
    r.run(3 * 86400)
    assert r["ph_in_band_s"] / r["ph_time_s"] > 0.99
    assert r["alarms"] == 0


def test_low_water_interlock():
    r = Rig(quantum_ms=50, evap_L_day=4.0, **PERFECT).run(5)
    r.run(4 * 86400)
    assert int(r["alarms"]) & 1
    assert not r.loads()["pump"]
    assert r["dry_run_s"] == 0


def test_stuck_probe_is_contained():
    r = Rig(quantum_ms=5, **PERFECT).run(40)
    r.set("probe_fault", 1).set("stuck_V", 1.97).run(3 * 3600)   # reads a plausible 6.9
    assert int(r["alarms"]) & 4                  # DOSING HALTED
    assert r["acid_used_mL"] < 100
    r.set("stuck_V", 3.2).set("reset_stats").run(600)             # reads 11.2: rejected
    assert r["acid_used_mL"] == 0


def test_lcd_text_is_always_printable():
    r = Rig(quantum_ms=5).run(3)
    for seq in ("C", "#", "#", "#", "#", "*", "12", "*", "0", "*", "B", "13", "A", "D"):
        r.keys(seq)
        top, bottom = r.lcd()
        assert "▒" not in top + bottom


def test_lights_follow_the_photoperiod():
    r = Rig(quantum_ms=50).run(5)
    on_hours = 0
    for _ in range(24):
        r.run(3600)
        on_hours += r.loads()["grow_light"]
    assert on_hours == 16


def test_economy_mode_saves_light_and_pump_time():
    r = Rig(quantum_ms=50).run(5).key("D")
    lit = pumped = 0
    for _ in range(24 * 12):
        r.run(300)
        lit += r.loads()["grow_light"]
        pumped += r.loads()["pump"]
    assert lit / 12 == 12                   # 12 h of light
    assert pumped / 12 < 7                  # 10 min on / 20-50 min off
    assert not r.loads()["fluo_light"]


def test_manual_dosing_pump_switches_itself_off():
    r = Rig(quantum_ms=5).run(5).key("B").key("3")
    assert r.loads()["ph_down"]
    r.run(11)
    assert not r.loads()["ph_down"]


def test_blind_climate_control_fails_safe():
    r = Rig(quantum_ms=20).run(5)
    r.set("dht_fail_ppm", 1_000_000).run(60)
    assert int(r["alarms"]) & 32            # DHT11 FAULT
    assert r.loads()["fan"] and not r.loads()["humidifier"]


def test_reservoir_never_overdosed_from_a_hard_water_fill():
    r = Rig(quantum_ms=5, **PERFECT).run(35)
    r.set("set_ph", 7.9).set("reset_stats").run(2 * 3600)
    assert r["ph_min"] > 5.9                # dose-and-wait lands on 6.0 without overshoot
    assert r["ph_bulk"] < 6.5
