#include "state.h"

#include "advisor.h"

VehicleState g_state;
BleLink g_ble = BleLink::OFF;

static bool s_live_seen = false;

void state_set_live_seen(void) {
  s_live_seen = true;
}

// ~1 min window at the BECM cadence (one sample per ~8 s poll tick).
static long s_chg_ring[8] = {0};
static int s_chg_idx = 0;
static int s_chg_n = 0;

void charge_note_power(long w) {
  if (w < 0) return;
  s_chg_ring[s_chg_idx] = w;
  s_chg_idx = (s_chg_idx + 1) % 8;
  if (s_chg_n < 8) s_chg_n++;
  // Session energy: integrate validated watts while power flows. A new
  // session (power returning after none) restarts the total.
  static uint32_t last_ms = 0;
  uint32_t now = millis();
  if (w > 0) {
    if (last_ms == 0) {
      g_state.chg_kwh = 0.0f;
    } else {
      g_state.chg_kwh += (float)w * (float)(now - last_ms) / 3600000.0f;
    }
    last_ms = now;
  } else {
    last_ms = 0;
  }
}

float charge_kw_avg(void) {
  if (s_chg_n == 0) return -1.0f;
  long sum = 0;
  for (int i = 0; i < s_chg_n; i++) sum += s_chg_ring[i];
  return (float)sum / (1000.0f * s_chg_n);
}

static float charge_soc(void) {
  return g_state.soc_disp >= 0.0f ? g_state.soc_disp : g_state.soc_pct;
}

int charge_target_pct(void) {
  float soc = charge_soc();
  if (soc < 0.0f) return -1;
  return soc < 80.0f ? 80 : 100;
}

float charge_eta_hours(void) {
  float soc = charge_soc();
  float kw = charge_kw_avg();
  if (soc < 0.0f || kw <= 0.0f) return -1.0f;
  int target = charge_target_pct();
  if (soc >= (float)target) return 0.0f;
  return ((float)target - soc) / 100.0f * RR_USABLE_KWH / kw;
}

void state_clock_update(void) {
  static uint32_t last_ms = 0;
  static uint32_t acc_ms = 0;
  uint32_t now = millis();
  if (last_ms == 0) {
    last_ms = now;
    return;
  }
  uint32_t dt = now - last_ms;
  last_ms = now;
  // Trip time is drive time: parked time doesn't count. Unknown still
  // counts (no shifter PID yet), only PARK freezes the clock.
  if (g_state.gear == Gear::PARK) return;
  acc_ms += dt;
  while (acc_ms >= 1000) {
    acc_ms -= 1000;
    g_state.trip_sec++;
  }
  if (g_ble == BleLink::CONNECTED) {
    g_state.trip_mi += (double)g_state.speed_kmh * (double)dt / 3600000.0 * 0.621371;
  }
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
  // Trip time and distance belong to state_clock_update now.
  g_state.kw = 0;
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
  // Wall-clock tick so copy rotation keeps its 5 s pace at any UI rate.
  int tick = (int)(millis() / 250);
  // In a validated charge session the marquee reports real charge power,
  // not the drive meter (which rests at zero while parked).
  float ekw = (float)g_state.kw;
  if (vehicle_charging() && g_state.chg_w > 0) ekw = g_state.chg_w / 1000.0f;
  advisor_pick(ekw, g_state.speed_kmh, vehicle_charging(), tick,
               g_state.advisory, sizeof(g_state.advisory));
}
