#pragma once
#include <Arduino.h>

// Shared vehicle data model. Every theme renders from this struct,
// so orientation and theme switches never touch data plumbing.
enum class Gear : int {
  UNKNOWN = 0,
  PARK = 1,
  REVERSE = 2,
  NEUTRAL = 3,
  DRIVE = 4,
};

struct VehicleState {
  // Unknown until the energy model learns the truck. Never a stub number.
  int pace_mi = -1;
  int kw = 0;
  float speed_kmh = 0.0f;
  // Raw battery % (015B), -1 until answered. Runs below the dash number.
  float soc_pct = -1.0f;
  // Displayed battery % (BECM 224845, validated vs dash), -1 until answered.
  float soc_disp = -1.0f;
  // Control-module voltage, -1 until 0142 answers.
  float mod_v = -1.0f;
  // Charge power in watts (BECM 22484E, validated), -1 until answered.
  long chg_w = -1;
  // Session energy in kWh, integrated from charge watts this session.
  float chg_kwh = 0.0f;
  // AC input volts (charger 22485E), -1 until answered.
  float chg_ac_v = -1.0f;
  // Charge bar scale in kW: 20 on AC, 200 on DC/unknown.
  int chg_max_kw = 200;
  // Unknown until a live shifter reading (or the offline demo) says otherwise.
  // Never default to DRIVE: that invents a mode the truck never reported.
  Gear gear = Gear::UNKNOWN;
  int max_draw_kw = 0;
  int max_regen_kw = 0;
  // Trip accumulators start at zero and fill from live data only.
  float trip_mi = 0.0f;
  float trip_kwh = 0.0f;
  float trip_eff = 0.0f;
  uint32_t trip_sec = 0;
  char advisory[64] = "";
};

enum class BleLink : int {
  OFF = 0,
  SCANNING = 1,
  CONNECTED = 2,
};

extern VehicleState g_state;
extern BleLink g_ble;

// Charging when the shifter says PARK or real charge power flows.
// Power-triggered so a validated charge session shows without a shifter PID.
inline bool vehicle_charging(void) {
  return g_state.gear == Gear::PARK || g_state.chg_w > 500;
}

// Usable pack energy, kWh. Extended-range truck; VIN trim decode (TODO)
// will select this automatically.
#define RR_USABLE_KWH 131.0f

// Stub data source: animated values until BLE OBD2 lands.
void state_stub_update(void);
// Real trip clock, every tick: wall-time seconds plus speed-integrated miles
// while linked (dead reckoning, no odometer yet). Never mock distance.
void state_clock_update(void);
// Call once live data arrives. The stub then only zeroes the meter and
// freezes the last live shifter/speed instead of demoing phantom modes.
void state_set_live_seen(void);
// Feed each fresh charge-power sample (watts, >= 0). Keeps ~1 min of
// samples for the normalized charge rate.
void charge_note_power(long w);
// Windowed charge rate in kW, -1 when nothing recorded yet.
float charge_kw_avg(void);
// Charge target for the current SoC: 80 below eighty, else 100. -1 unknown.
int charge_target_pct(void);
// Hours to target at the windowed rate. -1 unknown, 0 already there.
float charge_eta_hours(void);
// Advisory copy from live or stub values. Always runs.
void state_advisory_update(void);
// Session peaks (since power on). Call after kw updates.
void state_track_peaks(void);
