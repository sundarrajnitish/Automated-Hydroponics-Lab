// chart.js - a small canvas line chart with a crosshair tooltip.
// Colours are CSS custom properties, resolved at draw time, so charts follow
// the light/dark theme.

const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim() || name;
const charts = new Set();
function redrawAll() { for (const c of charts) c.draw(); }
if (window.matchMedia) matchMedia("(prefers-color-scheme: dark)").addEventListener?.("change", redrawAll);
new MutationObserver(redrawAll).observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });

function niceStep(span, target) {
  const raw = span / Math.max(1, target);
  const mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const n = raw / mag;
  return (n < 1.5 ? 1 : n < 3 ? 2 : n < 7 ? 5 : 10) * mag;
}
function ticks(lo, hi, target) {
  const st = niceStep(hi - lo, target);
  const out = [];
  for (let v = Math.ceil(lo / st - 1e-9) * st; v <= hi + st * 1e-9; v += st) out.push(+v.toFixed(10));
  return { values: out, step: st };
}
function nearest(xs, x) {
  let lo = 0, hi = xs.length - 1;
  if (hi < 0) return -1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (xs[m] < x) lo = m; else hi = m; }
  return Math.abs(xs[lo] - x) <= Math.abs(xs[hi] - x) ? lo : hi;
}

export class LineChart {
  constructor(el, opts = {}) {
    this.el = el;
    this.o = Object.assign({ height: 260, xDomain: null, yDomain: null, bands: [], hlines: [], xFmt: (v) => String(+v.toFixed(2)),
      yFmt: (v) => String(+v.toFixed(2)), tipX: null, xLabel: "", yLabel: "", pad: { l: 46, r: 14, t: 24, b: 34 } }, opts);
    el.classList.add("chart");
    this.canvas = document.createElement("canvas");
    this.canvas.setAttribute("role", "img");
    if (opts.aria) this.canvas.setAttribute("aria-label", opts.aria);
    this.tip = document.createElement("div");
    this.tip.className = "tip";
    this.tip.hidden = true;
    el.append(this.canvas, this.tip);
    this.series = [];
    this.hoverX = null;
    this.pending = false;
    new ResizeObserver(() => this.draw()).observe(el);
    this.canvas.addEventListener("pointermove", (e) => this.hover(e));
    this.canvas.addEventListener("pointerleave", () => { this.hoverX = null; this.tip.hidden = true; this.draw(); });
    charts.add(this);
    if (opts.legend) this.legendEl = opts.legend;
  }
  set(series) { this.series = series; this.renderLegend(); this.request(); return this; }
  request() {
    if (this.pending) return;
    this.pending = true;
    requestAnimationFrame(() => { this.pending = false; this.draw(); });
  }
  renderLegend() {
    if (!this.legendEl) return;
    this.legendEl.innerHTML = "";
    this.legendEl.classList.add("legend");
    for (const s of this.series) {
      if (s.noLegend) continue;
      const sp = document.createElement("span");
      const i = document.createElement("i");
      i.style.background = css(s.color);
      if (s.dash) i.style.background = `repeating-linear-gradient(90deg, ${css(s.color)} 0 4px, transparent 4px 7px)`;
      sp.append(i, document.createTextNode(s.label));
      this.legendEl.appendChild(sp);
    }
    for (const b of this.o.bands) {
      if (!b.label) continue;
      const sp = document.createElement("span");
      const i = document.createElement("i");
      i.className = "area";
      i.style.background = css(b.color || "--band");
      sp.append(i, document.createTextNode(b.label));
      this.legendEl.appendChild(sp);
    }
  }
  domain() {
    let [x0, x1] = this.o.xDomain || [Infinity, -Infinity];
    let [y0, y1] = this.o.yDomain || [Infinity, -Infinity];
    if (!this.o.xDomain || !this.o.yDomain) {
      for (const s of this.series) {
        for (let i = 0; i < s.x.length; i++) {
          const y = s.y[i];
          if (y == null || !isFinite(y)) continue;
          if (!this.o.xDomain) { x0 = Math.min(x0, s.x[i]); x1 = Math.max(x1, s.x[i]); }
          if (!this.o.yDomain) { y0 = Math.min(y0, y); y1 = Math.max(y1, y); }
        }
      }
      if (!this.o.yDomain && isFinite(y0)) {
        const m = (y1 - y0) * 0.08 || 0.5;
        y0 -= m; y1 += m;
        if (this.o.yMin != null) y0 = Math.min(y0, this.o.yMin);
        if (this.o.yMax != null) y1 = Math.max(y1, this.o.yMax);
      }
    }
    if (!isFinite(x0)) { x0 = 0; x1 = 1; }
    if (!isFinite(y0)) { y0 = 0; y1 = 1; }
    if (x1 === x0) x1 = x0 + 1;
    if (y1 === y0) y1 = y0 + 1;
    return { x0, x1, y0, y1 };
  }
  draw() {
    const w = this.el.clientWidth;
    if (!w) return;
    const h = this.o.height;
    const dpr = window.devicePixelRatio || 1;
    const cv = this.canvas;
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
      cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
      cv.style.height = h + "px";
    }
    const g = cv.getContext("2d");
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const P = this.o.pad;
    const pw = w - P.l - P.r, ph = h - P.t - P.b;
    const d = this.domain();
    this.geom = { P, pw, ph, d, w, h };
    const X = (x) => P.l + ((x - d.x0) / (d.x1 - d.x0)) * pw;
    const Y = (y) => P.t + (1 - (y - d.y0) / (d.y1 - d.y0)) * ph;
    const ink = css("--muted"), grid = css("--grid");
    g.font = `11px ${css("--f-mono")}`;

    for (const b of this.o.bands) {
      g.fillStyle = css(b.color || "--band");
      const a = Y(Math.min(b.y1, d.y1)), z = Y(Math.max(b.y0, d.y0));
      if (z > a) g.fillRect(P.l, a, pw, z - a);
    }
    const yt = ticks(d.y0, d.y1, Math.max(3, Math.floor(ph / 42)));
    g.strokeStyle = grid; g.lineWidth = 1; g.fillStyle = ink; g.textAlign = "right"; g.textBaseline = "middle";
    for (const v of yt.values) {
      const y = Math.round(Y(v)) + 0.5;
      g.beginPath(); g.moveTo(P.l, y); g.lineTo(P.l + pw, y); g.stroke();
      g.fillText(this.o.yFmt(v), P.l - 6, y);
    }
    const xt = ticks(d.x0, d.x1, Math.max(3, Math.floor(pw / 90)));
    g.textAlign = "center"; g.textBaseline = "top";
    for (const v of xt.values) {
      const x = Math.round(X(v)) + 0.5;
      g.beginPath(); g.moveTo(x, P.t + ph); g.lineTo(x, P.t + ph + 4); g.stroke();
      g.fillText(this.o.xFmt(v, xt.step), x, P.t + ph + 7);
    }
    if (this.o.xLabel) { g.textAlign = "right"; g.fillText(this.o.xLabel, P.l + pw, P.t + ph + 20); }
    if (this.o.yLabel) { g.textAlign = "left"; g.textBaseline = "top"; g.fillText(this.o.yLabel, 4, 0); }

    for (const hl of this.o.hlines) {
      if (hl.y < d.y0 || hl.y > d.y1) continue;
      const y = Math.round(Y(hl.y)) + 0.5;
      g.strokeStyle = css(hl.color || "--muted"); g.setLineDash([4, 3]);
      g.beginPath(); g.moveTo(P.l, y); g.lineTo(P.l + pw, y); g.stroke(); g.setLineDash([]);
      if (hl.label) { g.fillStyle = css(hl.color || "--muted"); g.textAlign = "left"; g.textBaseline = "bottom"; g.fillText(hl.label, P.l + 6, y - 3); }
    }

    g.save();
    g.beginPath(); g.rect(P.l, P.t - 2, pw, ph + 4); g.clip();
    for (const s of this.series) {
      const n = s.x.length;
      if (!n) continue;
      g.strokeStyle = css(s.color);
      g.lineWidth = s.width || 2;
      g.globalAlpha = s.alpha ?? 1;
      g.setLineDash(s.dash || []);
      g.lineJoin = "round";
      g.beginPath();
      let pen = false, prevY = 0;
      // thin very long series to ~2 points per pixel
      const stride = Math.max(1, Math.floor(n / (pw * 2)));
      for (let i = 0; i < n; i += (i + stride < n ? stride : Math.max(1, n - 1 - i))) {
        const yv = s.y[i];
        if (yv == null || !isFinite(yv)) { pen = false; continue; }
        const x = X(s.x[i]), y = Y(yv);
        if (!pen) { g.moveTo(x, y); pen = true; }
        else if (s.step) { g.lineTo(x, prevY); g.lineTo(x, y); }
        else g.lineTo(x, y);
        prevY = y;
        if (i === n - 1) break;
      }
      g.stroke();
      if (s.endDot && n) {
        const yv = s.y[n - 1];
        if (yv != null) { g.fillStyle = css(s.color); g.beginPath(); g.arc(X(s.x[n - 1]), Y(yv), 3.5, 0, 7); g.fill(); }
      }
      g.globalAlpha = 1;
      g.setLineDash([]);
    }
    if (this.o.marker) {
      const m = this.o.marker;
      g.fillStyle = css(m.color || "--ink");
      g.beginPath(); g.arc(X(m.x), Y(m.y), 5, 0, 7); g.fill();
    }
    g.restore();

    if (this.hoverX != null) {
      const x = Math.round(X(this.hoverX)) + 0.5;
      g.strokeStyle = css("--ink-2"); g.globalAlpha = .5;
      g.beginPath(); g.moveTo(x, P.t); g.lineTo(x, P.t + ph); g.stroke(); g.globalAlpha = 1;
      for (const s of this.series) {
        const i = nearest(s.x, this.hoverX);
        if (i < 0 || s.y[i] == null) continue;
        g.fillStyle = css("--surface"); g.strokeStyle = css(s.color); g.lineWidth = 2;
        g.beginPath(); g.arc(X(s.x[i]), Y(s.y[i]), 4, 0, 7); g.fill(); g.stroke();
      }
    }
  }
  hover(e) {
    if (!this.geom) return;
    const r = this.canvas.getBoundingClientRect();
    const { P, pw, d } = this.geom;
    const px = e.clientX - r.left;
    if (px < P.l || px > P.l + pw) { this.hoverX = null; this.tip.hidden = true; this.draw(); return; }
    this.hoverX = d.x0 + ((px - P.l) / pw) * (d.x1 - d.x0);
    const rows = [];
    for (const s of this.series) {
      if (s.noTip) continue;
      const i = nearest(s.x, this.hoverX);
      if (i < 0 || s.y[i] == null) continue;
      rows.push(`<div><i style="background:${css(s.color)}"></i>${s.label}: <b>${(s.fmt || this.o.yFmt)(s.y[i])}</b></div>`);
    }
    const head = this.o.tipX ? this.o.tipX(this.hoverX) : this.o.xFmt(this.hoverX);
    this.tip.innerHTML = `<div class="muted">${head}</div>${rows.join("")}`;
    this.tip.hidden = false;
    const tw = this.tip.offsetWidth;
    let left = px + 14;
    if (left + tw > r.width) left = px - tw - 14;
    this.tip.style.left = Math.max(0, left) + "px";
    this.tip.style.top = "8px";
    this.draw();
  }
}
