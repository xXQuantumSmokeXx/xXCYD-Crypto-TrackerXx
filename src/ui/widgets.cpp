#include "widgets.h"
#include "theme.h"
#include "../config/config.h"
#include "../modules/brightness.h"
#include <cstring>
#include <cstdio>

// Arm lengths for corner bracket ticks
#define TK_H  10
#define TK_V   6
#define STATUSBAR_H 14

void drawTopbar(TFT_eSPI &tft, const char *leftLabel, const char *screenLabel, const char *timeStr, bool wifiOk) {
    tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, COL_BG);

    // ── Left label (e.g. "CRYPTO") — themed ───────────────────────────────
    if (leftLabel && leftLabel[0]) {
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(TK_H + 3, 3);
        tft.print(leftLabel);
    }

    // ── Screen label — centered, drawn BEFORE time ────────────────────────
    if (screenLabel && screenLabel[0]) {
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        int lw = tft.textWidth(screenLabel);
        tft.setCursor((SCREEN_W - lw) / 2, 3);
        tft.print(screenLabel);
    }

    // ── Time — right side, drawn LAST with full background clear ──────────
    tft.setTextFont(FONT_MD);
    int tw = tft.textWidth(timeStr);
    int timeX = SCREEN_W - tw - TK_H - 3;
    if (timeX < 160) timeX = 160;
    // Aggressive clear behind time — wider to cover any ghosting
    tft.fillRect(timeX - 6, 0, tw + 16, TOPBAR_H - 1, COL_BG);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(timeX, 3);
    tft.print(timeStr);

    // ── Solid border at bottom of topbar ───────────────────────────────────
    tft.drawFastHLine(0, TOPBAR_H - 1, SCREEN_W, g_themeColor);

    // ── Corner bracket ticks ──────────────────────────────────────────────
    tft.drawFastHLine(0,              0, TK_H, g_themeColor);
    tft.drawFastVLine(0,              0, TK_V, g_themeColor);
    tft.drawFastHLine(SCREEN_W - TK_H, 0, TK_H, g_themeColor);
    tft.drawFastVLine(SCREEN_W - 1,    0, TK_V, g_themeColor);

    // ── Small filled squares flanking the label on the border line ─────────
    if (screenLabel && screenLabel[0]) {
        int lw = tft.textWidth(screenLabel);
        int lx = (SCREEN_W - lw) / 2;
        tft.fillRect(lx - 6,      TOPBAR_H - 5, 3, 4, g_themeColor);
        tft.fillRect(lx + lw + 3, TOPBAR_H - 5, 3, 4, g_themeColor);
    }
}

void drawTopbarTime(TFT_eSPI &tft, const char *timeStr, const char *screenLabel) {
    // Clear center + right portion — keep clear of left label (starts ~x=13, ends ~x=73)
    tft.fillRect(82, 0, SCREEN_W - 82, TOPBAR_H - 1, COL_BG);

    // ── Screen label — centered (drawn BEFORE time) ───────────────────────
    if (screenLabel && screenLabel[0]) {
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        int lw = tft.textWidth(screenLabel);
        tft.setCursor((SCREEN_W - lw) / 2, 3);
        tft.print(screenLabel);
    }

    // ── Time — right side (drawn LAST, with clean background) ─────────────
    tft.setTextFont(FONT_MD);
    int tw = tft.textWidth(timeStr);
    int timeX = SCREEN_W - tw - TK_H - 3;
    if (timeX < 160) timeX = 160;
    // Clear behind time to cover any label overlap
    tft.fillRect(timeX - 4, 0, tw + 12, TOPBAR_H - 1, COL_BG);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(timeX, 3);
    tft.print(timeStr);

    // Refresh themed corner ticks (right side only)
    tft.drawFastHLine(SCREEN_W - TK_H, 0, TK_H, g_themeColor);
    tft.drawFastVLine(SCREEN_W - 1,    0, TK_V, g_themeColor);
}

void drawBottombar(TFT_eSPI &tft, const char *label, int activeScreen, int totalScreens) {
    int y0 = SCREEN_H - BOTBAR_H;
    tft.fillRect(0, y0, SCREEN_W, BOTBAR_H, COL_BG);

    int my = y0 + BOTBAR_H / 2;

    // ── Navigation arrows ──────────────────────────────────────────────────
    tft.setTextFont(FONT_MD);
    int arrowW = tft.textWidth(">");

    uint16_t lCol = (activeScreen > 0) ? g_themeColor : COL_DIM;
    tft.setTextColor(lCol, COL_BG);
    tft.setCursor(TK_H + 2, my - 8);
    tft.print("<");

    int rarrowX = SCREEN_W - TK_H - 2 - arrowW;
    uint16_t rCol = (activeScreen < totalScreens - 1) ? g_themeColor : COL_DIM;
    tft.setTextColor(rCol, COL_BG);
    tft.setCursor(rarrowX, my - 8);
    tft.print(">");

    // ── Battery % ─────────────────────────────────────────────────────────
    int batt = batteryPct();
    if (batt >= 0) {
        char bbuf[8];
        snprintf(bbuf, sizeof(bbuf), "%d%%", batt);
        tft.setTextFont(FONT_MD);
        int bw = tft.textWidth(bbuf);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(rarrowX - bw - 4, my - 8);
        tft.print(bbuf);
    }

    // ── Page indicator dots or centered label ──────────────────────────────
    if (!label || !label[0]) {
        const int DS = 4, DG = 6;
        int total = totalScreens * DS + (totalScreens - 1) * DG;
        int dx = (SCREEN_W - total) / 2;
        int dy = my - DS / 2;
        for (int i = 0; i < totalScreens; i++) {
            if (i == activeScreen)
                tft.fillRect(dx, dy, DS, DS, g_themeColor);
            else
                tft.drawRect(dx, dy, DS, DS, COL_DIM);
            dx += DS + DG;
        }
    } else {
        // Split "Friday, Jun 13, 2026" → day left, rest centered (like CYD-Weather)
        const char *comma = strchr(label, ',');
        if (comma) {
            char dayBuf[12];
            size_t dayLen = comma - label;
            if (dayLen > sizeof(dayBuf) - 1) dayLen = sizeof(dayBuf) - 1;
            memcpy(dayBuf, label, dayLen);
            dayBuf[dayLen] = '\0';

            tft.setTextFont(FONT_MD);
            tft.setTextColor(g_themeColor, COL_BG);
            tft.setCursor(TK_H + 2 + arrowW + 6, my - 8);
            tft.print(dayBuf);

            const char *rest = comma + 2;  // skip ", "
            int rw = tft.textWidth(rest);
            tft.setTextColor(g_themeColor, COL_BG);
            tft.setCursor((SCREEN_W - rw) / 2, my - 8);
            tft.print(rest);
        } else {
            tft.setTextFont(FONT_MD);
            tft.setTextColor(g_themeColor, COL_BG);
            int lw = tft.textWidth(label);
            tft.setCursor((SCREEN_W - lw) / 2, my - 8);
            tft.print(label);
        }
    }

    // ── Solid border at top of bottombar ──────────────────────────────────
    tft.drawFastHLine(0, y0, SCREEN_W, g_themeColor);

    // ── Corner bracket ticks ──────────────────────────────────────────────
    tft.drawFastHLine(0,              SCREEN_H - 1, TK_H, g_themeColor);
    tft.drawFastVLine(0,              SCREEN_H - TK_V, TK_V, g_themeColor);
    tft.drawFastHLine(SCREEN_W - TK_H, SCREEN_H - 1, TK_H, g_themeColor);
    tft.drawFastVLine(SCREEN_W - 1,    SCREEN_H - TK_V, TK_V, g_themeColor);
}

void drawStatusbar(TFT_eSPI &tft, const char *source, const char *center, const char *right) {
    int y = TOPBAR_H;
    tft.fillRect(0, y, SCREEN_W, STATUSBAR_H, COL_BG);

    tft.setTextFont(FONT_SM);

    // Left: data source
    if (source && source[0]) {
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(4, y + 3);
        tft.print(source);
    }

    // Center: e.g. Fear & Greed
    if (center && center[0]) {
        tft.setTextColor(g_themeColor, COL_BG);
        int cw = tft.textWidth(center);
        tft.setCursor((SCREEN_W - cw) / 2, y + 3);
        tft.print(center);
    }

    // Right: e.g. LIVE/CACHED + timestamp
    if (right && right[0]) {
        tft.setTextColor(g_themeColor, COL_BG);
        int rw = tft.textWidth(right);
        tft.setCursor(SCREEN_W - rw - 4, y + 3);
        tft.print(right);
    }

    tft.drawFastHLine(0, y + STATUSBAR_H - 1, SCREEN_W, g_themeColor);
}

void drawPctChip(TFT_eSPI &tft, int x, int y, float pct, bool compact) {
    char buf[12];
    if (compact)
        snprintf(buf, sizeof(buf), "%+.1f%%", pct);
    else
        snprintf(buf, sizeof(buf), "%+.2f%%", pct);

    uint16_t col = (pct >= 0) ? COL_GREEN : COL_RED;
    tft.setTextFont(compact ? FONT_SM : FONT_MD);
    tft.setTextColor(col, COL_BG);
    tft.setCursor(x, y);
    tft.print(buf);
}
