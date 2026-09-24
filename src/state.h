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
  int pace_mi = 247;
  int kw = 0;
  float speed_kmh = 0.0f;
  Gear gear = Gear::DRIVE;
  int max_draw_kw = 0;
  int max_regen_kw = 0;
  float trip_mi = 7.2f;
  float trip_kwh = 3.4f;
  float trip_eff = 2.1f;
  uint32_t trip_sec = 5520;
  char advisory[64] = "";
};

enum class BleLink : int {
  OFF = 0,
  SCANNING = 1,
  CONNECTED = 2,
};

extern VehicleState g_state;
extern BleLink g_ble;

// Stub data source: animated values until BLE OBD2 lands.
void state_stub_update(void);
// Advisory copy from live or stub values. Always runs.
void state_advisory_update(void);
// Session peaks (since power on). Call after kw updates.
void state_track_peaks(void);
