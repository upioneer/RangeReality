#include "state.h"

VehicleState g_state;
BleLink g_ble = BleLink::OFF;

void state_stub_update(void) {
  static int tick = 0;
  tick++;

  g_state.pace_mi = 247 + ((tick / 2) % 7) - 3;

  int phase = tick % 40;
  g_state.kw = (phase < 20) ? (phase * 4 - 20) : ((40 - phase) * 4 - 20);

  g_state.trip_mi += 0.005f;
  g_state.trip_sec += 1;
  // Stub shifter: 10 s of PARK every minute so charge mode demos offline.
  g_state.gear = ((tick % 240) >= 200) ? Gear::PARK : Gear::DRIVE;
}

void state_track_peaks(void) {
  // Drive mode semantics only. Charge input gets its own peak later.
  if (g_state.gear == Gear::PARK) return;
  if (g_state.kw > g_state.max_draw_kw) g_state.max_draw_kw = g_state.kw;
  if (g_state.kw < g_state.max_regen_kw) g_state.max_regen_kw = g_state.kw;
}

void state_advisory_update(void) {
  static int tick = 0;
  tick++;
  if (g_state.kw > 45 && g_state.gear != Gear::PARK) {
    snprintf(g_state.advisory, sizeof(g_state.advisory), "Watt in Tarnation!");
  } else if ((tick % 20) < 10) {
    snprintf(g_state.advisory, sizeof(g_state.advisory), "+12mi by slowing to 63mph");
  } else {
    snprintf(g_state.advisory, sizeof(g_state.advisory), "Headwind penalty ~8 mi");
  }
}
