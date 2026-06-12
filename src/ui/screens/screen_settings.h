#pragma once
#include <TFT_eSPI.h>
#include <cstdint>

void     screenSettingsDraw(TFT_eSPI &tft, bool wifiOk);
bool     screenSettingsTap(TFT_eSPI &tft, int16_t x, int16_t y);

// Returns true if user tapped "Manage Coins" — caller should switch to coin picker
bool     screenSettingsCoinPickerTapped();

// Auto-rotate (cycles through enabled pages)
bool     screenSettingsGetAutoRotate();
uint32_t screenSettingsGetAutoRotateMs();

// Sleep timer
int      screenSettingsGetSleepTimerSecs();
bool     screenSettingsGetScheduleEnabled();
int      screenSettingsGetSleepHour();
int      screenSettingsGetWakeHour();
