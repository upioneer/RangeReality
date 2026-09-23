#pragma once
#include <lvgl.h>

// Theme + orientation manager. Themes are renderers over VehicleState.
// Selection persists in NVS so a public build keeps user choice.

enum class ThemeId : int {
  STANDARD = 0,
  KITT = 1,
  MINIMALIST = 2,
  STEAMPUNK = 3,
};

enum class Orientation : int {
  PORTRAIT = 0,
  LANDSCAPE = 1,
};

struct ThemeOps {
  ThemeId id;
  const char *name;
  bool built;
  void (*build)(lv_obj_t *scr, Orientation o);
  void (*update)(void);
};

void themes_init(void);
ThemeId themes_current(void);
Orientation orient_current(void);
const ThemeOps *themes_active(void);

// Returns false when the name is unknown or the theme is not built yet.
bool themes_set(const char *name);
bool orient_set(const char *name);
// Step to the next built theme and apply it.
void themes_cycle(void);

// Rebuild the screen for the current theme + orientation.
void themes_apply(void);
// Refresh the active theme from g_state.
void themes_update(void);

// Implemented by main.cpp: owns the panel + LVGL display handle.
void display_set_orientation(Orientation o);
