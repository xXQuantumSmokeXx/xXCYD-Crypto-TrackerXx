#include "screen_crypto.h"
#include "../theme.h"
#include "../theme_color.h"
#include "../widgets.h"
#include "../../config/config.h"
#include "../../modules/crypto.h"
#include "../../modules/brightness.h"
#include "../../modules/time_sync.h"
#include <cstdio>
#include <cstring>

static char s_tappedCoinId[32] = "";
static int  s_oneCoinGraphMode = 0;   // 0=24H, 1=7D

// ── Format price with comma separators ────────────────────────────────────
static void formatPrice(double price, char *out, int outLen) {
    char tmp[24];
    if (price < 0.001)
        snprintf(tmp, sizeof(tmp), "%.6f", price);
    else if (price < 1.0)
        snprintf(tmp, sizeof(tmp), "%.4f", price);
    else if (price < 100.0)
        snprintf(tmp, sizeof(tmp), "%.2f", price);
    else
        snprintf(tmp, sizeof(tmp), "%.0f", price);

    int intLen = 0;
    while (tmp[intLen] && tmp[intLen] != '.') intLen++;

    int o = 0;
    out[o++] = '$';
    for (int i = 0; tmp[i] && o < outLen - 1; i++) {
        if (i < intLen && i > 0 && (intLen - i) % 3 == 0)
            out[o++] = ',';
        out[o++] = tmp[i];
    }
    out[o] = '\0';
}

// ── Draw sparkline ────────────────────────────────────────────────────────
static void drawSpark(TFT_eSPI &tft, int x, int y, int w, int h,
                      const float *data, int count, float change,
                      bool forceTheme = false) {
    if (count < 2) return;
    tft.fillRect(x, y, w, h + 2, COL_BG);

    float mn = data[0], mx = data[0];
    for (int i = 1; i < count; i++) {
        if (data[i] < mn) mn = data[i];
        if (data[i] > mx) mx = data[i];
    }
    float range = mx - mn;
    if (range < 1e-6f) range = 1.0f;

    uint16_t col = forceTheme ? g_themeColor : ((change >= 0) ? g_themeColor : COL_RED);
    int px = -1, py = -1;
    for (int i = 0; i < count; i++) {
        int cx = x + (int)((float)i * (w - 1) / (count - 1));
        int cy = y + h - 1 - (int)(((data[i] - mn) / range) * (h - 2));
        if (cy < y)         cy = y;
        if (cy > y + h - 1) cy = y + h - 1;
        if (px >= 0) tft.drawLine(px, py, cx, cy, col);
        px = cx; py = cy;
    }
}

// ── 1 COIN — Full detail mode with inline graph switching ────────────────
static void drawOneCoin(TFT_eSPI &tft, int contentY, int contentH, const CoinData &c) {
    int y = contentY + 4;

    // Symbol (themed) + name (themed, to the right with gap)
    tft.setTextFont(FONT_MD);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(6, y);
    tft.print(c.symbol);

    tft.setTextFont(FONT_SM);
    tft.setTextColor(g_themeColor, COL_BG);
    int symW = tft.textWidth(c.symbol);
    tft.setCursor(6 + symW + 13, y + 4);
    tft.print(c.name);

    // Big price
    char priceBuf[20];
    formatPrice(c.priceUsd, priceBuf, sizeof(priceBuf));
    tft.setTextFont(FONT_LG);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(6, y + 20);
    tft.print(priceBuf);

    // ── 24h / 7d change percentages (FONT_MD values + FONT_MD labels) ──────
    int chgY = y + 52;
    char buf[16];

    // 24h
    snprintf(buf, sizeof(buf), "%+.2f%%", c.change24h);
    tft.setTextFont(FONT_MD);
    tft.setTextColor((c.change24h >= 0) ? COL_GREEN : COL_RED, COL_BG);
    tft.setCursor(6, chgY);
    tft.print(buf);
    tft.setTextFont(FONT_MD);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(6, chgY + 18);
    tft.print("24H");

    // 7d
    snprintf(buf, sizeof(buf), "%+.2f%%", c.change7d);
    tft.setTextFont(FONT_MD);
    tft.setTextColor((c.change7d >= 0) ? COL_GREEN : COL_RED, COL_BG);
    tft.setCursor(80, chgY);
    tft.print(buf);
    tft.setTextFont(FONT_MD);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(80, chgY + 18);
    tft.print("7D");

    // ── 24h High / Low — stacked on two lines (FONT_MD for readability) ────
    int hlY = chgY + 44;
    char hlBuf[20];

    // High line
    tft.setTextFont(FONT_MD);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(6, hlY);
    tft.print("HIGH  ");
    tft.setTextColor(COL_GREEN, COL_BG);
    snprintf(hlBuf, sizeof(hlBuf), "$%.2f", (double)c.high24h);
    tft.print(hlBuf);

    // Low line
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(6, hlY + 18);
    tft.print("LOW   ");
    tft.setTextColor(COL_RED, COL_BG);
    snprintf(hlBuf, sizeof(hlBuf), "$%.2f", (double)c.low24h);
    tft.print(hlBuf);

    // ── Chart mode label (right side, FONT_MD) ─────────────────────────────
    const char *modeLabel = (s_oneCoinGraphMode == 0) ? "24H" : "7D";
    {
        char modeBuf[14];
        snprintf(modeBuf, sizeof(modeBuf), "%s CHART", modeLabel);
        int mw = tft.textWidth(modeBuf);
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(SCREEN_W - mw - 6, hlY);
        tft.print(modeBuf);
        // Small hint below the chart label
        tft.setTextFont(FONT_SM);
        tft.setTextColor(COL_DIM, COL_BG);
        const char *hint = "tap to switch";
        int hw = tft.textWidth(hint);
        tft.setCursor(SCREEN_W - hw - 6, hlY + 20);
        tft.print(hint);
    }

    // ── Inline graph — switches between 24H / 7D on tap ───────────────────
    int sparkY = hlY + 42;
    int sparkH = contentY + contentH - sparkY - 4;

    const float *sparkData = nullptr;
    int sparkCount = 0;
    float sparkChange = 0;

    if (s_oneCoinGraphMode == 0) {
        // 24H — last 24 points of 7d sparkline
        if (c.spark7dCount >= 24) {
            sparkData  = c.spark7d + c.spark7dCount - 24;
            sparkCount = 24;
        } else {
            sparkData  = c.spark7d;
            sparkCount = c.spark7dCount;
        }
        sparkChange = c.change24h;
    } else {
        // 7D
        sparkData   = c.spark7d;
        sparkCount  = c.spark7dCount;
        sparkChange = c.change7d;
    }

    if (sparkH > 16 && sparkCount >= 2) {
        drawSpark(tft, 4, sparkY, SCREEN_W - 8, sparkH,
                  sparkData, sparkCount, sparkChange, true);
    }
}

// ── 2 COINS — Half detail mode ───────────────────────────────────────────
static void drawTwoCoins(TFT_eSPI &tft, int contentY, int contentH,
                         const CoinData &c0, const CoinData &c1) {
    int rowH = contentH / 2;

    for (int i = 0; i < 2; i++) {
        const CoinData &c = (i == 0) ? c0 : c1;
        int ry = contentY + i * rowH;

        if (i > 0)
            tft.drawFastHLine(0, ry, SCREEN_W, COL_DIM);

        int y = ry + 3;

        // ── Line 1: Symbol (themed) + Name (gray, right of symbol) ──────────
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(6, y);
        tft.print(c.symbol);

        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, COL_BG);
        int symW = tft.textWidth(c.symbol);
        tft.setCursor(6 + symW + 11, y + 4);
        tft.print(c.name);

        // ── Line 2: Price (white, left side) ────────────────────────────────
        char priceBuf[20];
        formatPrice(c.priceUsd, priceBuf, sizeof(priceBuf));
        tft.setTextFont(FONT_LG);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(6, y + 18);
        tft.print(priceBuf);

        // ── Line 3: 24H + 7D changes (values up 5px from labels) ───────────
        int chgY = ry + rowH - 25;
        char buf[16];

        snprintf(buf, sizeof(buf), "%+.2f%%", c.change24h);
        tft.setTextFont(FONT_MD);
        tft.setTextColor((c.change24h >= 0) ? COL_GREEN : COL_RED, COL_BG);
        tft.setCursor(6, chgY - 5);   // value 5px above label
        tft.print(buf);
        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(6, chgY + 14);
        tft.print("24H");

        snprintf(buf, sizeof(buf), "%+.2f%%", c.change7d);
        tft.setTextFont(FONT_MD);
        tft.setTextColor((c.change7d >= 0) ? COL_GREEN : COL_RED, COL_BG);
        tft.setCursor(80, chgY - 5);   // value 5px above label
        tft.print(buf);
        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(80, chgY + 14);
        tft.print("7D");

        // ── Divider + sparkline on the right ────────────────────────────────
        int divX = 155;
        tft.drawFastVLine(divX, ry + 2, rowH - 4, g_themeColor);

        int sparkX = divX + 4, sparkW = SCREEN_W - sparkX - 4;
        int sparkH = rowH - 16;
        if (sparkH > 10 && c.spark7dCount >= 2) {
            drawSpark(tft, sparkX, ry + 4, sparkW, sparkH,
                      c.spark7d, c.spark7dCount, c.change7d, true);
        }
        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(sparkX, ry + rowH - 12);
        tft.print("7D");
    }
}

// ── 3-4 COINS — Compact mode ─────────────────────────────────────────────
static void drawCompactCoins(TFT_eSPI &tft, int contentY, int contentH,
                             const CoinData **coins, int count) {
    int rowH = contentH / count;

    for (int i = 0; i < count; i++) {
        if (!coins[i] || !coins[i]->valid) continue;
        const CoinData &c = *coins[i];
        int ry = contentY + i * rowH;

        if (i > 0)
            tft.drawFastHLine(0, ry, SCREEN_W, COL_DIM);

        int midY = ry + rowH / 2;

        // Symbol
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(6, midY - 8);
        tft.print(c.symbol);

        // Price
        char priceBuf[20];
        formatPrice(c.priceUsd, priceBuf, sizeof(priceBuf));
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(58, midY - 8);
        tft.print(priceBuf);

        // Mini sparkline (7d)
        int sparkW = 76;
        int sparkX = 150;
        int sparkH = rowH - 6;
        if (c.spark7dCount >= 2) {
            drawSpark(tft, sparkX, ry + 2, sparkW, sparkH,
                      c.spark7d, c.spark7dCount, c.change7d, true);  // always themed
        }

        // 24h change
        char chg[14];
        snprintf(chg, sizeof(chg), "%+.1f%%", c.change24h);
        tft.setTextFont(FONT_MD);
        tft.setTextColor((c.change24h >= 0) ? COL_GREEN : COL_RED, COL_BG);
        int cw = tft.textWidth(chg);
        tft.setCursor(SCREEN_W - cw - 6, midY - 8);
        tft.print(chg);
    }
}

// ── Main draw ──────────────────────────────────────────────────────────────
void screenCryptoDraw(TFT_eSPI &tft, bool wifiOk, int page) {
    s_tappedCoinId[0] = '\0';

    char timeStr[10]; timeGetShort(timeStr);

    char pageLabel[16];
    snprintf(pageLabel, sizeof(pageLabel), "PAGE %d", page + 1);

    // Date string for bottombar: "Friday, Jun 13, 2026"
    char dateStr[32];
    timeGetDateLong(dateStr, sizeof(dateStr));

    drawTopbar(tft, "CRYPTO", pageLabel, timeStr, wifiOk);

    // Status bar
    int fg = cryptoGetFearGreed();
    char centerBuf[32] = "";
    if (fg >= 0) {
        char fgLabel[16];
        cryptoGetFearGreedLabel(fgLabel, sizeof(fgLabel));
        snprintf(centerBuf, sizeof(centerBuf), "F&G: %d %s", fg, fgLabel);
    }

    char rightBuf[28];
    char syncT[12];
    cryptoGetSyncTime(syncT, sizeof(syncT));
    snprintf(rightBuf, sizeof(rightBuf), "%s %s",
             cryptoIsFromCache() ? "CACHED" : "LIVE", syncT);

    drawStatusbar(tft, "COINGECKO", centerBuf, rightBuf);

    // Content area — snug below status bar (no gap, statusbar is 14px)
    int contentY = TOPBAR_H + 14;
    int contentH = SCREEN_H - contentY - BOTBAR_H;
    tft.fillRect(0, contentY, SCREEN_W, contentH, COL_BG);

    // Count active coins on this page
    int coinCount = cryptoPageCountActive(page);

    if (coinCount == 0) {
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(40, contentY + contentH / 2 - 12);
        tft.print("No coins on this page");
        tft.setCursor(20, contentY + contentH / 2 + 8);
        tft.print("Settings > Manage Coins");
        drawBottombar(tft, dateStr, page, MAX_CRYPTO_PAGES);
        return;
    }

    // Collect valid coin data pointers
    const CoinData *loadedCoins[MAX_COINS_PER_PAGE] = {};
    bool allLoaded = true;
    for (int s = 0; s < coinCount; s++) {
        int idx = cryptoPageGetCoin(page, s);
        if (idx >= 0) {
            loadedCoins[s] = cryptoGetCoinData(g_top50[idx].id);
            if (!loadedCoins[s] || !loadedCoins[s]->valid) allLoaded = false;
        } else {
            // Slot counted as active but no coin assigned — NVS may be stale
            allLoaded = false;
        }
    }

    if (!allLoaded) {
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(60, contentY + contentH / 2 - 8);
        tft.print("Loading data...");
        drawBottombar(tft, dateStr, page, MAX_CRYPTO_PAGES);
        return;
    }

    // Draw based on coin count
    if (coinCount == 1) {
        drawOneCoin(tft, contentY, contentH, *loadedCoins[0]);
    } else if (coinCount == 2) {
        drawTwoCoins(tft, contentY, contentH, *loadedCoins[0], *loadedCoins[1]);
    } else {
        drawCompactCoins(tft, contentY, contentH, loadedCoins, coinCount);
    }

    drawBottombar(tft, dateStr, page, MAX_CRYPTO_PAGES);
}

// ── Tap handling ──────────────────────────────────────────────────────────
void screenCryptoTap(TFT_eSPI &tft, int16_t x, int16_t y, int page, bool wifiOk) {
    (void)tft;
    (void)wifiOk;

    s_tappedCoinId[0] = '\0';

    int contentY = TOPBAR_H + 15;
    int contentH = SCREEN_H - contentY - BOTBAR_H;
    int coinCount = cryptoPageCountActive(page);
    if (coinCount == 0) return;

    // For 1-coin pages: tap cycles graph mode inline (no detail screen)
    if (coinCount == 1) {
        if (y >= contentY && y < contentY + contentH) {
            s_oneCoinGraphMode = (s_oneCoinGraphMode + 1) % 2;  // 24H ↔ 7D
        }
        return;
    }

    // For multi-coin pages: tap does nothing for now (detail screen disabled)
    // The mini sparklines already show the 7D trend inline.
    (void)x; (void)y;  // unused in multi-coin case
    return;
}

const char* screenCryptoGetTappedCoin() {
    return s_tappedCoinId[0] ? s_tappedCoinId : nullptr;
}
