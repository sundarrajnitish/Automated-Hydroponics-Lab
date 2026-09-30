// sketch.h - handle on the firmware sketch compiled into the simulator.
#pragma once
#include <stdint.h>

namespace sim {

struct SketchIface {
  const char* name;
  void (*reset)();        // re-run the sketch's global initialisers
  void (*setup)();
  void (*loop)();
  double (*reported_ph)();  // what the firmware believes the pH is
  int (*program)();
  int (*alarms)();
  int (*dose_state)();
  double (*gain)();
  double (*dose_ms_today)();
  int (*screen)();
};

extern const SketchIface FIRMWARE;   // firmware/Hydroponics, unmodified

}  // namespace sim
