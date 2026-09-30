// sim.js - thin wrapper around the WebAssembly build of sim/ (C++).
// Every Rig is its own WebAssembly instance with its own memory: a virtual
// Arduino Mega running the firmware, wired to its own simulated reservoir,
// so several sections of the page can each run a rig at the same time.
import { WASM_BASE64 } from "./hydrosim-wasm.js";

export const LOADS = ["pump", "ph_down", "nutrient", "grow_light", "fluo_light", "fan", "humidifier"];
export const ALARMS = ["Low water", "pH probe fault", "Dosing halted", "pH too low", "Acid limit", "DHT11 fault"];
export const PERFECT_BOARD = { board_offset_pH: 0, board_slope: 3.5 };

let modulePromise = null;
function wasmBytes() {
  const bin = atob(WASM_BASE64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
export function loadModule() {
  if (!modulePromise) modulePromise = WebAssembly.compile(wasmBytes());
  return modulePromise;
}

export class Rig {
  static async create() {
    const mod = await loadModule();
    const inst = await WebAssembly.instantiate(mod, {});
    return new Rig(inst.exports);
  }
  constructor(e) {
    this.e = e;
    e.__wasm_call_ctors();
    const names = (count, get) => {
      const m = {};
      for (let i = 0; i < e[count](); i++) m[this.str(e[get](i))] = i;
      return m;
    };
    this.F = names("sim_field_count", "sim_field_name");
    this.A = names("sim_action_count", "sim_action_name");
    this.P = names("sim_param_count", "sim_param_name");
    this.serialPos = 0;
  }
  str(ptr) {
    const m = new Uint8Array(this.e.memory.buffer);
    let s = "";
    while (m[ptr]) s += String.fromCharCode(m[ptr++]);
    return s;
  }
  // power the board on with these plant parameters
  init(params = {}, seed = 1) {
    const { e } = this;
    e.sim_param(-1, 0);
    for (const [k, v] of Object.entries(params)) e.sim_param(this.P[k], v);
    e.sim_init(seed);
    this.serialPos = 0;
    return this;
  }
  run(ms) { this.e.sim_run(ms); return this; }
  get(name) { return this.e.sim_get(this.F[name]); }
  set(action, v = 0) { this.e.sim_set(this.A[action], v); return this; }
  key(ch, holdMs = 150) { this.e.sim_key(ch.charCodeAt(0), holdMs); return this; }
  lcd() {
    const p = this.e.sim_lcd();
    return new Uint8Array(this.e.memory.buffer, p, 32).slice();
  }
  loads() {
    const bits = this.get("loads");
    const o = {};
    LOADS.forEach((n, i) => { o[n] = !!((bits >> i) & 1); });
    return o;
  }
  serial() {
    const n = this.e.sim_serial(this.serialPos);
    const p = this.e.sim_serial_buf();
    const s = new TextDecoder("latin1").decode(new Uint8Array(this.e.memory.buffer, p, n));
    this.serialPos = this.get("serial_total");
    return s;
  }
  alarms() {
    const a = this.get("alarms");
    return ALARMS.filter((_, i) => (a >> i) & 1);
  }
  // chemistry and conversion helpers (the same code the rig uses)
  phOpen(alk, ct = 3, pKa = 6.8) { return this.e.chem_ph(alk, ct, pKa); }
  phClosed(alk, ct, pKa, dic) { return this.e.chem_ph_closed(alk, ct, pKa, dic); }
  alk(ph, ct = 3, pKa = 6.8) { return this.e.chem_alk(ph, ct, pKa); }
  dicEq(ph) { return this.e.chem_dic_eq(ph); }
  beta(ph, ct = 3, pKa = 6.8) { return this.e.chem_beta(ph, ct, pKa); }
  growth(ph) { return this.e.chem_growth_ph(ph); }
  voltsToPh(v, v7, v4) { return this.e.core_volts_to_ph(v, v7, v4); }
}

// Runs registered simulations on animation frames, only while visible,
// within a per-frame CPU budget.
export class Scheduler {
  constructor() {
    this.jobs = new Set();
    this.last = 0;
    const loop = (t) => {
      const dt = this.last ? Math.min(0.05, (t - this.last) / 1000) : 0;
      this.last = t;
      for (const j of this.jobs) if (j.visible && j.running) j.step(dt);
      requestAnimationFrame(loop);
    };
    requestAnimationFrame(loop);
  }
  // el: one element or several; the job runs while any of them is on screen
  add(job, el) {
    job.visible = true;
    this.jobs.add(job);
    const els = (Array.isArray(el) ? el : [el]).filter(Boolean);
    if (els.length && "IntersectionObserver" in window) {
      const seen = new Map(els.map((e) => [e, true]));
      const io = new IntersectionObserver((es) => {
        for (const e of es) seen.set(e.target, e.isIntersecting);
        job.visible = [...seen.values()].some(Boolean);
      }, { rootMargin: "120px" });
      els.forEach((e) => io.observe(e));
    }
    return job;
  }
}

export function fmtClock(simSeconds, startHour = 6) {
  const s = Math.floor(simSeconds + startHour * 3600);
  const d = Math.floor(s / 86400);
  const hh = String(Math.floor((s % 86400) / 3600)).padStart(2, "0");
  const mm = String(Math.floor((s % 3600) / 60)).padStart(2, "0");
  return `day ${d + 1} · ${hh}:${mm}`;
}
