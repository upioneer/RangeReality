#pragma once
#include <stdio.h>

// Pure advisory copy: inputs are live values, no I/O, host-testable.
// The aero curve is a placeholder until truck calibration measures it.
inline void advisor_pick(float kw, float speed_kmh, bool parked, int tick, char *out, int n) {
  if (parked) {
    if (kw > 1) {
      snprintf(out, n, "Charging +%d kW", (int)kw);
    } else {
      snprintf(out, n, "Ready to charge");
    }
    return;
  }
  if (kw > 45) {
    snprintf(out, n, "Watt in Tarnation!");
    return;
  }
  if (kw < -5) {
    snprintf(out, n, "Regen banking energy");
    return;
  }
  if (speed_kmh > 80.0f) {
    float expected = 0.0032f * speed_kmh * speed_kmh;
    if (expected > 1.0f && kw > expected * 1.25f) {
      float ratio = 101.0f * 101.0f / (speed_kmh * speed_kmh);
      int save = (int)((1.0f - ratio) * 100.0f);
      if (save < 0) save = 0;
      snprintf(out, n, "Aero heavy: 63 mph saves ~%d%%", save);
      return;
    }
  }
  if ((tick % 20) < 10) {
    snprintf(out, n, "+12mi by slowing to 63mph");
  } else {
    snprintf(out, n, "Headwind penalty ~8 mi");
  }
}
