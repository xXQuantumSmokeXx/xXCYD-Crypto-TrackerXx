#pragma once
#include <TFT_eSPI.h>
#include <cstdint>

// Draw the standard CYD topbar with corner brackets, screen label, time, WiFi status
void drawTopbar(TFT_eSPI &tft, const char *leftLabel, const char *screenLabel, const char *timeStr, bool wifiOk);
void drawTopbarTime(TFT_eSPI &tft, const char *timeStr, const char *screenLabel);

// Draw bottom bar with navigation arrows, battery %, page dots or label
void drawBottombar(TFT_eSPI &tft, const char *label, int activeScreen, int totalScreens);

// Status bar line below topbar (used for data source / sync info)
void drawStatusbar(TFT_eSPI &tft, const char *source, const char *center, const char *right);

// Draw a highlighted percentage chip (green/red for crypto gains/losses)
void drawPctChip(TFT_eSPI &tft, int x, int y, float pct, bool compact = false);
