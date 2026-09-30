"""README figures (matplotlib)."""
from __future__ import annotations

from pathlib import Path

BLUE, AQUA, ORANGE = "#2a78d6", "#1baf7a", "#eb6834"
INK, MUTED, GRID, BAND = "#0b0b0b", "#52514e", "#e4e2dc", "#eef4ea"


def _style(ax, title, xlabel, ylabel):
    ax.set_title(title, loc="left", fontsize=12, color=INK, pad=10, fontweight="bold")
    ax.set_xlabel(xlabel, color=MUTED)
    ax.set_ylabel(ylabel, color=MUTED)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(GRID)
    ax.tick_params(colors=MUTED)


def make_figures(res: dict, out: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    out.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.size": 10, "font.family": "DejaVu Sans"})

    # 1. two weeks of reservoir pH
    g = res["grow"]
    s = g["series"]
    fig, ax = plt.subplots(figsize=(9, 3.8), dpi=150)
    ax.axhspan(5.5, 6.5, color=BAND, zorder=0)
    ax.text(13.9, 5.53, "target band 5.5-6.5", color=MUTED, va="bottom", ha="right", fontsize=9)
    ax.plot([t / 24 for t in s["t_h"]], s["ph"], color=BLUE, lw=2)
    ax.set_ylim(5.4, 6.8)
    ax.axvline(7.125, color=MUTED, lw=1, ls=(0, (4, 3)))
    ax.text(7.2, 6.72, "weekly top-up with tap water", color=MUTED, fontsize=9, va="top")
    sm = g["summary"]
    _style(ax, f"Reservoir pH over {sm['days']:.0f} days: {sm['in_band_pct']}% in band", "day", "pH")
    fig.tight_layout()
    fig.savefig(out / "grow14_ph.png")
    plt.close(fig)

    # 2. correcting a hard-water fill
    d = res["dosing"]
    s = d["series"]
    fig, ax = plt.subplots(figsize=(9, 3.8), dpi=150)
    ax.axhspan(5.5, 6.5, color=BAND, zorder=0)
    ax.plot(s["t_min"], s["ph_zone"], color=AQUA, lw=1, alpha=.55, label="where the acid lands")
    ax.plot(s["t_min"], s["ph"], color=BLUE, lw=2.2, label="reservoir (bulk)")
    for t, on in zip(s["t_min"], s["dosing"]):
        if on:
            ax.plot([t, t], [5.42, 5.5], color=ORANGE, lw=1.2)
    ax.set_ylim(5.4, 8.1)
    ax.set_xlim(0, 90)
    ax.text(89, 5.52, "orange ticks: pH-down pump running", color=MUTED, ha="right", fontsize=9)
    sm = d["summary"]
    _style(ax, f"Dose-and-wait from pH {sm['start_ph']}: in band after {sm['minutes_to_band']} min, no overshoot",
           "minutes", "pH")
    ax.legend(frameon=False, loc="upper right", fontsize=9)
    fig.tight_layout()
    fig.savefig(out / "dosing_step.png")
    plt.close(fig)

    # 3. calibration
    c = res["calibration"]
    fig, ax = plt.subplots(figsize=(6.2, 5), dpi=150)
    ax.plot([4, 9], [4, 9], color=GRID, lw=1.5)
    for key, col in (("default", ORANGE), ("calibrated", BLUE)):
        r = c[key]
        pts = [(a, b) for a, b in zip(r["true"], r["reported"]) if b is not None]
        ax.plot([p[0] for p in pts], [p[1] for p in pts], color=col, lw=2,
                label=f'{r["summary"]["label"]}: max error {r["summary"]["max_error"]}')
    _style(ax, "Reported vs true pH, same amplifier board", "true pH", "reported pH")
    ax.legend(frameon=False, loc="upper left", fontsize=9)
    fig.tight_layout()
    fig.savefig(out / "calibration.png")
    plt.close(fig)
