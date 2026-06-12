#pragma once
#include <TFT_eSPI.h>

void screenCoinPickerDraw(TFT_eSPI &tft, bool wifiOk);
void screenCoinPickerTap(TFT_eSPI &tft, int16_t x, int16_t y, bool wifiOk);
void screenCoinPickerSwipe(int dir);   // 1 = down, -1 = up (scroll)
bool screenCoinPickerShouldExit();     // true = go back to settings
