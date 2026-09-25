#include "themes.h"
#include "state.h"
#include "meter.h"

// Standard theme, both orientations. Portrait follows
// project_details/assets/mock/standard.jpg. Landscape is the
// original bringup layout.

struct StdUi {
  lv_obj_t *trip = nullptr;
  lv_obj_t *hero = nullptr;
  lv_obj_t *unit = nullptr;
  lv_obj_t *advisory = nullptr;
  lv_obj_t *pill = nullptr;
  lv_obj_t *kw_num = nullptr;
  lv_obj_t *canvas = nullptr;
  lv_color_t *cbuf = nullptr;
  int cw = 0;
  int chh = 0;
  lv_obj_t *lab1 = nullptr;
  lv_obj_t *lab2 = nullptr;
  lv_obj_t *val1 = nullptr;
  lv_obj_t *val2 = nullptr;
  lv_obj_t *canvas2 = nullptr;
  lv_color_t *cbuf2 = nullptr;
  int cw2 = 0;
  int chh2 = 0;
  lv_obj_t *capA = nullptr;
  lv_obj_t *capB = nullptr;
  lv_obj_t *ble = nullptr;
  lv_obj_t *maxl = nullptr;
  lv_obj_t *maxr = nullptr;
};

static StdUi s_ui;
static Orientation s_orient = Orientation::PORTRAIT;
// One gauge buffer shared by both orientations: only one canvas is alive.
// Sized above the widest row drawn; charge rows sit inset from both walls.
static lv_color_t s_gauge_buf[356 * 26];
static lv_color_t s_gauge_buf2[356 * 26];

void standard_update(void);

static void ble_paint(void) {
  if (s_ui.ble == nullptr) return;
  // Red reads as not connected, neutral grey while scanning, blue linked.
  lv_color_t c = lv_color_make(0xE0, 0x30, 0x10);
  if (g_ble == BleLink::CONNECTED) {
    c = lv_color_make(0x2E, 0x9B, 0xFF);
  } else if (g_ble == BleLink::SCANNING) {
    c = lv_color_make(0xB0, 0xB0, 0xB0);
  }
  lv_obj_set_style_text_color(s_ui.ble, c, LV_PART_MAIN);
}

static lv_obj_t *mk_ble(lv_obj_t *parent) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_make(0xE0, 0x30, 0x10), LV_PART_MAIN);
  return l;
}

static void strip(lv_draw_rect_dsc_t *dsc, int x, int y, int sw, int h, lv_color_t c) {
  dsc->bg_color = c;
  lv_canvas_draw_rect(s_ui.canvas, x, y, sw, h, dsc);
}

static void soc_draw(float soc) {
  if (s_ui.canvas2 == nullptr) return;
  int w = s_ui.cw2;
  int h = s_ui.chh2;
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_opa = LV_OPA_COVER;
  dsc.bg_color = lv_color_make(0x20, 0x20, 0x20);
  dsc.border_opa = LV_OPA_TRANSP;
  dsc.radius = 0;
  lv_canvas_draw_rect(s_ui.canvas2, 0, 0, w, h, &dsc);
  int sw = (soc >= 0.0f) ? (int)(soc / 100.0f * (w - 4)) : 0;
  if (sw > w - 4) sw = w - 4;
  if (sw > 0) {
    dsc.bg_color = lv_color_make(0x30, 0x90, 0xFF);
    lv_canvas_draw_rect(s_ui.canvas2, 2, 2, sw, h - 4, &dsc);
  }
  dsc.bg_opa = LV_OPA_TRANSP;
  dsc.border_opa = LV_OPA_COVER;
  dsc.border_color = lv_color_white();
  dsc.border_width = 2;
  lv_canvas_draw_rect(s_ui.canvas2, 0, 0, w, h, &dsc);
  // 80 % target tick: both charge targets read off the same scale.
  dsc.bg_opa = LV_OPA_COVER;
  dsc.bg_color = lv_color_white();
  dsc.border_opa = LV_OPA_TRANSP;
  int tx = 2 + (int)(0.8f * (float)(w - 4));
  lv_canvas_draw_rect(s_ui.canvas2, tx, 0, 2, h, &dsc);
}

static void gauge_draw(int kw, bool charge) {
  if (s_ui.canvas == nullptr) return;
  int w = s_ui.cw;
  int h = s_ui.chh;
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);

  dsc.bg_opa = LV_OPA_COVER;
  dsc.bg_color = lv_color_make(0x20, 0x20, 0x20);
  dsc.border_opa = LV_OPA_TRANSP;
  dsc.radius = 0;
  lv_canvas_draw_rect(s_ui.canvas, 0, 0, w, h, &dsc);

  int in_y = 4;
  int in_h = h - 8;

  if (charge) {
    // Charge speed meter, full canvas, green gradient. SoC lives on canvas2.
    int bw = meter_charge_fill(kw, w - 4, g_state.chg_max_kw);
    for (int x = 0; x < bw; x += 5) {
      int t = (x * 255) / (bw > 0 ? bw : 1);
      int sw = (x + 5 > bw) ? (bw - x) : 5;
      strip(&dsc, 2 + x, in_y, sw, in_h,
            lv_color_make((uint8_t)(0x60 + ((0x00 - 0x60) * t / 255)),
                          (uint8_t)(0xC8 - ((0xC8 - 0x78) * t / 255)), 0x00));
    }
  } else {
    int half = w / 2;
    MeterDriveFill f = meter_drive_fill(kw, half);
    for (int x = 0; x < f.pos_w; x += 5) {
      int t = (x * 255) / (f.pos_w > 0 ? f.pos_w : 1);
      int sw = (x + 5 > f.pos_w) ? (f.pos_w - x) : 5;
      strip(&dsc, half + 1 + x, in_y, sw, in_h,
            lv_color_make((uint8_t)(0xF0 - ((0xF0 - 0xE0) * t / 255)),
                          (uint8_t)(0xC0 - ((0xC0 - 0x30) * t / 255)), 0x10));
    }
    for (int x = 0; x < f.neg_w; x += 5) {
      int t = ((f.neg_w - x) * 255) / (f.neg_w > 0 ? f.neg_w : 1);
      int sw = (x + 5 > f.neg_w) ? (f.neg_w - x) : 5;
      strip(&dsc, half - f.neg_w + x, in_y, sw, in_h,
            lv_color_make((uint8_t)(0x60 - ((0x60 - 0x00) * t / 255)),
                          (uint8_t)(0xC8 - ((0xC8 - 0x78) * t / 255)), 0x00));
    }
    dsc.bg_color = lv_color_white();
    lv_canvas_draw_rect(s_ui.canvas, half - 1, 2, 2, h - 4, &dsc);
  }

  dsc.bg_opa = LV_OPA_TRANSP;
  dsc.border_opa = LV_OPA_COVER;
  dsc.border_color = lv_color_white();
  dsc.border_width = 2;
  lv_canvas_draw_rect(s_ui.canvas, 0, 0, w, h, &dsc);
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
  return l;
}

static void rule(lv_obj_t *scr, int y, int w) {
  lv_obj_t *r = lv_obj_create(scr);
  lv_obj_set_size(r, w, 2);
  lv_obj_set_style_bg_color(r, lv_color_make(0x30, 0x90, 0xFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(r, 0, LV_PART_MAIN);
  lv_obj_align(r, LV_ALIGN_TOP_MID, 0, y);
}

static void build_landscape(lv_obj_t *scr) {
  s_ui.trip = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_obj_align(s_ui.trip, LV_ALIGN_TOP_MID, 0, 6);

  s_ui.ble = mk_ble(scr);
  lv_obj_align(s_ui.ble, LV_ALIGN_TOP_RIGHT, -8, 8);

  s_ui.hero = mk_label(scr, &lv_font_montserrat_48, lv_color_white());
  lv_obj_align(s_ui.hero, LV_ALIGN_TOP_MID, 0, 36);

  s_ui.unit = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(s_ui.unit, "MI PACE RANGE");
  lv_obj_align(s_ui.unit, LV_ALIGN_TOP_MID, 0, 100);

  s_ui.advisory = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0xFF, 0xD7, 0x00));
  lv_obj_align(s_ui.advisory, LV_ALIGN_TOP_MID, 0, 126);

  s_ui.kw_num = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_obj_align(s_ui.kw_num, LV_ALIGN_TOP_MID, 0, 158);

  // Charge rows: label left, meter, fixed right-anchored value, the whole
  // row inset 24 px from both walls. Drive re-centers the shared canvas
  // per mode in standard_update.
  s_ui.lab1 = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(s_ui.lab1, "ROC");
  lv_obj_align(s_ui.lab1, LV_ALIGN_TOP_LEFT, 24, 144);
  lv_obj_add_flag(s_ui.lab1, LV_OBJ_FLAG_HIDDEN);

  s_ui.cw = 324;
  s_ui.chh = 26;
  s_ui.cbuf = s_gauge_buf;
  s_ui.canvas = lv_canvas_create(scr);
  lv_canvas_set_buffer(s_ui.canvas, s_ui.cbuf, s_ui.cw, s_ui.chh, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(s_ui.canvas, LV_ALIGN_TOP_MID, 0, 182);

  s_ui.val1 = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_obj_set_width(s_ui.val1, 100);
  lv_obj_set_style_text_align(s_ui.val1, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(s_ui.val1, LV_ALIGN_TOP_RIGHT, -24, 169);
  lv_obj_add_flag(s_ui.val1, LV_OBJ_FLAG_HIDDEN);

  s_ui.lab2 = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(s_ui.lab2, "SOC");
  lv_obj_align(s_ui.lab2, LV_ALIGN_TOP_LEFT, 24, 198);
  lv_obj_add_flag(s_ui.lab2, LV_OBJ_FLAG_HIDDEN);

  s_ui.cw2 = 324;
  s_ui.chh2 = 26;
  s_ui.cbuf2 = s_gauge_buf2;
  s_ui.canvas2 = lv_canvas_create(scr);
  lv_canvas_set_buffer(s_ui.canvas2, s_ui.cbuf2, s_ui.cw2, s_ui.chh2, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(s_ui.canvas2, LV_ALIGN_TOP_LEFT, 24, 218);
  lv_obj_add_flag(s_ui.canvas2, LV_OBJ_FLAG_HIDDEN);

  s_ui.val2 = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_obj_set_width(s_ui.val2, 100);
  lv_obj_set_style_text_align(s_ui.val2, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(s_ui.val2, LV_ALIGN_TOP_RIGHT, -24, 223);
  lv_obj_add_flag(s_ui.val2, LV_OBJ_FLAG_HIDDEN);

  s_ui.maxl = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0x00, 0xC8, 0x00));
  lv_obj_align(s_ui.maxl, LV_ALIGN_BOTTOM_LEFT, 8, -8);
  s_ui.maxr = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0xE0, 0x30, 0x10));
  lv_obj_align(s_ui.maxr, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
}

static void build_portrait(lv_obj_t *scr) {
  s_ui.trip = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_obj_align(s_ui.trip, LV_ALIGN_TOP_MID, 0, 8);

  rule(scr, 34, 220);

  lv_obj_t *cap = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(cap, "PACE RANGE");
  lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 42);

  s_ui.hero = mk_label(scr, &lv_font_montserrat_48, lv_color_white());
  lv_obj_align(s_ui.hero, LV_ALIGN_TOP_MID, 0, 62);

  s_ui.unit = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(s_ui.unit, "mi");
  lv_obj_align(s_ui.unit, LV_ALIGN_TOP_MID, 0, 118);

  s_ui.pill = lv_obj_create(scr);
  lv_obj_t *pill = s_ui.pill;
  lv_obj_set_size(pill, 224, 36);
  lv_obj_set_style_bg_color(pill, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_color(pill, lv_color_make(0xFF, 0xD7, 0x00), LV_PART_MAIN);
  lv_obj_set_style_border_width(pill, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(pill, 18, LV_PART_MAIN);
  lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 142);
  s_ui.advisory = mk_label(pill, &lv_font_montserrat_14, lv_color_make(0xFF, 0xD7, 0x00));
  lv_obj_center(s_ui.advisory);

  rule(scr, 188, 220);

  lv_obj_t *live = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(live, "LIVE DRAW");
  lv_obj_align(live, LV_ALIGN_TOP_MID, 0, 196);

  s_ui.kw_num = mk_label(scr, &lv_font_montserrat_28, lv_color_white());
  lv_obj_align(s_ui.kw_num, LV_ALIGN_TOP_MID, 0, 216);

  s_ui.lab1 = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(s_ui.lab1, "ROC");
  lv_obj_align(s_ui.lab1, LV_ALIGN_TOP_MID, 0, 236);
  lv_obj_add_flag(s_ui.lab1, LV_OBJ_FLAG_HIDDEN);

  s_ui.cw = 194;
  s_ui.chh = 26;
  s_ui.cbuf = s_gauge_buf;
  s_ui.canvas = lv_canvas_create(scr);
  lv_canvas_set_buffer(s_ui.canvas, s_ui.cbuf, s_ui.cw, s_ui.chh, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(s_ui.canvas, LV_ALIGN_TOP_MID, 0, 252);

  s_ui.val1 = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_obj_set_width(s_ui.val1, 86);
  lv_obj_set_style_text_align(s_ui.val1, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(s_ui.val1, LV_ALIGN_TOP_RIGHT, -16, 221);
  lv_obj_add_flag(s_ui.val1, LV_OBJ_FLAG_HIDDEN);

  s_ui.lab2 = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(s_ui.lab2, "SOC");
  lv_obj_align(s_ui.lab2, LV_ALIGN_TOP_MID, 0, 278);
  lv_obj_add_flag(s_ui.lab2, LV_OBJ_FLAG_HIDDEN);

  s_ui.cw2 = 194;
  s_ui.chh2 = 26;
  s_ui.cbuf2 = s_gauge_buf2;
  s_ui.canvas2 = lv_canvas_create(scr);
  lv_canvas_set_buffer(s_ui.canvas2, s_ui.cbuf2, s_ui.cw2, s_ui.chh2, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(s_ui.canvas2, LV_ALIGN_TOP_MID, 0, 292);
  lv_obj_add_flag(s_ui.canvas2, LV_OBJ_FLAG_HIDDEN);

  s_ui.val2 = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_obj_set_width(s_ui.val2, 86);
  lv_obj_set_style_text_align(s_ui.val2, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(s_ui.val2, LV_ALIGN_TOP_RIGHT, -16, 271);
  lv_obj_add_flag(s_ui.val2, LV_OBJ_FLAG_HIDDEN);

  s_ui.capA = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0x00, 0xC8, 0x00));
  lv_label_set_text(s_ui.capA, "Regen");
  lv_obj_align(s_ui.capA, LV_ALIGN_TOP_LEFT, 12, 282);

  s_ui.capB = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0xE0, 0x30, 0x10));
  lv_label_set_text(s_ui.capB, "Power Draw");
  lv_obj_align(s_ui.capB, LV_ALIGN_TOP_RIGHT, -12, 282);

  s_ui.maxl = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0x00, 0xC8, 0x00));
  lv_obj_align(s_ui.maxl, LV_ALIGN_TOP_LEFT, 8, 300);
  s_ui.maxr = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0xE0, 0x30, 0x10));
  lv_obj_align(s_ui.maxr, LV_ALIGN_TOP_RIGHT, -8, 300);

  s_ui.ble = mk_ble(scr);
  lv_obj_align(s_ui.ble, LV_ALIGN_TOP_MID, 0, 300);
}

void standard_build(lv_obj_t *scr, Orientation o) {
  s_orient = o;
  s_ui = StdUi();
  if (o == Orientation::LANDSCAPE) {
    build_landscape(scr);
  } else {
    build_portrait(scr);
  }
  standard_update();
}

static void show(lv_obj_t *o, bool v) {
  if (o == nullptr) return;
  if (v) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void standard_update(void) {
  bool charge = vehicle_charging();
  // Trip strip is a drive layout; hidden while charging.
  show(s_ui.trip, !charge);
  // Trip strip shows live data only: real clock, HV %, LV volts.
  // Distance and efficiency return with the odometer and energy model.
  char trip[64];
  uint32_t m = g_state.trip_sec / 60;
  uint32_t s = g_state.trip_sec % 60;
  char socb[12], voltb[12];
  // Displayed SoC matches the dash; raw 015B backs it up.
  float socshow = g_state.soc_disp >= 0.0f ? g_state.soc_disp : g_state.soc_pct;
  if (socshow >= 0.0f) {
    snprintf(socb, sizeof(socb), "%.0f%%", (double)socshow);
  } else {
    snprintf(socb, sizeof(socb), "--");
  }
  if (g_state.mod_v >= 0.0f) {
    snprintf(voltb, sizeof(voltb), "%.1fV", (double)g_state.mod_v);
  } else {
    snprintf(voltb, sizeof(voltb), "--");
  }
  snprintf(trip, sizeof(trip), "TRIP %02lu:%02lu | HV %s | LV %s", (unsigned long)m,
           (unsigned long)s, socb, voltb);
  if (!charge && s_ui.trip != nullptr) lv_label_set_text(s_ui.trip, trip);
  if (s_ui.unit != nullptr) {
    if (charge) {
      lv_label_set_text(s_ui.unit, "THIS SESSION");
    } else if (s_orient == Orientation::LANDSCAPE) {
      lv_label_set_text(s_ui.unit, "MI PACE RANGE");
    } else {
      lv_label_set_text(s_ui.unit, "mi");
    }
  }
  if (s_ui.hero != nullptr) {
    if (charge) {
      // LVGL mini-printf has no %f: format session kWh with snprintf.
      char kb[24];
      snprintf(kb, sizeof(kb), "+%.0f kWh", (double)g_state.chg_kwh);
      lv_label_set_text(s_ui.hero, kb);
    } else if (g_state.pace_mi < 0) {
      // Pace range until the validated energy model lands: unknown, not a number.
      lv_label_set_text(s_ui.hero, "--");
    } else {
      lv_label_set_text_fmt(s_ui.hero, "%d", g_state.pace_mi);
    }
  }
  // Charge layout reclaims the strip row: positions per mode, exact rows.
  // Charge hides kw_num; the ETA owns the yellow advisory row instead.
  if (s_orient == Orientation::LANDSCAPE) {
    if (charge) {
      lv_obj_align(s_ui.hero, LV_ALIGN_TOP_MID, 0, 32);
      lv_obj_align(s_ui.unit, LV_ALIGN_TOP_MID, 0, 88);
      lv_obj_align(s_ui.advisory, LV_ALIGN_TOP_MID, 0, 112);
      lv_obj_align(s_ui.canvas, LV_ALIGN_TOP_LEFT, 24, 164);
    } else {
      lv_obj_align(s_ui.hero, LV_ALIGN_TOP_MID, 0, 36);
      lv_obj_align(s_ui.unit, LV_ALIGN_TOP_MID, 0, 100);
      lv_obj_align(s_ui.advisory, LV_ALIGN_TOP_MID, 0, 126);
      lv_obj_align(s_ui.kw_num, LV_ALIGN_TOP_MID, 0, 158);
      lv_obj_align(s_ui.canvas, LV_ALIGN_TOP_MID, 0, 182);
    }
  } else {
    if (charge) {
      lv_obj_align(s_ui.hero, LV_ALIGN_TOP_MID, 0, 44);
      lv_obj_align(s_ui.unit, LV_ALIGN_TOP_MID, 0, 100);
      if (s_ui.pill != nullptr) lv_obj_align(s_ui.pill, LV_ALIGN_TOP_MID, 0, 124);
      lv_obj_align(s_ui.lab1, LV_ALIGN_TOP_LEFT, 16, 200);
      lv_obj_align(s_ui.canvas, LV_ALIGN_TOP_LEFT, 16, 216);
      lv_obj_align(s_ui.lab2, LV_ALIGN_TOP_LEFT, 16, 250);
      lv_obj_align(s_ui.canvas2, LV_ALIGN_TOP_LEFT, 16, 266);
    } else {
      lv_obj_align(s_ui.hero, LV_ALIGN_TOP_MID, 0, 62);
      lv_obj_align(s_ui.unit, LV_ALIGN_TOP_MID, 0, 118);
      if (s_ui.pill != nullptr) lv_obj_align(s_ui.pill, LV_ALIGN_TOP_MID, 0, 142);
      lv_obj_align(s_ui.canvas, LV_ALIGN_TOP_MID, 0, 252);
    }
  }
  if (s_ui.advisory != nullptr) {
    if (charge) {
      // Charge ETA owns the yellow row: "H:MM until N%". Power lives in ROC.
      float eta_h = charge_eta_hours();
      int tgt = charge_target_pct();
      char eb[32];
      if (eta_h < 0.0f || tgt < 0) {
        snprintf(eb, sizeof(eb), "CHARGING");
      } else if (eta_h == 0.0f) {
        snprintf(eb, sizeof(eb), "FULL");
      } else {
        int mins = (int)(eta_h * 60.0f + 0.5f);
        snprintf(eb, sizeof(eb), "%d:%02d until %d%%", mins / 60, mins % 60, tgt);
      }
      lv_label_set_text(s_ui.advisory, eb);
    } else {
      lv_label_set_text(s_ui.advisory, g_state.advisory);
    }
  }
  ble_paint();
  // Smooth the bar toward the last link value so 250 ms UI motion
  // never costs link packets.
  static int shown_kw = 0;
  // Charge sessions display validated charge watts; drive shows pack power.
  int target_kw = g_state.kw;
  if (charge && g_state.chg_w >= 0) target_kw = (int)((g_state.chg_w + 500) / 1000);
  int diff = target_kw - shown_kw;
  if (diff != 0) {
    int step = diff / 3;
    if (step == 0) step = (diff > 0) ? 1 : -1;
    shown_kw += step;
  }
  if (s_ui.maxl != nullptr) {
    if (charge) {
      lv_obj_add_flag(s_ui.maxl, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_ui.maxr, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(s_ui.maxl, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(s_ui.maxr, LV_OBJ_FLAG_HIDDEN);
    }
  }
  // Charge-only meters and captions; drive captions return otherwise.
  show(s_ui.lab1, charge);
  show(s_ui.lab2, charge);
  show(s_ui.canvas2, charge);
  show(s_ui.val1, charge);
  show(s_ui.val2, charge);
  show(s_ui.kw_num, !charge);
  show(s_ui.capA, !charge);
  show(s_ui.capB, !charge);
  // End-of-row values: fixed width, right-anchored, so live numbers
  // never shift the layout while charging.
  if (charge && s_ui.val1 != nullptr) {
    char vb[16];
    if (g_state.chg_w >= 0) {
      snprintf(vb, sizeof(vb), "%.1fkW", (double)g_state.chg_w / 1000.0);
    } else {
      snprintf(vb, sizeof(vb), "--");
    }
    lv_label_set_text(s_ui.val1, vb);
  }
  if (charge && s_ui.val2 != nullptr) {
    char vb[16];
    if (socshow >= 0.0f) {
      snprintf(vb, sizeof(vb), "%.0f%%", (double)socshow);
    } else {
      snprintf(vb, sizeof(vb), "--");
    }
    lv_label_set_text(s_ui.val2, vb);
  }
  if (s_orient == Orientation::PORTRAIT) {
    if (s_ui.kw_num != nullptr) {
      lv_obj_set_style_text_font(s_ui.kw_num,
                                 charge ? &lv_font_montserrat_14 : &lv_font_montserrat_28,
                                 LV_PART_MAIN);
    }
    // Bottom row is the SoC meter while charging; park the link icon top.
    if (s_ui.ble != nullptr) {
      if (charge) lv_obj_align(s_ui.ble, LV_ALIGN_TOP_RIGHT, -8, 8);
      else lv_obj_align(s_ui.ble, LV_ALIGN_TOP_MID, 0, 300);
    }
  }
  // kw_num is drive-only; charge hides it and shows the ETA in advisory.
  if (s_ui.kw_num != nullptr && !charge) {
    if (s_orient == Orientation::LANDSCAPE) {
      lv_label_set_text_fmt(s_ui.kw_num,
                            (shown_kw >= 0 ? "POWER +%d kW" : "REGEN %d kW"), shown_kw);
    } else {
      lv_label_set_text_fmt(s_ui.kw_num, "%d", shown_kw);
    }
  }
  gauge_draw(shown_kw, charge);
  if (charge) soc_draw(socshow);
  if (s_ui.maxl != nullptr) lv_label_set_text_fmt(s_ui.maxl, "%d MAX", g_state.max_regen_kw);
  if (s_ui.maxr != nullptr) lv_label_set_text_fmt(s_ui.maxr, "MAX +%d", g_state.max_draw_kw);
}
