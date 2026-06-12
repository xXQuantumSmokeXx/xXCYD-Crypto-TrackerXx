#include "screen_detail.h"
#include "../theme.h"
#include "../theme_color.h"
#include "../widgets.h"
#include "../../config/config.h"
#include "../../modules/crypto.h"
#include "../../modules/time_sync.h"
#include <cstdio>
#include <cstring>

static bool  s_shouldExit = false;
static int   s_graphTab  = 0;   // 0=24H, 1=7D
static const char *s_tabLabels[] = {"24H", "7D"};

// ── Draw full sparkline with baseline and grid ─────────────────────────────
static void drawFullSpark(TFT_eSPI &tft, int x, int y, int w, int h,
                          const float *data, int count, float change) {
    if (count < 2) {
        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(x + w / 2 - 30, y + h / 2 - 4);
        tft.print("No data");
        return;
    }

    tft.fillRect(x, y, w, h + 1, COL_BG);

    float mn = data[0], mx = data[0];
    for (int i = 1; i < count; i++) {
        if (data[i] < mn) mn = data[i];
        if (data[i] > mx) mx = data[i];
    }
    float range = mx - mn;
    if (range < 1e-6f) range = 1.0f;

    // Grid lines
    for (int g = 0; g <= 3; g++) {
        int gy = y + g * (h / 3);
        tft.drawFastHLine(x, gy, w, COL_DIM);
    }

    uint16_t col = (change >= 0) ? g_themeColor : COL_RED;

    // Filled area under the curve
    int px = -1, py = -1;
    int firstX = -1;
    for (int i = 0; i < count; i++) {
        int cx = x + (int)((float)i * (w - 1) / (count - 1));
        int cy = y + h - 1 - (int)(((data[i] - mn) / range) * (h - 4));
        if (cy < y)         cy = y;
        if (cy > y + h - 1) cy = y + h - 1;
        if (firstX < 0) firstX = cx;
        if (px >= 0) {
            tft.drawLine(px, py, cx, cy, col);
            // Fill below
            for (int fy = cy + 1; fy <= y + h - 1; fy++)
                tft.drawPixel(cx, fy, COL_BG);
            tft.drawLine(cx, cy + 1, cx, y + h - 1, COL_DIM);
        }
        px = cx; py = cy;
    }

    // Baseline
    tft.drawFastHLine(x, y + h, w + 1, g_themeColor);

    // Min/max labels
    tft.setTextFont(FONT_SM);
    tft.setTextColor(g_themeColor, COL_BG);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", mn);
    tft.setCursor(x, y + h - 8);
    tft.print(buf);
    snprintf(buf, sizeof(buf), "%.2f", mx);
    int mw = tft.textWidth(buf);
    tft.setCursor(x + w - mw, y + 2);
    tft.print(buf);
}

// ── Main detail draw ──────────────────────────────────────────────────────
void screenDetailDraw(TFT_eSPI &tft, bool wifiOk, const char *coinId) {
    s_shouldExit = false;

    char timeStr[10]; timeGetShort(timeStr);

    const CoinData *c = cryptoGetCoinData(coinId);
    if (!c || !c->valid) {
        drawTopbar(tft, "< BACK", "DETAIL", timeStr, wifiOk);
        tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(60, CONTENT_Y + 60);
        tft.print("Coin data not loaded");
        drawBottombar(tft, "[TAP TO GO BACK]", 0, 1);
        return;
    }

    char title[40];
    snprintf(title, sizeof(title), "%s", c->symbol);
    drawTopbar(tft, "< BACK", title, timeStr, wifiOk);

    int cy = CONTENT_Y;
    tft.fillRect(0, cy, SCREEN_W, CONTENT_H, COL_BG);

    // ── Price header ───────────────────────────────────────────────────────
    char priceBuf[20];
    // formatPrice is static in screen_crypto.cpp — duplicate inline here
    auto fmtPrice = [](double price, char *out, int outLen) {
        char tmp[24];
        if (price < 0.001)      snprintf(tmp, sizeof(tmp), "%.6f", price);
        else if (price < 1.0)   snprintf(tmp, sizeof(tmp), "%.4f", price);
        else if (price < 100.0) snprintf(tmp, sizeof(tmp), "%.2f", price);
        else                    snprintf(tmp, sizeof(tmp), "%.0f", price);
        int intLen = 0;
        while (tmp[intLen] && tmp[intLen] != '.') intLen++;
        int o = 0;
        out[o++] = '$';
        for (int i = 0; tmp[i] && o < outLen - 1; i++) {
            if (i < intLen && i > 0 && (intLen - i) % 3 == 0) out[o++] = ',';
            out[o++] = tmp[i];
        }
        out[o] = '\0';
    };
    fmtPrice(c->priceUsd, priceBuf, sizeof(priceBuf));

    tft.setTextFont(FONT_LG);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(4, cy + 4);
    tft.print(priceBuf);

    // Name
    tft.setTextFont(FONT_SM);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(4, cy + 32);
    tft.print(c->name);
    tft.print(" (");
    tft.print(c->symbol);
    tft.print(")");

    // ── Change stats row ────────────────────────────────────────────────────
    int statY = cy + 48;
    tft.setTextFont(FONT_MD);
    drawPctChip(tft, 4,   statY, c->change24h, false);
    drawPctChip(tft, 110, statY, c->change7d, false);

    tft.setTextFont(FONT_MD);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(4,   statY + 18); tft.print("24H");
    tft.setCursor(110, statY + 18); tft.print("7D");

    // ── High / Low — stacked, FONT_MD ──────────────────────────────────────
    int hlY = statY + 44;
    char hlBuf[20];

    // High line
    tft.setTextFont(FONT_MD);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(4, hlY);
    tft.print("HIGH  ");
    tft.setTextColor(COL_GREEN, COL_BG);
    snprintf(hlBuf, sizeof(hlBuf), "$%.2f", (double)c->high24h);
    tft.print(hlBuf);

    // Low line
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(4, hlY + 18);
    tft.print("LOW   ");
    tft.setTextColor(COL_RED, COL_BG);
    snprintf(hlBuf, sizeof(hlBuf), "$%.2f", (double)c->low24h);
    tft.print(hlBuf);

    // ── Graph tabs (FONT_MD, slightly taller) ──────────────────────────────
    int tabY = hlY + 42;
    for (int t = 0; t < 2; t++) {
        int tx = 80 + t * 66;
        tft.fillRect(tx, tabY, 60, 20, COL_INPUTBG);
        if (t == s_graphTab) {
            tft.drawRect(tx - 1, tabY - 1, 62, 22, g_themeColor);
            tft.setTextColor(g_themeColor, COL_INPUTBG);
        } else {
            tft.drawRect(tx, tabY, 60, 20, COL_DIM);
            tft.setTextColor(COL_DIM, COL_INPUTBG);
        }
        tft.setTextFont(FONT_MD);
        int tw = tft.textWidth(s_tabLabels[t]);
        tft.setCursor(tx + (60 - tw) / 2, tabY + 2);
        tft.print(s_tabLabels[t]);
    }

    // ── Graph area ─────────────────────────────────────────────────────────
    int graphY = tabY + 28;
    int graphH = SCREEN_H - graphY - BOTBAR_H - 4;
    int graphW = SCREEN_W - 8;

    const float *sparkData = nullptr;
    int sparkCount = 0;
    float sparkChange = 0;

    switch (s_graphTab) {
        case 0:  // 7D
            sparkData   = c->spark7d;
            sparkCount  = c->spark7dCount;
            sparkChange = c->change7d;
            break;
        case 1:  // 24H — show last 24 points of 7d sparkline
            if (c->spark7dCount >= 24) {
                sparkData  = c->spark7d + c->spark7dCount - 24;
                sparkCount = 24;
            } else {
                sparkData  = c->spark7d;
                sparkCount = c->spark7dCount;
            }
            sparkChange = c->change24h;
            break;
    }

    drawFullSpark(tft, 4, graphY, graphW, graphH, sparkData, sparkCount, sparkChange);

    // ── Bottombar: just back indicator ──────────────────────────────────────
    tft.fillRect(0, SCREEN_H - BOTBAR_H, SCREEN_W, BOTBAR_H, COL_BG);
    tft.drawFastHLine(0, SCREEN_H - BOTBAR_H, SCREEN_W, g_themeColor);
    tft.setTextFont(FONT_SM);
    tft.setTextColor(COL_DIM, COL_BG);
    const char *hint = "[TAP ANYWHERE TO GO BACK]";
    int hw = tft.textWidth(hint);
    tft.setCursor((SCREEN_W - hw) / 2, SCREEN_H - BOTBAR_H + 4);
    tft.print(hint);
}

// ── Tap handling ──────────────────────────────────────────────────────────
void screenDetailTap(TFT_eSPI &tft, int16_t x, int16_t y, bool wifiOk) {
    (void)tft;
    (void)wifiOk;

    // Check if tap is in graph tab area
    // position matches the updated layout in screenDetailDraw
    int tabY = CONTENT_Y + 48 + 44 + 42;  // price + stat + high/low + gap
    if (y >= tabY && y < tabY + 22) {
        for (int t = 0; t < 2; t++) {
            int tx = 80 + t * 66;
            if (x >= tx && x < tx + 60) {
                s_graphTab = t;
                return;  // tab changed, redraw will pick up new tab
            }
        }
    }

    // Any other tap = go back
    s_shouldExit = true;
}

bool screenDetailShouldExit() {
    if (s_shouldExit) {
        s_shouldExit = false;
        s_graphTab = 0;
        return true;
    }
    return false;
}
