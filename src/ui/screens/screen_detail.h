#pragma once
#include <TFT_eSPI.h>

// Show detail view for a single coin with graph tabs (24h/7d)
void screenDetailDraw(TFT_eSPI &tft, bool wifiOk, const char *coinId);
void screenDetailTap(TFT_eSPI &tft, int16_t x, int16_t y, bool wifiOk);
// Returns true if user tapped back (caller should return to crypto page)
bool screenDetailShouldExit();
