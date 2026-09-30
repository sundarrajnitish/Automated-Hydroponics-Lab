// app.js - wires every chapter of the page to the simulator.
// Author: Nitish Sundarraj
import { Rig, PERFECT_BOARD, Scheduler, fmtClock, loadModule } from "./sim.js";
import { FrontPanel } from "./ui.js";
import { LineChart } from "./chart.js";

const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const sched = new Scheduler();
const REAL_BOARD = { board_offset_pH: 0.3, board_slope: 3.5 * 1.06 };
const fx = (v, d = 2) => (v == null || !isFinite(v) ? "—" : Number(v).toFixed(d));

// ---------------------------------------------------------------- helpers

function seg(sel, onPick) {
  const root = $(sel);
  root.addEventListener("click", (e) => {
    const b = e.target.closest("button");
    if (!b) return;
    $$("button", root).forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
    onPick(b);
  });
}

// Keeps a rig's simulated clock locked to wall-clock time (times a speed
// factor). A sketch stuck in delay() jumps the rig ahead; the pacer then waits
// for real time to catch up, so key presses land where they would on a real
// board. Never spends more than budgetMs of CPU in one frame.
class Pacer {
  constructor(rig) { this.bind(rig); }
  bind(rig) { this.rig = rig; this.target = rig.get("t_s") * 1000; }
  now() { return this.rig.get("t_s") * 1000; }
  advance(simMs, budgetMs = 9, onChunk = null) {
    this.target += simMs;
    const chunk = Math.max(16, simMs / 8);
    const t0 = performance.now();
    let n;
    while ((n = this.now()) < this.target && performance.now() - t0 < budgetMs) {
      this.rig.run(Math.min(this.target - n, chunk));
      if (onChunk) onChunk();
    }
    const behind = this.target - this.now();
    if (behind > 4 * simMs + 1000) { this.target = this.now(); return false; }  // too slow: drop the backlog
    return true;
  }
}

function stat(k, v, cls = "") { return `<div class="stat ${cls}"><span class="k">${k}</span><span class="v">${v}</span></div>`; }

function failNotice(el, err) {
  console.error(err);
  const p = document.createElement("p");
  p.className = "notice";
  p.textContent = "This live demo needs WebAssembly, which this browser blocked. The recorded results further down still apply.";
  el.prepend(p);
}

async function getJSON(path) {
  const r = await fetch(path);
  if (!r.ok) throw new Error(`${path}: ${r.status}`);
  return r.json();
}

// ------------------------------------------------------------------ theme

function setupTheme() {
  const btn = $("#theme-btn");
  const order = ["system", "light", "dark"];
  let mode = "system";
  try { mode = localStorage.getItem("hy-theme") || "system"; } catch (e) { /* storage blocked */ }
  const apply = () => {
    if (mode === "system") delete document.documentElement.dataset.theme;
    else document.documentElement.dataset.theme = mode;
    btn.textContent = `theme: ${mode}`;
  };
  btn.addEventListener("click", () => {
    mode = order[(order.indexOf(mode) + 1) % 3];
    try { localStorage.setItem("hy-theme", mode); } catch (e) { /* ignore */ }
    apply();
  });
  apply();
}

function setupNav() {
  const links = new Map($$(".chapters a").map((a) => [a.getAttribute("href").slice(1), a]));
  const io = new IntersectionObserver((es) => {
    for (const e of es) if (e.isIntersecting) {
      links.forEach((a) => a.classList.remove("on"));
      links.get(e.target.id)?.classList.add("on");
    }
  }, { rootMargin: "-45% 0px -50% 0px" });
  $$("section.chapter").forEach((s) => io.observe(s));
}

// --------------------------------------------------------- 0+1 hero + rig

async function heroAndRig() {
  const rig = (await Rig.create()).init(PERFECT_BOARD, 11);
  rig.set("quantum_ms", 2);
  const panel = new FrontPanel($("#hero-panel"), rig, { title: "Hydroponinator · virtual Mega", big: true });
  let speed = 1;
  const pacer = new Pacer(rig);
  seg("#hero-speed", (b) => {
    speed = +b.dataset.s;
    rig.set("quantum_ms", speed >= 3600 ? 20 : speed >= 60 ? 5 : 2);
  });
  const svg = $(".schematic");
  const parts = $$(".part[data-load]", svg);
  const truth = $("#hero-truth");
  const job = {
    running: true,
    step(dt) {
      pacer.advance(dt * speed * 1000);
      const l = panel.update(fmtClock(rig.get("t_s")));
      truth.textContent = `true pH ${fx(rig.get("ph_bulk"))} · tank ${fx(rig.get("level_frac") * 100, 0)} %`;
      for (const p of parts) p.classList.toggle("live", !!l[p.dataset.load]);
      svg.classList.toggle("pumping", l.pump);
    },
  };
  sched.add(job, [$("#hero-panel"), svg]);
}

function setupParts() {
  const list = $("#parts-list");
  const svg = $(".schematic svg");
  const pick = (id) => {
    $$("button", list).forEach((b) => b.classList.toggle("on", b.dataset.part === id));
    $$(".part", svg).forEach((p) => p.classList.toggle("on", p.dataset.part === id));
  };
  list.addEventListener("click", (e) => { const b = e.target.closest("button"); if (b) pick(b.dataset.part); });
  $$(".part", svg).forEach((p) => p.addEventListener("click", () => pick(p.dataset.part)));
}

// -------------------------------------------------------------- 2 chemistry

function chemistry(w) {
  const chart = new LineChart($("#ch-chart"), {
    height: 280, xDomain: [0, 600], yDomain: [4, 8.6], xLabel: "mL of pH-down", yLabel: "pH",
    bands: [{ y0: 5.5, y1: 6.5, label: "target 5.5-6.5" }], legend: $("#ch-legend"),
    xFmt: (v) => String(Math.round(v)), tipX: (v) => `${Math.round(v)} mL added`,
  });
  const avail = new LineChart($("#avail-chart"), {
    height: 190, xDomain: [4, 9], yDomain: [0, 1.05], yLabel: "availability",
    bands: [{ y0: -1, y1: 2, color: "--surface" }], xFmt: (v) => v.toFixed(1), tipX: (v) => `pH ${v.toFixed(2)}`,
  });
  const ax = [], ay = [];
  for (let p = 4; p <= 9.0001; p += 0.02) { ax.push(p); ay.push(w.growth(p)); }
  avail.set([{ label: "availability", color: "--accent", x: ax, y: ay, fmt: (v) => `${Math.round(v * 100)} %` }]);

  const inp = { start: $("#ch-start"), buf: $("#ch-buf"), acid: $("#ch-acid") };
  const upd = () => {
    const p0 = +inp.start.value, ct = +inp.buf.value, mL = +inp.acid.value;
    $("#ch-start-o").textContent = p0.toFixed(1);
    $("#ch-buf-o").textContent = `${ct.toFixed(1)} mM`;
    $("#ch-acid-o").textContent = `${mL} mL`;
    const alk0 = w.alk(p0, ct), dic = w.dicEq(p0);
    const perML = 50 / 1000 / 12;   // mM of alkalinity removed per mL
    const xs = [], closed = [], open = [];
    for (let v = 0; v <= 600; v += 4) {
      xs.push(v);
      closed.push(w.phClosed(alk0 - v * perML, ct, 6.8, dic));
      open.push(w.phOpen(alk0 - v * perML, ct));
    }
    const now = w.phClosed(alk0 - mL * perML, ct, 6.8, dic);
    const later = w.phOpen(alk0 - mL * perML, ct);
    chart.o.marker = { x: mL, y: now, color: "--accent" };
    chart.set([
      { label: "right after the dose", color: "--accent", x: xs, y: closed },
      { label: "hours later, CO₂ gone", color: "--ink-2", dash: [5, 4], x: xs, y: open },
    ]);
    // acid needed to reach 6.0 right after dosing
    let lo = 0, hi = 5000;
    for (let i = 0; i < 50; i++) { const m = (lo + hi) / 2; if (w.phClosed(alk0 - m * perML, ct, 6.8, dic) > 6.0) lo = m; else hi = m; }
    $("#ch-stats").innerHTML =
      stat("pH right after", fx(now)) + stat("pH hours later", fx(later), later > 6.5 ? "warn" : "") +
      stat("buffer at start", `${fx(w.beta(p0, ct), 2)} mM/pH`) + stat("mL to reach 6.0", Math.round(lo));
  };
  Object.values(inp).forEach((i) => i.addEventListener("input", upd));
  upd();
}

// ----------------------------------------------------------------- 3 sensor

function sensor(w, cal) {
  const inp = { ph: $("#sn-ph"), off: $("#sn-off"), slope: $("#sn-slope") };
  const FACTORY = { v7: 2.0, v4: 4 / 3.5, done: false };
  let c = { ...FACTORY };
  const btn = $("#sn-cal");
  const board = () => ({ off: +inp.off.value, slope: 3.5 * (1 + +inp.slope.value / 100) });
  const adcOf = (ph) => {
    const b = board();
    const v = (ph - b.off) / b.slope;
    return Math.max(0, Math.min(1023, Math.floor((v / 5) * 1024)));
  };
  btn.addEventListener("click", () => {
    c = { v7: (adcOf(7) * 5) / 1024, v4: (adcOf(4) * 5) / 1024, done: true };
    btn.textContent = "Calibrated ✓ (change the board to start again)";
    upd();
  });
  [inp.off, inp.slope].forEach((i) => i.addEventListener("input", () => {
    if (c.done) { c = { ...FACTORY }; btn.textContent = "Calibrate in pH 7 and pH 4 buffers"; }
  }));
  const sweep = new LineChart($("#sweep-chart"), {
    height: 300, xDomain: [4, 9], yDomain: [3, 9.5], xLabel: "true pH", yLabel: "reported pH", legend: $("#sweep-legend"),
    xFmt: (v) => v.toFixed(1), tipX: (v) => `true pH ${v.toFixed(2)}`,
  });
  if (cal) {
    const S = (k, color) => ({ label: cal[k].summary.label, color, x: cal[k].true, y: cal[k].reported });
    sweep.set([
      { label: "ideal", color: "--line", x: [4, 9], y: [4, 9], noTip: true, noLegend: true, width: 1.5 },
      S("default", "--s-orange"), S("calibrated", "--s-blue"),
    ]);
  }
  const upd = () => {
    const ph = +inp.ph.value;
    const b = board();
    $("#sn-ph-o").textContent = ph.toFixed(2);
    $("#sn-off-o").textContent = `${b.off >= 0 ? "+" : ""}${b.off.toFixed(2)} pH`;
    $("#sn-slope-o").textContent = `${+inp.slope.value >= 0 ? "+" : ""}${inp.slope.value} %`;
    const mv = -59.16 * (ph - 7);
    const volts = (ph - b.off) / b.slope;
    const adc = adcOf(ph);
    const sum = adc * 6;
    const v = (sum / 6) * 5 / 1024;
    const p = w.voltsToPh(v, c.v7, c.v4);
    const rejected = p > 9 || adc <= 3 || adc >= 1020;
    $("#sn-chain").innerHTML = `
      <div><span class="k">electrode</span><span class="v">${mv >= 0 ? "+" : ""}${mv.toFixed(0)} mV</span><span class="d">−59.16 mV per pH at 25 °C</span></div>
      <div><span class="k">amplifier out</span><span class="v">${volts.toFixed(3)} V</span><span class="d">(pH − offset) ÷ gain</span></div>
      <div><span class="k">10-bit ADC</span><span class="v">${adc}</span><span class="d">⌊V ÷ 5 × 1024⌋</span></div>
      <div><span class="k">filtered</span><span class="v">${v.toFixed(3)} V</span><span class="d">middle 6 of 10 samples</span></div>
      <div><span class="k">firmware pH</span><span class="v" style="color:var(--s-blue)">${rejected ? "fault" : p.toFixed(2)}</span><span class="d">error ${rejected ? "—" : fx(Math.abs(p - ph), 2)}</span></div>`;
    $("#sn-code-a").innerHTML =
`<span class="c">// one analogRead every 100 ms</span>
phRaw[i] = analogRead(PIN_PH);      <span class="c">// ${adc}</span>
<span class="c">// after ten: sort, keep the middle six</span>
sum = s[2] + ... + s[7];            <span class="c">// ${sum}</span>
volts = sum / 6.0 * 5.0 / 1024.0;   <span class="c">// <span class="hl">${v.toFixed(3)} V</span></span>`;
    $("#sn-code-b").innerHTML =
`pH = 7 + (V − v7) × 3 / (v7 − v4)
v7 = ${c.v7.toFixed(3)} V   v4 = ${c.v4.toFixed(3)} V  <span class="c">${c.done ? "// from your buffers" : "// factory default"}</span>
   = <span class="hl">${p.toFixed(2)}</span>
${rejected ? '<span class="c">// above pH 9 or at an ADC rail: rejected as a sensor fault</span>' : `if (pH &gt; 6.3) start a correction  <span class="c">// ${p > 6.3 ? "yes" : "no"}</span>`}`;
    const e = Math.abs(p - ph);
    const vd = $("#sn-verdict");
    vd.className = "verdict " + (rejected ? "bad" : e < 0.1 ? "good" : "bad");
    vd.textContent = rejected ? "Reading rejected: no dosing, PH PROBE FAULT on the LCD." : e < 0.1 ? `Within ${e.toFixed(2)} pH.` : `Off by ${e.toFixed(2)} pH: calibrate the board.`;
    sweep.o.marker = rejected ? null : { x: ph, y: p, color: "--s-blue" };
    sweep.request();
  };
  Object.values(inp).forEach((i) => i.addEventListener("input", upd));
  upd();
}

// ----------------------------------------------------------------- 4 dosing

async function dosing(recorded) {
  const chart = new LineChart($("#ds-chart"), {
    height: 300, xDomain: [0, 180], yDomain: [5.4, 8.5], xLabel: "minutes", yLabel: "pH",
    bands: [{ y0: 5.5, y1: 6.5, label: "target 5.5-6.5" }],
    legend: $("#ds-legend"), xFmt: (v) => String(Math.round(v)), tipX: (v) => `minute ${v.toFixed(1)}`,
  });
  const series = (d) => [
    { label: "where the acid lands", color: "--s-aqua", width: 1, alpha: 0.6, x: d.t_min, y: d.ph_zone },
    { label: "reservoir pH", color: "--s-blue", width: 2.5, x: d.t_min, y: d.ph, endDot: !!d.live },
  ];
  const stats = (o) => {
    $("#ds-stats").innerHTML =
      stat("pH-down used", `${Math.round(o.acid)} mL`) + stat("doses", o.doses) +
      stat("lowest pH", fx(o.min), o.min < 5.9 ? "bad" : "good") + stat("learned response", `${fx(o.gain, 3)} pH/s`) +
      stat(o.live ? "state" : "in band after", o.live ? o.state : `${o.band} min`);
  };
  if (recorded) {
    chart.set(series(recorded.series));
    const r = recorded.summary;
    stats({ acid: r.acid_mL, doses: r.doses, min: r.ph_min, gain: r.learned_gain, band: r.minutes_to_band });
  }
  const inputs = { start: $("#ds-start"), mix: $("#ds-mix"), acid: $("#ds-acid") };
  const out = () => {
    $("#ds-start-o").textContent = (+inputs.start.value).toFixed(1);
    $("#ds-mix-o").textContent = `${inputs.mix.value} s`;
    $("#ds-acid-o").textContent = `${inputs.acid.value} mM`;
  };
  Object.values(inputs).forEach((i) => i.addEventListener("input", out));
  out();
  let speed = 60;
  seg("#ds-speed", (b) => { speed = +b.dataset.s; });

  let rig = null, pacer = null, d = null, t0 = 0, doses = 0, wasDosing = false;
  const job = { running: false, step(dt) {
    pacer.advance(dt * speed * 1000, 6, () => {
      const dosingNow = rig.get("dose_state") === 1;
      if (dosingNow && !wasDosing) doses++;
      wasDosing = dosingNow;
    });
    const t = (rig.get("t_s") - t0) / 60;
    if (!d.t_min.length || t - d.t_min[d.t_min.length - 1] > 0.05) {
      d.t_min.push(t); d.ph.push(rig.get("ph_bulk")); d.ph_zone.push(rig.get("ph_zone"));
    }
    chart.request();
    stats({ live: true, acid: rig.get("acid_used_mL"), doses, min: rig.get("ph_min"), gain: rig.get("gain"),
      state: ["waiting", "dosing", "mixing", "locked out"][rig.get("dose_state")] || "waiting" });
    $("#ds-note").textContent = `Live: minute ${t.toFixed(1)} of 180.`;
    if (t >= 180) { job.running = false; $("#ds-run").textContent = "Run again"; }
  } };
  sched.add(job, $("#ds-chart"));
  $("#ds-run").addEventListener("click", async () => {
    if (!rig) rig = await Rig.create();
    rig.init({ ...PERFECT_BOARD, mix_tau_on_s: +inputs.mix.value, acid_mM: +inputs.acid.value }, 1);
    rig.set("quantum_ms", 5).run(35000).set("set_ph", +inputs.start.value).set("reset_stats");
    t0 = rig.get("t_s");
    doses = 0; wasDosing = false;
    d = { t_min: [], ph: [], ph_zone: [], live: true };
    pacer = new Pacer(rig);
    chart.set(series(d));
    job.running = true;
    $("#ds-run").textContent = "Restart";
  });
}

// ----------------------------------------------------------------- 5 timing

async function timing(resp) {
  if (resp) {
    const { edges_ms: e, counts } = resp.histogram;
    const total = counts.reduce((a, b) => a + b, 0);
    const label = (i) => (i === 0 ? `≤ ${e[0]} ms` : i === counts.length - 1 ? `> ${e[e.length - 1]} ms` : `${e[i - 1]}-${e[i]} ms`);
    const max = Math.max(...counts);
    $("#hist").innerHTML = counts.map((n, i) => n ? `<div class="bar-row"><span>${label(i)}</span><span class="track"><span class="fill" style="display:block;width:${(100 * n) / max}%"></span></span><span class="n">${((100 * n) / total).toFixed(n / total < 0.01 ? 2 : 1)} %</span></div>` : "").join("");
    const s = resp.summary;
    $("#tm-stats").innerHTML = stat("passes per second", s.loops_per_s.toLocaleString()) + stat("longest pass", `${s.loop_max_ms} ms`) +
      stat("keys caught", `${s.caught} / ${s.pressed}`, "good") + stat("key latency", `${s.latency_max_ms} ms`);
  }
  const rig = (await Rig.create()).init(PERFECT_BOARD, 3);
  rig.set("quantum_ms", 1).run(40000).set("acid_left_mL", 0).set("set_ph", 7.8).run(40000);
  const t0 = rig.get("t_s"), ticks = [];
  let loops = rig.get("loops");
  while (rig.get("t_s") - t0 < 12) {
    rig.run(1);
    const l = rig.get("loops");
    if (l > loops) ticks.push(rig.get("t_s") - t0);
    loops = l;
  }
  const draw = () => {
    const cv = $("#tl-fw");
    const wpx = cv.clientWidth || 600, dpr = window.devicePixelRatio || 1;
    cv.width = wpx * dpr; cv.height = 34 * dpr;
    const g = cv.getContext("2d");
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = getComputedStyle(document.documentElement).getPropertyValue("--s-blue").trim();
    for (const t of ticks) g.fillRect(Math.round((t / 12) * (wpx - 2)), 4, 1, 26);
    $("#tl-fw-n").textContent = `${ticks.length.toLocaleString()} calls`;
  };
  draw();
  new ResizeObserver(draw).observe($("#tl-fw"));
  new MutationObserver(draw).observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
}

// -------------------------------------------------------------- 6 simulator

async function simulator() {
  const rigInst = await Rig.create();
  let speed = 3600, paused = false, rig = null, pacer = null;
  const panel = { obj: null };
  const phX = [], phTrue = [], phFw = [], lvX = [], lv = [];
  let lastSample = -1e9, evapOn = false, dhtOn = false;
  const serialEl = $("#sim-serial");
  const phChart = new LineChart($("#sim-ph"), {
    height: 230, yDomain: null, yMin: 5.5, yMax: 7, xLabel: "hours", legend: $("#sim-ph-legend"),
    bands: [{ y0: 5.5, y1: 6.5, label: "target" }], xFmt: (v, st) => v.toFixed(st < 1 ? 1 : 0), tipX: (v) => `hour ${v.toFixed(1)}`,
  });
  const envChart = new LineChart($("#sim-env"), {
    height: 150, yDomain: [0, 100], xLabel: "hours", yLabel: "%", legend: $("#sim-env-legend"),
    bands: [{ y0: 0, y1: 21.4, color: "--accent-soft", label: "below the pump intake" }],
    xFmt: (v, st) => v.toFixed(st < 1 ? 1 : 0), tipX: (v) => `hour ${v.toFixed(1)}`, yFmt: (v) => v.toFixed(0),
  });
  const quantum = () => (speed >= 21600 ? 50 : speed >= 3600 ? 20 : speed >= 60 ? 5 : 2);
  const boot = () => {
    const perfect = $("#sim-board").checked;
    rig = rigInst.init(perfect ? PERFECT_BOARD : REAL_BOARD, 21);
    rig.set("quantum_ms", quantum());
    for (const a of [phX, phTrue, phFw, lvX, lv]) a.length = 0;
    lastSample = -1e9; evapOn = false; dhtOn = false;
    serialEl.textContent = "";
    if (!panel.obj) panel.obj = new FrontPanel($("#sim-panel"), rig, { title: "virtual Mega" });
    else panel.obj.setRig(rig);
    phChart.set([
      { label: "true pH", color: "--ink", x: phX, y: phTrue },
      { label: "firmware reading", color: "--s-blue", dash: [5, 4], x: phX, y: phFw },
    ]);
    envChart.set([{ label: "water level", color: "--s-aqua", x: lvX, y: lv, fmt: (v) => `${v.toFixed(0)} %` }]);
    $$("[data-act]").forEach((b) => b.setAttribute("aria-pressed", "false"));
    step(2 * 3600, true);   // open on a couple of hours of history
    pacer = new Pacer(rig);
  };
  const sample = () => {
    const t = rig.get("t_s") / 3600;
    phX.push(t); phTrue.push(rig.get("ph_bulk"));
    const rep = rig.get("ph_reported");
    phFw.push(rep >= 0 ? rep : null);
    lvX.push(t); lv.push(rig.get("level_frac") * 100);
    while (phX.length && t - phX[0] > 48) { phX.shift(); phTrue.shift(); phFw.shift(); lvX.shift(); lv.shift(); }
  };
  const step = (simSec, force = false) => {
    const every = Math.min(300, Math.max(1, speed / 4));
    let left = simSec * 1000;
    const t0 = performance.now();
    while (left > 0 && (force || performance.now() - t0 < 10)) {
      const chunk = Math.min(left, every * 1000);
      rig.run(chunk);
      left -= chunk;
      const t = rig.get("t_s");
      if (t - lastSample >= every) { sample(); lastSample = t; }
    }
    return simSec - left / 1000;
  };
  const render = () => {
    panel.obj.update(fmtClock(rig.get("t_s")));
    phChart.request(); envChart.request();
    const rep = rig.get("ph_reported");
    const tru = rig.get("ph_bulk");
    const bad = (v) => v < 5.5 || v > 6.5;
    $("#sim-stats").innerHTML =
      stat("clock", fmtClock(rig.get("t_s"))) +
      stat("true pH", fx(tru), bad(tru) ? "bad" : "good") +
      stat("firmware thinks", rep >= 0 ? fx(rep) : "no reading") +
      stat("water", `${fx(rig.get("level_frac") * 100, 0)} %`, rig.get("level_frac") < 0.25 ? "bad" : "") +
      stat("air", `${fx(rig.get("air_T"), 0)} °C, ${fx(rig.get("air_RH"), 0)} %RH`) +
      stat("pH-down used", `${Math.round(rig.get("acid_used_mL"))} mL`) +
      stat("energy", `${fx(rig.get("energy_Wh") / 1000, 2)} kWh`) +
      stat("growth index", fx(rig.get("biomass"), 2)) +
      stat("pump ran dry", `${fx(rig.get("dry_run_s") / 60, 0)} min`, rig.get("dry_run_s") > 0 ? "bad" : "");
    const al = rig.alarms();
    $("#sim-alarms").innerHTML = al.length ? al.map((a) => `<span class="alarm-chip">${a}</span>`).join(" ") : `<span class="small muted">no alarms</span>`;
    const s = rig.serial();
    if (s) {
      serialEl.textContent = (serialEl.textContent + s).split("\n").slice(-120).join("\n");
      serialEl.scrollTop = serialEl.scrollHeight;
    }
  };
  const job = { running: true, step(dt) {
    if (!paused) {
      const ok = pacer.advance(dt * speed * 1000, 10, () => {
        const t = rig.get("t_s");
        if (t - lastSample >= Math.min(300, Math.max(1, speed / 4))) { sample(); lastSample = t; }
      });
      $("#sim-rate").textContent = ok ? "" : "running slower than requested";
    }
    render();
  } };
  seg("#sim-speed", (b) => { speed = +b.dataset.s; rig.set("quantum_ms", quantum()); });
  $("#sim-pause").addEventListener("click", (e) => { paused = !paused; e.target.setAttribute("aria-pressed", String(paused)); e.target.textContent = paused ? "Resume" : "Pause"; });
  $("#sim-reset").addEventListener("click", boot);
  $("#sim-board").addEventListener("change", boot);
  $$("[data-act]").forEach((b) => b.addEventListener("click", () => {
    const perfect = $("#sim-board").checked;
    const vFor = (ph) => (perfect ? ph / 3.5 : (ph - REAL_BOARD.board_offset_pH) / REAL_BOARD.board_slope);
    switch (b.dataset.act) {
      case "refill": rig.set("refill"); break;
      case "evap": evapOn = !evapOn; rig.set("evap_L_day", evapOn ? 6 : 0.55); b.setAttribute("aria-pressed", String(evapOn)); break;
      case "buf7": rig.set("probe_where", 7); break;
      case "buf4": rig.set("probe_where", 4); break;
      case "tank": rig.set("probe_where", 0); break;
      case "stuck": rig.set("stuck_V", 3.2).set("probe_fault", 1); break;
      case "stuck69": rig.set("stuck_V", vFor(6.9)).set("probe_fault", 1); break;
      case "unplug": rig.set("probe_fault", 2); break;
      case "fix": rig.set("probe_fault", 0); break;
      case "dht": dhtOn = !dhtOn; rig.set("dht_fail_ppm", dhtOn ? 1e6 : 1e4); b.setAttribute("aria-pressed", String(dhtOn)); break;
    }
    render();
  }));
  $("#sim-board").checked = true;
  boot();
  render();
  sched.add(job, $("#simulator"));
}

// ---------------------------------------------------------------- 7 results

function fillFindings(f) {
  for (const el of $$("[data-f]")) {
    const v = el.dataset.f.split(".").reduce((o, k) => (o == null ? o : o[k]), f);
    if (v == null) continue;
    if (typeof v !== "number") { el.textContent = v; continue; }
    el.textContent = el.dataset.d ? v.toFixed(+el.dataset.d) : Number.isInteger(v) ? v.toLocaleString() : String(+v.toFixed(2));
  }
}

function results(grow, low, faults) {
  if (grow) {
    const s = grow.series;
    const chart = new LineChart($("#gr-chart"), {
      height: 300, xDomain: [0, 14], yDomain: [5.4, 6.8], xLabel: "day", yLabel: "pH", legend: $("#gr-legend"),
      bands: [{ y0: 5.5, y1: 6.5, label: "target 5.5-6.5" }], xFmt: (v) => String(Math.round(v)), tipX: (v) => `day ${v.toFixed(2)}`,
    });
    chart.set([
      { label: "reservoir pH", color: "--s-blue", x: s.t_h.map((t) => t / 24), y: s.ph },
      { label: "firmware reading", color: "--ink-2", dash: [4, 4], width: 1.2, x: s.t_h.map((t) => t / 24), y: s.ph_fw },
    ]);
    const g = grow.summary;
    $("#gr-stats").innerHTML = stat("pH-down used", `${(g.acid_mL / 1000).toFixed(2)} L`) + stat("corrections", g.corrections) +
      stat("light per day", `${g.lights_h_per_day} h`) + stat("pump per day", `${g.pump_h_per_day} h`) +
      stat("fan per day", `${g.fan_h_per_day} h`) + stat("alarms", g.alarms_seen ? g.alarms_seen : "none", "good") +
      stat("watchdog resets", g.wdt_resets, "good") + stat("growth index", fx(g.growth_index));
  }
  if (low) {
    const s = low.series;
    const chart = new LineChart($("#lw-chart"), {
      height: 240, xDomain: [0, 192], yDomain: [0, 100], yLabel: "water level %", xLabel: "hours", legend: $("#lw-legend"),
      bands: [{ y0: 0, y1: low.summary.pump_intake_pct, color: "--accent-soft", label: "pump intake in air" }],
      hlines: [{ y: 25, label: "LOW WATER alarm, pump locked out" }],
      xFmt: (v) => String(Math.round(v)), tipX: (v) => `hour ${v.toFixed(1)}`, yFmt: (v) => v.toFixed(0),
    });
    chart.set([
      { label: "water level", color: "--s-aqua", x: s.t_h, y: s.level, fmt: (v) => `${v.toFixed(0)} %` },
    ]);
  }
  if (faults) {
    $("#ft-table").innerHTML = Object.values(faults).map((r) => {
      const s = r.summary;
      return `<tr><td>${s.label}</td><td class="num">${s.acid_mL} mL</td><td class="num">${s.alarm_after_min} min</td>
        <td class="num">${s.lcd.map((l) => l.replace(/ /g, "&nbsp;")).join("<br>")}</td></tr>`;
    }).join("");
  }
}

// ------------------------------------------------------------------- main

async function main() {
  setupTheme();
  setupNav();
  setupParts();
  const data = {};
  await Promise.all(["findings", "grow14", "dosing_step", "faults", "low_water", "calibration", "responsiveness"].map(async (n) => {
    try { data[n] = await getJSON(`data/${n}.json`); } catch (e) { console.warn(e); }
  }));
  if (data.findings) fillFindings(data.findings);
  results(data.grow14, data.low_water, data.faults);

  let calc = null;
  try { await loadModule(); calc = await Rig.create(); } catch (e) {
    for (const id of ["#hero-panel", "#chemistry", "#sensor", "#dosing", "#timing", "#simulator"]) failNotice($(id), e);
    return;
  }
  chemistry(calc);
  sensor(calc, data.calibration);
  const tasks = [heroAndRig(), dosing(data.dosing_step), simulator(), timing(data.responsiveness)];
  for (const t of await Promise.allSettled(tasks)) if (t.status === "rejected") console.error(t.reason);
}

main();
