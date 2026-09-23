#include "themes.h"
#include <Preferences.h>

// Forward decls from theme_standard.cpp
void standard_build(lv_obj_t *scr, Orientation o);
void standard_update(void);

static const ThemeOps kRegistry[] = {
    {ThemeId::STANDARD, "standard", true, standard_build, standard_update},
    {ThemeId::KITT, "kitt", false, nullptr, nullptr},
    {ThemeId::MINIMALIST, "minimalist", false, nullptr, nullptr},
    {ThemeId::STEAMPUNK, "steampunk", false, nullptr, nullptr},
};

static Preferences s_prefs;
static ThemeId s_theme = ThemeId::STANDARD;
static Orientation s_orient = Orientation::PORTRAIT;
static lv_obj_t *s_screen = nullptr;

static const ThemeOps *find_theme(ThemeId id) {
  for (const auto &t : kRegistry) {
    if (t.id == id) return &t;
  }
  return &kRegistry[0];
}

static int clamp_theme(int v) {
  return (v >= 0 && v <= (int)ThemeId::STEAMPUNK) ? v : 0;
}

void themes_init(void) {
  s_prefs.begin("lmc", false);
  s_theme = (ThemeId)clamp_theme(s_prefs.getInt("theme", 0));
  int o = s_prefs.getInt("orient", 1);
  s_orient = (o == 0) ? Orientation::PORTRAIT : Orientation::LANDSCAPE;
}

ThemeId themes_current(void) { return s_theme; }
Orientation orient_current(void) { return s_orient; }
const ThemeOps *themes_active(void) { return find_theme(s_theme); }

bool themes_set(const char *name) {
  for (const auto &t : kRegistry) {
    if (strcmp(t.name, name) == 0) {
      if (!t.built) return false;
      s_theme = t.id;
      s_prefs.putInt("theme", (int)s_theme);
      return true;
    }
  }
  return false;
}

bool orient_set(const char *name) {
  if (strcmp(name, "portrait") == 0) {
    s_orient = Orientation::PORTRAIT;
  } else if (strcmp(name, "landscape") == 0) {
    s_orient = Orientation::LANDSCAPE;
  } else {
    return false;
  }
  s_prefs.putInt("orient", (int)s_orient);
  return true;
}

void themes_cycle(void) {
  const int n = sizeof(kRegistry) / sizeof(kRegistry[0]);
  int cur = 0;
  for (int i = 0; i < n; i++) {
    if (kRegistry[i].id == s_theme) cur = i;
  }
  for (int k = 1; k <= n; k++) {
    const ThemeOps &t = kRegistry[(cur + k) % n];
    if (t.built) {
      s_theme = t.id;
      s_prefs.putInt("theme", (int)s_theme);
      themes_apply();
      return;
    }
  }
}

void themes_apply(void) {
  const ThemeOps *t = find_theme(s_theme);
  display_set_orientation(s_orient);
  if (s_screen != nullptr) {
    lv_obj_del(s_screen);
    s_screen = nullptr;
  }
  s_screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_screen, lv_color_black(), LV_PART_MAIN);
  t->build(s_screen, s_orient);
  lv_scr_load(s_screen);
}

void themes_update(void) {
  const ThemeOps *t = find_theme(s_theme);
  if (t->built && t->update != nullptr) t->update();
}
