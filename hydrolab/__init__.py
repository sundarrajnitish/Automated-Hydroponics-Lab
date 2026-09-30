"""hydrolab - run the Hydroponinator firmware on a simulated rig.

The simulator is C++ (sim/): a virtual Arduino Mega wired to a model of the
reservoir, room and crop, with the firmware in firmware/Hydroponics compiled
into it unmodified. This package loads it with ctypes, scripts experiments
and turns the results into numbers and figures.

Author: Nitish Sundarraj
"""
from .native import Rig, chem
from .scenarios import run_all

__all__ = ["Rig", "chem", "run_all"]
__version__ = "1.0.0"
