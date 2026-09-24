#pragma once

// Pure meter math: no Arduino, LVGL, or BLE dependencies so the park
// vs drive behavior stays host-testable. Widgets only draw what these
// functions return.

// Calibrated to Lightning extended range ratings: 580 hp is about 433 kW,
// ceiling set to 440. Regen 220 kW per owner reports, charge 200 kW estimate.
// Both confirm at truck logging.
#define METER_MAX_DRAW_KW 440
#define METER_MAX_REGEN_KW 220
#define METER_MAX_CHARGE_KW 200

enum class MeterMode {
  DRIVE,
  CHARGE,
};

// Park selects the full-bar charge meter. Anything else, including
// reverse and neutral, stays on the split drive meter.
inline MeterMode meter_mode(bool parked) {
  return parked ? MeterMode::CHARGE : MeterMode::DRIVE;
}

// Charge fill width in pixels, clamped to [0, full_w].
inline int meter_charge_fill(int kw, int full_w) {
  if (kw <= 0) return 0;
  if (kw >= METER_MAX_CHARGE_KW) return full_w;
  return kw * full_w / METER_MAX_CHARGE_KW;
}

struct MeterDriveFill {
  int neg_w;  // regen extent, pixels left of center
  int pos_w;  // draw extent, pixels right of center
};

// Split drive fill widths in pixels, each clamped to [0, half_w].
inline MeterDriveFill meter_drive_fill(int kw, int half_w) {
  MeterDriveFill f = {0, 0};
  if (kw > 0) {
    int c = kw > METER_MAX_DRAW_KW ? METER_MAX_DRAW_KW : kw;
    f.pos_w = c * half_w / METER_MAX_DRAW_KW;
  } else if (kw < 0) {
    int m = -kw > METER_MAX_REGEN_KW ? METER_MAX_REGEN_KW : -kw;
    f.neg_w = m * half_w / METER_MAX_REGEN_KW;
  }
  return f;
}
