#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <lvgl.h>

#include "state.h"
#include "themes.h"
#include "button.h"
#include "obd/ble_link.h"
#include "splash_img.h"

#include "fw_version.h"  // generated each build by tools/gen_fw_version.py
#define SPLASH_MS 10000

// ESP32-2432S028R (2.8" CYD): ST7789 240x320.
// NOTE: this unit carries the ST7789 panel variant, not ILI9341.
// Do not switch back: ILI9341 init renders text but scrambles LVGL output.
class LGFX_CYD_28 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;

 public:
  LGFX_CYD_28(void) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 3700;
      cfg.y_max = 200;
      cfg.pin_int = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 2;
      cfg.spi_host = (spi_host_device_t)-1;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX_CYD_28 tft;
static lv_disp_t *g_disp = nullptr;
static lv_disp_drv_t g_drv;

void display_set_orientation(Orientation o) {
  if (o == Orientation::LANDSCAPE) {
    tft.setRotation(1);
    g_drv.hor_res = 320;
    g_drv.ver_res = 240;
  } else {
    tft.setRotation(0);
    g_drv.hor_res = 240;
    g_drv.ver_res = 320;
  }
  if (g_disp != nullptr) lv_disp_drv_update(g_disp, &g_drv);
}

// --- LVGL input: touch cache polled in loop(), served to LVGL ---
static volatile bool s_touch_pressed = false;
static volatile int16_t s_touch_x = 0;
static volatile int16_t s_touch_y = 0;

static void my_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  (void)drv;
  static bool was_pressed = false;
  if (s_touch_pressed) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = s_touch_x;
    data->point.y = s_touch_y;
    if (!was_pressed) {
      was_pressed = true;
      Serial.printf("[indev] press x=%d y=%d\n", s_touch_x, s_touch_y);
    }
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
    if (was_pressed) {
      was_pressed = false;
      Serial.println("[indev] release");
    }
  }
}

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
  uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
  tft.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
  lv_disp_flush_ready(disp);
}

// --- State tick + splash ---
static void state_tick(lv_timer_t *t);

static lv_obj_t *s_splash = nullptr;
static lv_obj_t *s_idle = nullptr;
static lv_obj_t *s_idle_status = nullptr;
static bool s_showing_idle = true;
static uint32_t s_last_link_ms = 0;

// HERO IMAGE SLOT: drop the truck art at project_details/assets/hero/
// and it gets converted to a canvas here. Until then, type only.
static void idle_show(void) {
  if (s_idle == nullptr) {
    s_idle = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_idle, lv_color_black(), LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(s_idle);
    lv_label_set_text(title, "RangeReality");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t *icon = lv_label_create(s_idle);
    lv_label_set_text(icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(icon, lv_color_make(0xB0, 0xB0, 0xB0), LV_PART_MAIN);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -20);
    s_idle_status = lv_label_create(s_idle);
    lv_obj_set_style_text_color(s_idle_status, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_idle_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_idle_status, LV_ALIGN_CENTER, 0, 44);
    lv_obj_t *spin = lv_spinner_create(s_idle, 1000, 60);
    lv_obj_set_size(spin, 32, 32);
    lv_obj_align(spin, LV_ALIGN_CENTER, 0, 92);
  }
  lv_scr_load(s_idle);
  s_showing_idle = true;
}

static void splash_done(lv_timer_t *t) {
  lv_timer_del(t);
  lv_obj_t *old = s_splash;
  s_splash = nullptr;
  // Load idle BEFORE deleting splash: deleting the active screen leaves
  // scr_act dangling and the following scr_load faults on it.
  idle_show();
  if (old != nullptr && old != s_idle) lv_obj_del(old);
  state_tick(nullptr);
  lv_timer_create(state_tick, 250, nullptr);
}

static void state_tick(lv_timer_t *t) {
  (void)t;
  static bool announced = false;
  if (!announced) {
    announced = true;
    Serial.println("[ui] first update, lv timers running");
  }
  if (g_ble != BleLink::CONNECTED) state_stub_update();
  state_track_peaks();
  state_advisory_update();
  if (g_ble == BleLink::CONNECTED) {
    s_last_link_ms = millis();
    if (s_showing_idle) {
      themes_apply();
      s_showing_idle = false;
    }
    themes_update();
  } else {
    if (!s_showing_idle && millis() - s_last_link_ms > 60000) {
      idle_show();
    }
    if (s_idle_status != nullptr) {
      lv_label_set_text(s_idle_status,
                        g_ble == BleLink::SCANNING ? "Searching for adapter..." : "Waiting for link");
    }
  }
}

// --- Serial console ---
static void console_help(void) {
  Serial.println("[cmd] theme <standard|kitt|minimalist|steampunk>");
  Serial.println("[cmd] orient <portrait|landscape>");
  Serial.println("[cmd] status");
}

static void console_status(void) {
  Serial.printf("[cmd] theme=%s orient=%s ble=%s\n", themes_active()->name,
                orient_current() == Orientation::PORTRAIT ? "portrait" : "landscape",
                ble_state_str());
}

static void console_poll(void) {
  static char line[64];
  static size_t len = 0;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (len == 0) continue;
      line[len] = '\0';
      len = 0;
      if (strncmp(line, "theme ", 6) == 0) {
        if (themes_set(line + 6)) {
          if (!s_showing_idle) themes_apply();
          Serial.printf("[cmd] theme now %s\n", line + 6);
        } else {
          Serial.printf("[cmd] unknown or unbuilt theme '%s'\n", line + 6);
        }
      } else if (strncmp(line, "orient ", 7) == 0) {
        if (orient_set(line + 7)) {
          if (!s_showing_idle) themes_apply();
          Serial.printf("[cmd] orient now %s\n", line + 7);
        } else {
          Serial.println("[cmd] use orient portrait|landscape");
        }
      } else if (strcmp(line, "status") == 0) {
        console_status();
      } else {
        console_help();
      }
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

static void i2c_scan(void) {
  Serial.println("[boot] I2C scan...");
  Wire.begin(27, 22);
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[boot] I2C ACK 0x%02X\n", addr);
      found++;
    }
  }
  Serial.println(found == 0 ? "[boot] has_accelerometer=false"
                            : "[boot] has_accelerometer=true (verify 0x68 for MPU6050)");
}

void setup(void) {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[boot] Lightning McScreen bringup v" RR_FW_VERSION " (themes)");
  Serial.println("[boot] Ohm On The Range");

  themes_init();
  button_init();
  i2c_scan();
  Serial.println("[boot] UART GPS listen stub: has_gps=false (wiring later)");

  tft.init();
  display_set_orientation(orient_current());
  tft.setBrightness(128);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  lv_init();
  static lv_color_t buf[320 * 16];
  static lv_disp_draw_buf_t draw_buf;
  lv_disp_draw_buf_init(&draw_buf, buf, nullptr, 320 * 16);
  lv_disp_drv_init(&g_drv);
  if (orient_current() == Orientation::LANDSCAPE) {
    g_drv.hor_res = 320;
    g_drv.ver_res = 240;
  } else {
    g_drv.hor_res = 240;
    g_drv.ver_res = 320;
  }
  g_drv.flush_cb = my_disp_flush;
  g_drv.draw_buf = &draw_buf;
  g_disp = lv_disp_drv_register(&g_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
  Serial.printf("[indev] registered=%p\n", (void *)indev);

  ble_link_init();

  lv_obj_t *scr = lv_scr_act();
  s_splash = scr;
  lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
  lv_obj_t *hero = lv_img_create(scr);
  lv_img_set_src(hero, &splash_img);
  lv_obj_center(hero);
  lv_obj_t *brand = lv_label_create(scr);
  lv_label_set_text(brand, "RangeReality");
  lv_obj_set_style_text_color(brand, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(brand, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 8, 6);
  lv_obj_t *fwver = lv_label_create(scr);
  lv_label_set_text(fwver, "Firmware " RR_FW_VERSION);
  lv_obj_set_style_text_color(fwver, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(fwver, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(fwver, LV_ALIGN_TOP_LEFT, 8, 40);

  console_status();
  console_help();
  lv_timer_create(splash_done, SPLASH_MS, nullptr);
  Serial.println("[boot] LVGL splash ready");
}

void loop(void) {
  static uint32_t last_touch_poll = 0;
  if (millis() - last_touch_poll > 25) {
    last_touch_poll = millis();
    uint16_t x = 0, y = 0;
    if (tft.getTouch(&x, &y)) {
      s_touch_pressed = true;
      s_touch_x = (int16_t)x;
      s_touch_y = (int16_t)y;
    } else {
      s_touch_pressed = false;
    }
  }
  static uint32_t last_touch_dbg = 0;
  if (millis() - last_touch_dbg > 2000) {
    last_touch_dbg = millis();
    Serial.printf("[touch] pressed=%d x=%d y=%d\n", s_touch_pressed, s_touch_x, s_touch_y);
  }
  console_poll();
  button_poll();
  ble_link_poll();
  lv_timer_handler();
  delay(5);
}
