#include "themes.h"
#include "state.h"
#include "meter.h"

// Standard theme, both orientations. Portrait follows
// project_details/assets/mock/standard.jpg. Landscape is the
// original bringup layout.

struct StdUi {
  lv_obj_t *trip = nullptr;
  lv_obj_t *hero = nullptr;
  lv_obj_t *advisory = nullptr;
  lv_obj_t *kw_num = nullptr;
  lv_obj_t *canvas = nullptr;
  lv_color_t *cbuf = nullptr;
  int cw = 0;
  int chh = 0;
  lv_obj_t *ble = nullptr;
  lv_obj_t *maxl = nullptr;
  lv_obj_t *maxr = nullptr;
};

static StdUi s_ui;
static Orientation s_orient = Orientation::PORTRAIT;
// One gauge buffer shared by both orientations: only one canvas is alive.
static lv_color_t s_gauge_buf[280 * 26];

void standard_update(void);

static void ble_paint(void) {
  if (s_ui.ble == nullptr) return;
  lv_color_t c = lv_color_make(0x50, 0x50, 0x50);
  if (g_ble == BleLink::CONNECTED) {
    c = lv_color_make(0x2E, 0x9B, 0xFF);
  } else if (g_ble == BleLink::SCANNING) {
    c = lv_color_make(0xFF, 0xD7, 0x00);
  }
  lv_obj_set_style_text_color(s_ui.ble, c, LV_PART_MAIN);
}

static lv_obj_t *mk_ble(lv_obj_t *parent) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_make(0x50, 0x50, 0x50), LV_PART_MAIN);
  return l;
}

static void strip(lv_draw_rect_dsc_t *dsc, int x, int y, int sw, int h, lv_color_t c) {
  dsc->bg_color = c;
  lv_canvas_draw_rect(s_ui.canvas, x, y, sw, h, dsc);
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
    // Full-bar charge meter, 0..200 kW input, green gradient.
    int bw = meter_charge_fill(kw, w - 4);
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

  lv_obj_t *unit = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(unit, "MI PACE RANGE");
  lv_obj_align(unit, LV_ALIGN_TOP_MID, 0, 100);

  s_ui.advisory = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0xFF, 0xD7, 0x00));
  lv_obj_align(s_ui.advisory, LV_ALIGN_TOP_MID, 0, 126);

  s_ui.kw_num = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_obj_align(s_ui.kw_num, LV_ALIGN_TOP_MID, 0, 158);

  s_ui.cw = 280;
  s_ui.chh = 26;
  s_ui.cbuf = s_gauge_buf;
  s_ui.canvas = lv_canvas_create(scr);
  lv_canvas_set_buffer(s_ui.canvas, s_ui.cbuf, s_ui.cw, s_ui.chh, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(s_ui.canvas, LV_ALIGN_TOP_MID, 0, 182);

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

  lv_obj_t *unit = mk_label(scr, &lv_font_montserrat_14, lv_color_white());
  lv_label_set_text(unit, "mi");
  lv_obj_align(unit, LV_ALIGN_TOP_MID, 0, 118);

  lv_obj_t *pill = lv_obj_create(scr);
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

  s_ui.cw = 210;
  s_ui.chh = 26;
  s_ui.cbuf = s_gauge_buf;
  s_ui.canvas = lv_canvas_create(scr);
  lv_canvas_set_buffer(s_ui.canvas, s_ui.cbuf, s_ui.cw, s_ui.chh, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(s_ui.canvas, LV_ALIGN_TOP_MID, 0, 252);

  lv_obj_t *regen = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0x00, 0xC8, 0x00));
  lv_label_set_text(regen, "Regen");
  lv_obj_align(regen, LV_ALIGN_TOP_LEFT, 12, 282);

  lv_obj_t *power = mk_label(scr, &lv_font_montserrat_14, lv_color_make(0xE0, 0x30, 0x10));
  lv_label_set_text(power, "Power Draw");
  lv_obj_align(power, LV_ALIGN_TOP_RIGHT, -12, 282);

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

void standard_update(void) {
  char trip[64];
  uint32_t m = g_state.trip_sec / 60;
  uint32_t s = g_state.trip_sec % 60;
  snprintf(trip, sizeof(trip), "TRIP %02lu:%02lu | %.1f mi | %.1f mi/kWh", (unsigned long)m,
           (unsigned long)s, (double)g_state.trip_mi, (double)g_state.trip_eff);
  if (s_ui.trip != nullptr) lv_label_set_text(s_ui.trip, trip);
  if (s_ui.hero != nullptr) lv_label_set_text_fmt(s_ui.hero, "%d", g_state.pace_mi);
  if (s_ui.advisory != nullptr) lv_label_set_text(s_ui.advisory, g_state.advisory);
  ble_paint();
  // Smooth the bar toward the last link value so 250 ms UI motion
  // never costs link packets.
  static int shown_kw = 0;
  int diff = g_state.kw - shown_kw;
  if (diff != 0) {
    int step = diff / 3;
    if (step == 0) step = (diff > 0) ? 1 : -1;
    shown_kw += step;
  }
  bool charge = meter_mode(g_state.gear == Gear::PARK) == MeterMode::CHARGE;
  if (s_ui.kw_num != nullptr) {
    if (charge) {
      lv_label_set_text_fmt(s_ui.kw_num, "CHARGE +%d kW", shown_kw < 0 ? 0 : shown_kw);
    } else if (s_orient == Orientation::LANDSCAPE) {
      lv_label_set_text_fmt(s_ui.kw_num,
                            (shown_kw >= 0 ? "POWER +%d kW" : "REGEN %d kW"), shown_kw);
    } else {
      lv_label_set_text_fmt(s_ui.kw_num, "%d", shown_kw);
    }
  }
  gauge_draw(shown_kw, charge);
  if (s_ui.maxl != nullptr) lv_label_set_text_fmt(s_ui.maxl, "%d MAX", g_state.max_regen_kw);
  if (s_ui.maxr != nullptr) lv_label_set_text_fmt(s_ui.maxr, "MAX +%d", g_state.max_draw_kw);
}
