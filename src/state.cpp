#include "state.h"

#include "advisor.h"

VehicleState g_state;
BleLink g_ble = BleLink::OFF;

static bool s_live_seen = false;

void state_set_live_seen(void) {
  s_live_seen = true;
}

void state_stub_update(void) {
  if (s_live_seen) {
    // Link dropped after live data: hold the last live shifter and speed,
    // rest the meter at zero. Never demo modes the truck didn't report.
    g_state.kw = 0;
    return;
  }
  static int tick = 0;
  tick++;

  g_state.pace_mi = 247 + ((tick / 2) % 7) - 3;

  // Offline the meter rests at true zero: a dead link must never show
  // stale or swept values. Live data takes over on connect.
  g_state.kw = 0;

  g_state.trip_mi += 0.005f;
  g_state.trip_sec += 1;
  // Stub shifter: 10 s of PARK every minute so charge mode demos offline.
  g_state.gear = ((tick % 240) >= 200) ? Gear::PARK : Gear::DRIVE;
  // Stub speed: 15 s cruise at 101 km/h so the aero tip fires offline.
  g_state.speed_kmh = ((tick % 120) < 60) ? 0.0f : 101.0f;
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
  advisor_pick(g_state.kw, g_state.speed_kmh, g_state.gear == Gear::PARK, tick,
               g_state.advisory, sizeof(g_state.advisory));
}
