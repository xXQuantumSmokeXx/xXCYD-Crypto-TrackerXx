#include "theme_color.h"
#include "theme.h"
#include "../config/nvs_config.h"
#include "../modules/brightness.h"
#include <Arduino.h>

const ThemeEntry g_themes[THEME_COUNT] = {
    { "CYAN",   0x07FFu },
    { "GREEN",  0x07E0u },
    { "RED",    0xF800u },
    { "ORANGE", 0xFD00u },
    { "YELLOW", 0xFFE0u },
    { "GRAY",   0xCE79u },
    { "PURPLE", 0xF81Fu },
    { "PINK",   0xFC18u },
    { "WHITE",  0xFFFFu },
};

uint16_t g_themeColor  = 0x07FFu;
bool     g_invert      = false;
static uint16_t s_themeStored = 0x07FFu;
static int      g_themeIdx    = 0;

void themeColorInit() {
    g_themeIdx = nvsGetInt("theme_idx", 0);
    if (g_themeIdx < 0 || g_themeIdx >= THEME_COUNT) g_themeIdx = 0;
    s_themeStored = g_themes[g_themeIdx].color;
    g_invert = nvsGetInt("invert", 0) != 0;
    g_themeColor = g_invert ? 0x0000u : s_themeStored;
}

void themeColorSet(int idx) {
    if (idx < 0 || idx >= THEME_COUNT) return;
    g_themeIdx    = idx;
    s_themeStored = g_themes[idx].color;
    if (!g_invert) g_themeColor = s_themeStored;
    nvsPutInt("theme_idx", idx);
}

int      themeColorGetIdx() { return g_themeIdx; }

void invertSet(bool on) {
    if (on == g_invert) return;
    g_invert = on;
    g_themeColor = on ? 0x0000u : s_themeStored;
    if (on) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcWrite(TFT_BL, 255);
#else
        ledcWrite(0, 255);
#endif
    } else {
        brightnessRestore();
    }
    nvsPutInt("invert", on ? 1 : 0);
}

bool invertGet() { return g_invert; }
