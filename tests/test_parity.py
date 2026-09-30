"""The website runs the WebAssembly build; the analysis runs the native one.
Same C++, same inputs -> bit-identical results."""
import json
import shutil
import subprocess
from pathlib import Path

import pytest

from hydrolab import Rig

ROOT = Path(__file__).resolve().parent.parent
WASM = ROOT / "docs" / "js" / "hydrosim-wasm.js"

SCRIPT = r"""
import { WASM_BASE64 } from '%s';
const bytes = Buffer.from(WASM_BASE64, 'base64');
const { instance } = await WebAssembly.instantiate(bytes, {});
const e = instance.exports; e.__wasm_call_ctors();
const m = () => new Uint8Array(e.memory.buffer);
const str = p => { let s = ''; const a = m(); while (a[p]) s += String.fromCharCode(a[p++]); return s; };
const F = {}; for (let i = 0; i < e.sim_field_count(); i++) F[str(e.sim_field_name(i))] = i;
e.sim_init(7); e.sim_set(0, 20); e.sim_run(5000);
e.sim_key(67, 150); e.sim_run(2 * 3600e3);
const out = ['ph_bulk', 'ph_reported', 'air_T', 'energy_Wh', 'loops', 'biomass', 'acid_used_mL'].map(k => e.sim_get(F[k]));
console.log(JSON.stringify(out));
"""


@pytest.mark.skipif(shutil.which("node") is None or not WASM.exists(), reason="needs node and the wasm build")
def test_native_and_wasm_agree_bit_for_bit(tmp_path):
    js = tmp_path / "parity.mjs"
    js.write_text(SCRIPT % WASM.as_uri())
    got = json.loads(subprocess.run(["node", str(js)], capture_output=True, text=True, check=True).stdout)
    r = Rig(seed=7, quantum_ms=20).run(5)
    r.lib.sim_key(67, 150.0)
    r.run(2 * 3600)
    native = [r[k] for k in ("ph_bulk", "ph_reported", "air_T", "energy_Wh", "loops", "biomass", "acid_used_mL")]
    assert got == native
