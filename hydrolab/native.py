"""ctypes bindings for sim/build/libhydrosim.so (built on demand with make)."""
from __future__ import annotations

import ctypes
import os
import subprocess
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM_DIR = ROOT / "sim"
LIB_PATH = SIM_DIR / "build" / "libhydrosim.so"

_lock = threading.Lock()
_lib = None


def _sources_newer_than_lib() -> bool:
    if not LIB_PATH.exists():
        return True
    built = LIB_PATH.stat().st_mtime
    watched = list(SIM_DIR.rglob("*.cpp")) + list(SIM_DIR.rglob("*.h")) + list(SIM_DIR.rglob("*.inc"))
    watched += list(SIM_DIR.rglob("*.ino"))
    watched += list((ROOT / "firmware" / "Hydroponics").rglob("*"))
    return any(p.is_file() and p.stat().st_mtime > built for p in watched)


def load() -> ctypes.CDLL:
    """Build (if needed) and load the native simulator."""
    global _lib
    with _lock:
        if _lib is not None:
            return _lib
        if os.environ.get("HYDROLAB_NO_BUILD") != "1" and _sources_newer_than_lib():
            subprocess.run(["make", "-s", "-C", str(SIM_DIR), "native"], check=True)
        lib = ctypes.CDLL(str(LIB_PATH))
        d, i, s = ctypes.c_double, ctypes.c_int, ctypes.c_char_p
        sigs = {
            "sim_param_count": ([], i), "sim_param_name": ([i], s),
            "sim_field_count": ([], i), "sim_field_name": ([i], s),
            "sim_action_count": ([], i), "sim_action_name": ([i], s),
            "sim_param": ([i, d], None), "sim_param_get": ([i], d),
            "sim_init": ([d], i), "sim_run": ([d], None),
            "sim_key": ([i, d], None), "sim_key_later": ([i, d, d], None), "sim_get": ([i], d), "sim_set": ([i, d], None),
            "sim_lcd": ([], ctypes.POINTER(ctypes.c_uint8)),
            "sim_serial": ([d], i), "sim_serial_buf": ([], ctypes.POINTER(ctypes.c_char)),
            "chem_ph": ([d, d, d], d), "chem_ph_closed": ([d, d, d, d], d), "chem_dic_eq": ([d], d),
            "chem_alk": ([d, d, d], d), "chem_beta": ([d, d, d], d), "chem_growth_ph": ([d], d),
            "core_volts_to_ph": ([d, d, d], d),
        }
        for name, (args, res) in sigs.items():
            fn = getattr(lib, name)
            fn.argtypes = args
            fn.restype = res
        _lib = lib
        return lib


def _names(count, getter):
    lib = load()
    return {getattr(lib, getter)(k).decode(): k for k in range(getattr(lib, count)())}


class chem:
    """Reservoir chemistry used by the simulator (same code as the website)."""

    @staticmethod
    def ph(alk_mM: float, buffer_mM: float = 3.0, pKa: float = 6.8) -> float:
        return load().chem_ph(alk_mM, buffer_mM, pKa)

    @staticmethod
    def alkalinity(ph: float, buffer_mM: float = 3.0, pKa: float = 6.8) -> float:
        return load().chem_alk(ph, buffer_mM, pKa)

    @staticmethod
    def buffer_capacity(ph: float, buffer_mM: float = 3.0, pKa: float = 6.8) -> float:
        return load().chem_beta(ph, buffer_mM, pKa)

    @staticmethod
    def growth_factor(ph: float) -> float:
        return load().chem_growth_ph(ph)


class Rig:
    """The simulated Hydroponinator: a virtual Mega running the firmware.

    Only one Rig can be live per process (the sketch's globals are real
    globals, exactly as on the Arduino); creating a new Rig re-flashes and
    power-cycles it.
    """

    def __init__(self, seed: int = 1, quantum_ms: float = 20.0, **params):
        self.lib = load()
        self.fields = _names("sim_field_count", "sim_field_name")
        self.actions = _names("sim_action_count", "sim_action_name")
        self.param_ids = _names("sim_param_count", "sim_param_name")
        self.lib.sim_param(-1, 0.0)  # defaults
        for k, v in params.items():
            self.lib.sim_param(self.param_ids[k], float(v))
        self.lib.sim_init(float(seed))
        self.set("quantum_ms", quantum_ms)
        self._serial_pos = 0

    # --- time
    def run(self, seconds: float) -> "Rig":
        self.lib.sim_run(seconds * 1000.0)
        return self

    def run_until(self, t_s: float) -> "Rig":
        dt = t_s - self["t_s"]
        if dt > 0:
            self.run(dt)
        return self

    # --- state
    def __getitem__(self, field: str) -> float:
        return self.lib.sim_get(self.fields[field])

    def snapshot(self, *names: str) -> dict:
        return {n: self[n] for n in (names or self.fields)}

    def param(self, name: str) -> float:
        return self.lib.sim_param_get(self.param_ids[name])

    def set(self, action: str, value: float = 0.0) -> "Rig":
        self.lib.sim_set(self.actions[action], float(value))
        return self

    def loads(self) -> dict:
        bits = int(self["loads"])
        names = ["pump", "ph_down", "nutrient", "grow_light", "fluo_light", "fan", "humidifier"]
        return {n: bool(bits >> i & 1) for i, n in enumerate(names)}

    # --- operator
    def key(self, k: str, hold_ms: float = 150.0, settle_s: float = 0.4) -> "Rig":
        self.lib.sim_key(ord(k), hold_ms)
        if settle_s:
            self.run(settle_s)
        return self

    def key_later(self, k: str, delay_s: float, hold_ms: float = 150.0) -> "Rig":
        """Schedule a press that starts delay_s from now, even if the sketch
        is inside a delay() at that moment."""
        self.lib.sim_key_later(ord(k), delay_s * 1000.0, hold_ms)
        return self

    def keys(self, seq: str, gap_s: float = 0.4) -> "Rig":
        for k in seq:
            self.key(k, settle_s=gap_s)
        return self

    def lcd(self) -> tuple[str, str]:
        raw = self.lib.sim_lcd()
        cells = bytes(raw[i] for i in range(32))

        def show(b):
            return "".join(chr(c) if 32 <= c < 127 else "▒" for c in b)

        return show(cells[:16]), show(cells[16:])

    def serial(self) -> str:
        """Serial output since the last call."""
        n = self.lib.sim_serial(float(self._serial_pos))
        buf = self.lib.sim_serial_buf()
        text = bytes(buf[:n]).decode("latin1")
        self._serial_pos = int(self["serial_total"])
        return text

    def calibrate(self, settle_s: float = 60.0) -> "Rig":
        """Walk the two-point calibration wizard like an operator would."""
        self.key("C").key("0")
        self.set("probe_where", 7).run(settle_s).key("#")
        self.set("probe_where", 4).run(settle_s).key("#")
        self.set("probe_where", 0).run(5.0)
        return self
