#pragma once
#include <TFT_eSPI.h>

void screenCryptoDraw(TFT_eSPI &tft, bool wifiOk, int page);
void screenCryptoTap(TFT_eSPI &tft, int16_t x, int16_t y, int page, bool wifiOk);
// Returns coin ID string if a coin row was tapped (for detail view), else nullptr
const char* screenCryptoGetTappedCoin();
