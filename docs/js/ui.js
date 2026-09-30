// ui.js - the virtual front panel: HD44780 LCD, 4x4 membrane keypad, load LEDs.
import { LOADS } from "./sim.js";

export class LcdView {
  constructor(el, { big = false } = {}) {
    el.classList.add("lcd");
    if (big) el.classList.add("big");
    el.setAttribute("role", "img");
    const glass = document.createElement("div");
    glass.className = "lcd-glass";
    this.cells = [];
    for (let r = 0; r < 2; r++) {
      const row = document.createElement("div");
      row.className = "lcd-row";
      for (let c = 0; c < 16; c++) {
        const cell = document.createElement("span");
        cell.className = "lcd-cell";
        row.appendChild(cell);
        this.cells.push(cell);
      }
      glass.appendChild(row);
    }
    el.appendChild(glass);
    this.el = el;
    this.prev = new Uint8Array(32).fill(0);
  }
  update(bytes) {
    let text = "";
    for (let i = 0; i < 32; i++) {
      const b = bytes[i];
      const printable = b >= 32 && b < 127;
      const ch = printable ? String.fromCharCode(b) : "▒";
      text += ch;
      if (b === this.prev[i]) continue;
      this.prev[i] = b;
      const cell = this.cells[i];
      cell.textContent = ch === " " ? "" : ch;
      cell.classList.toggle("junk", !printable);
    }
    this.el.setAttribute("aria-label", `LCD: ${text.slice(0, 16).trim()} / ${text.slice(16).trim()}`);
    return text;
  }
}

const KEYS = ["1", "2", "3", "A", "4", "5", "6", "B", "7", "8", "9", "C", "*", "0", "#", "D"];

export class KeypadView {
  constructor(el, onKey) {
    el.classList.add("keypad");
    el.setAttribute("role", "group");
    el.setAttribute("aria-label", "4x4 keypad");
    this.btn = {};
    for (const k of KEYS) {
      const b = document.createElement("button");
      b.type = "button";
      b.className = "key" + (/[A-D]/.test(k) ? " fn" : "");
      b.textContent = k;
      b.setAttribute("aria-label", `key ${k}`);
      b.addEventListener("click", () => onKey(k));
      el.appendChild(b);
      this.btn[k] = b;
    }
  }
  flash(k) {
    const b = this.btn[k];
    if (!b) return;
    b.classList.add("down");
    setTimeout(() => b.classList.remove("down"), 120);
  }
}

const LOAD_LABEL = {
  pump: "pump", ph_down: "pH-down", nutrient: "nutrient", grow_light: "grow light",
  fluo_light: "fluoro", fan: "fan", humidifier: "humidifier",
};

// A complete front panel bound to one simulated rig.
export class FrontPanel {
  constructor(root, rig, { title = "", big = false, onKey = null } = {}) {
    this.rig = rig;
    root.classList.add("controller");
    root.innerHTML = `
      <div class="ctl-top"><span>${title}</span><b class="ctl-clock"></b></div>
      <div class="ctl-body">
        <div class="ctl-lcd"></div>
        <div class="ctl-keys"></div>
        <div class="loads"></div>
      </div>`;
    this.lcd = new LcdView(root.querySelector(".ctl-lcd"), { big });
    this.keypad = new KeypadView(root.querySelector(".ctl-keys"), (k) => this.press(k));
    this.clock = root.querySelector(".ctl-clock");
    const loads = root.querySelector(".loads");
    this.leds = {};
    for (const n of LOADS) {
      const s = document.createElement("span");
      s.className = "load" + (n === "ph_down" ? " warn" : "");
      s.innerHTML = `<i class="led"></i>${LOAD_LABEL[n]}`;
      loads.appendChild(s);
      this.leds[n] = s;
    }
    this.onKey = onKey;
    root.tabIndex = 0;
    root.addEventListener("keydown", (ev) => {
      const k = ev.key.toUpperCase();
      if (/^[0-9A-D*#]$/.test(k)) { ev.preventDefault(); this.press(k); }
    });
  }
  press(k) {
    this.rig.key(k, 150);
    this.keypad.flash(k);
    if (this.onKey) this.onKey(k);
  }
  setRig(rig) { this.rig = rig; this.lcd.prev.fill(0); }
  update(clockText) {
    this.lcd.update(this.rig.lcd());
    const l = this.rig.loads();
    for (const n of LOADS) this.leds[n].classList.toggle("on", l[n]);
    if (clockText !== undefined) this.clock.textContent = clockText;
    return l;
  }
}
