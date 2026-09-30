"""python -m hydrolab [--quick] [--no-figures]

Runs every experiment, writes results/findings.json, the website's data
files (docs/data/*.json) and the README figures (media/figures/*.png).
"""
from __future__ import annotations

import argparse
import json
import time

from .native import ROOT
from .scenarios import run_all


def _thin(series: dict, every: int) -> dict:
    return {k: v[::every] for k, v in series.items()}


def summaries(res: dict) -> dict:
    return {
        "grow": res["grow"]["summary"],
        "dosing": res["dosing"]["summary"],
        "low_water": res["low_water"]["summary"],
        "responsiveness": res["responsiveness"]["summary"],
        "faults": {k: v["summary"] for k, v in res["faults"].items()},
        "calibration": {k: v["summary"] for k, v in res["calibration"].items()},
    }


def main() -> None:
    ap = argparse.ArgumentParser(prog="hydrolab")
    ap.add_argument("--quick", action="store_true", help="3-day grow instead of 14 (for CI)")
    ap.add_argument("--no-figures", action="store_true")
    ap.add_argument("--processes", type=int, default=None)
    args = ap.parse_args()

    t = time.time()
    res = run_all(processes=args.processes, quick=args.quick)
    print(f"simulations finished in {time.time() - t:.0f} s")
    s = summaries(res)

    if not args.quick:
        (ROOT / "results").mkdir(exist_ok=True)
        (ROOT / "results" / "findings.json").write_text(json.dumps(s, indent=2) + "\n")
        data = ROOT / "docs" / "data"
        data.mkdir(parents=True, exist_ok=True)
        dump = lambda name, obj: (data / name).write_text(json.dumps(obj, separators=(",", ":")))
        dump("findings.json", s)
        dump("grow14.json", dict(summary=res["grow"]["summary"], series=_thin(res["grow"]["series"], 2)))
        dump("dosing_step.json", res["dosing"])
        dump("faults.json", res["faults"])
        dump("low_water.json", res["low_water"])
        dump("calibration.json", res["calibration"])
        dump("responsiveness.json", res["responsiveness"])
        if not args.no_figures:
            from .figures import make_figures
            make_figures(res, ROOT / "media" / "figures")

    print(json.dumps(s, indent=2))


if __name__ == "__main__":
    main()
