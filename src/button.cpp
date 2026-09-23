#include "button.h"

#include <Arduino.h>

#include "state.h"
#include "themes.h"

// GPIO0 is a strapping pin: holding BOOT at power on forces download
// mode, which is exactly how flashing works. After boot it is a free
// input with the onboard button pulling low. Never invert that behavior.

#define BTN_PIN 0
#define BTN_DEBOUNCE_MS 30
#define BTN_DOUBLE_MS 450
#define BTN_LONG_MS 1500

static bool s_pressed = false;
static uint32_t s_last_change = 0;
static uint32_t s_press_start = 0;
static uint8_t s_taps = 0;
static uint32_t s_first_release = 0;
static bool s_long_fired = false;

void button_init(void) {
  pinMode(BTN_PIN, INPUT_PULLUP);
}

static void on_single(void) {
  // Orientation toggle lives here once portrait UIs land.
  Serial.println("[btn] single: orientation toggle parked");
}

static void on_double(void) {
  themes_cycle();
  Serial.printf("[btn] double: theme now %s\n", themes_active()->name);
}

static void on_long(void) {
  g_state.trip_sec = 0;
  g_state.trip_mi = 0.0f;
  g_state.trip_kwh = 0.0f;
  Serial.println("[btn] long: trip clocks reset");
}

void button_poll(void) {
  bool pressed = digitalRead(BTN_PIN) == LOW;
  uint32_t now = millis();
  if (pressed != s_pressed && now - s_last_change > BTN_DEBOUNCE_MS) {
    s_pressed = pressed;
    s_last_change = now;
    if (pressed) {
      s_press_start = now;
      s_long_fired = false;
    } else if (!s_long_fired) {
      if (s_taps == 0) {
        s_taps = 1;
        s_first_release = now;
      } else {
        s_taps = 0;
        on_double();
      }
    }
  }
  if (s_pressed && !s_long_fired && now - s_press_start > BTN_LONG_MS) {
    s_long_fired = true;
    s_taps = 0;
    on_long();
  }
  if (s_taps == 1 && !s_pressed && now - s_first_release > BTN_DOUBLE_MS) {
    s_taps = 0;
    on_single();
  }
}
