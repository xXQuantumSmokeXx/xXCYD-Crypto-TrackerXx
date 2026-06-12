#include "screen_coinpicker.h"
#include "../theme.h"
#include "../theme_color.h"
#include "../widgets.h"
#include "../../config/config.h"
#include "../../modules/crypto.h"
#include "../../modules/time_sync.h"
#include <cstdio>
#include <cstring>

static int  s_scrollOffset = 0;
static int  s_selectedPage = 0;
static bool s_shouldExit   = false;
static bool s_showFullMsg  = false;
static unsigned long s_fullMsgMs = 0;

#define VISIBLE_ROWS  8
#define HEADER_Y      CONTENT_Y
#define PAGE_ROW_H    18
#define LABEL_H       14
#define LIST_Y        (HEADER_Y + PAGE_ROW_H + LABEL_H + 4)
#define ROW_H         22

// ── Draw ──────────────────────────────────────────────────────────────────
void screenCoinPickerDraw(TFT_eSPI &tft, bool wifiOk) {
    char timeStr[10]; timeGetShort(timeStr);
    drawTopbar(tft, "< SETTINGS", "COIN PICKER", timeStr, wifiOk);
    drawBottombar(tft, "TAP COIN TO ADD/REMOVE", 0, 1);

    int contentH = SCREEN_H - CONTENT_Y - BOTBAR_H;
    tft.fillRect(0, CONTENT_Y, SCREEN_W, contentH, COL_BG);

    // ── Page toggle row: P1-P5 (gapless, wider tabs for easy tapping) ────
    int pageBtnW = (SCREEN_W - 12) / MAX_CRYPTO_PAGES;  // ~61px each, no gaps
    int pageBtnH = PAGE_ROW_H;
    int pageRowY = HEADER_Y;

    tft.setTextFont(FONT_SM);
    for (int p = 0; p < MAX_CRYPTO_PAGES; p++) {
        int px = 6 + p * pageBtnW;  // tabs butt against each other
        bool enabled = cryptoPageIsEnabled(p);
        bool selected = (p == s_selectedPage);

        tft.fillRect(px, pageRowY, pageBtnW - 1, pageBtnH, COL_INPUTBG);

        if (selected) {
            // Selected page gets theme border
            tft.drawRect(px - 1, pageRowY - 1, pageBtnW + 1, pageBtnH + 2, g_themeColor);
        }

        if (enabled) {
            tft.setTextColor(g_themeColor, COL_INPUTBG);
        } else {
            tft.setTextColor(COL_DIM, COL_INPUTBG);
            tft.drawRect(px, pageRowY, pageBtnW - 1, pageBtnH, COL_DIM);
        }

        char pl[6];
        int coinCount = cryptoPageCountActive(p);
        snprintf(pl, sizeof(pl), "P%d:%d", p + 1, coinCount);
        int pw = tft.textWidth(pl);
        tft.setCursor(px + (pageBtnW - pw) / 2, pageRowY + (pageBtnH - 8) / 2);
        tft.print(pl);
    }

    // ── Info label ─────────────────────────────────────────────────────────
    int labelY = pageRowY + PAGE_ROW_H + 6;  // +5px from original
    tft.setTextFont(FONT_SM);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(6, labelY);
    int active = cryptoPageCountActive(s_selectedPage);
    tft.printf("Page %d — %d/4 coins", s_selectedPage + 1, active);

    // "FULL" flash message
    if (s_showFullMsg && millis() - s_fullMsgMs < 2000) {
        tft.setTextColor(COL_AMBER, COL_BG);
        tft.setCursor(180, labelY);
        tft.print("FULL!");
    }

    // ── Coin list ──────────────────────────────────────────────────────────
    int listY = LIST_Y;
    int remainingH = SCREEN_H - listY - BOTBAR_H - 20;  // 20px for bottom buttons
    int visibleRows = remainingH / ROW_H;
    if (visibleRows < 3) visibleRows = 3;
    if (visibleRows > VISIBLE_ROWS) visibleRows = VISIBLE_ROWS;

    int visibleStart = s_scrollOffset;
    int visibleEnd   = visibleStart + visibleRows;
    int totalCoins = TOP50_COUNT + g_customCoinCount;
    if (visibleEnd > totalCoins) visibleEnd = totalCoins;

    for (int i = visibleStart; i < visibleEnd; i++) {
        int ry = listY + (i - visibleStart) * ROW_H;

        // Find if this coin is assigned on any page
        int assignedPage = -1;
        for (int p = 0; p < MAX_CRYPTO_PAGES; p++) {
            if (cryptoPageHasCoin(p, i)) {
                assignedPage = p;
                break;
            }
        }

        bool onThisPage = (assignedPage == s_selectedPage);
        bool onOtherPage = (assignedPage >= 0 && assignedPage != s_selectedPage);

        // Row background — subtle striping
        uint16_t rowBg = (i % 2 == 0) ? COL_INPUTBG : COL_BG;
        tft.fillRect(0, ry, SCREEN_W, ROW_H, rowBg);

        // Checkbox
        int chkX = 6, chkY = ry + (ROW_H - 10) / 2;
        tft.drawRect(chkX, chkY, 10, 10, onThisPage ? g_themeColor : COL_DIM);
        if (onThisPage) {
            // Filled checkbox
            tft.fillRect(chkX + 2, chkY + 2, 6, 6, g_themeColor);
        } else if (onOtherPage) {
            // Half-filled — small dot to indicate "on another page"
            tft.fillRect(chkX + 3, chkY + 3, 4, 4, COL_DIM);
        }

        // Symbol
        const CoinDef *def = coinGetDef(i);
        if (!def) continue;
        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, rowBg);
        tft.setCursor(chkX + 14, ry + (ROW_H - 8) / 2);
        tft.print(def->symbol);

        // Name
        tft.setTextColor(g_themeColor, rowBg);
        tft.setCursor(62, ry + (ROW_H - 8) / 2);
        tft.print(def->name);

        // Assignment indicator
        if (onOtherPage) {
            char aBuf[8];
            snprintf(aBuf, sizeof(aBuf), "[P%d]", assignedPage + 1);
            int aw = tft.textWidth(aBuf);
            tft.setTextColor(g_themeColor, rowBg);
            tft.setCursor(SCREEN_W - aw - 6, ry + (ROW_H - 8) / 2);
            tft.print(aBuf);
        }
    }

    // ── Scroll indicators ──────────────────────────────────────────────────
    if (visibleStart > 0) {
        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(SCREEN_W - 14, listY - 8);
        tft.print("^");
    }
    if (visibleEnd < totalCoins) {
        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, COL_BG);
        int botY = SCREEN_H - BOTBAR_H - 22;
        tft.setCursor(SCREEN_W - 14, botY);
        tft.print("v");
    }

    // ── Bottom action buttons ──────────────────────────────────────────────
    int btnY = SCREEN_H - BOTBAR_H - 18;
    // Clear Page button
    tft.fillRect(6, btnY, 90, 16, COL_INPUTBG);
    tft.drawRect(6, btnY, 90, 16, COL_RED);
    tft.setTextFont(FONT_SM);
    tft.setTextColor(COL_RED, COL_INPUTBG);
    const char *clr = "CLEAR PAGE";
    int cw = tft.textWidth(clr);
    tft.setCursor(6 + (90 - cw) / 2, btnY + 3);
    tft.print(clr);

    // Done button
    int doneX = SCREEN_W - 56;
    tft.fillRect(doneX, btnY, 50, 16, COL_INPUTBG);
    tft.drawRect(doneX, btnY, 50, 16, g_themeColor);
    tft.setTextColor(g_themeColor, COL_INPUTBG);
    const char *dn = "DONE";
    int dw = tft.textWidth(dn);
    tft.setCursor(doneX + (50 - dw) / 2, btnY + 3);
    tft.print(dn);
}

// ── Tap handling ──────────────────────────────────────────────────────────
void screenCoinPickerTap(TFT_eSPI &tft, int16_t x, int16_t y, bool wifiOk) {
    (void)tft;
    (void)wifiOk;

    // ── Page toggle row ────────────────────────────────────────────────────
    int pageRowY = HEADER_Y;
    int pageBtnW = (SCREEN_W - 12) / MAX_CRYPTO_PAGES;  // gapless, wider
    int pageBtnH = PAGE_ROW_H;

    if (y >= pageRowY && y < pageRowY + pageBtnH) {
        for (int p = 0; p < MAX_CRYPTO_PAGES; p++) {
            int px = 6 + p * pageBtnW;  // tabs butt against each other
            if (x >= px && x < px + pageBtnW - 1) {
                if (p == s_selectedPage) {
                    // Tap selected page = toggle enable/disable
                    // Don't allow disabling the last enabled page
                    bool currentlyEnabled = cryptoPageIsEnabled(p);
                    if (currentlyEnabled && cryptoGetEnabledPageCount() <= 1) {
                        return;  // can't disable last page
                    }
                    cryptoPageSetEnabled(p, !currentlyEnabled);
                } else {
                    // Tap different page = select it (and auto-enable if disabled)
                    s_selectedPage = p;
                    if (!cryptoPageIsEnabled(p)) {
                        cryptoPageSetEnabled(p, true);
                    }
                }
                s_scrollOffset = 0;
                return;
            }
        }
    }

    // ── Bottom buttons ─────────────────────────────────────────────────────
    int btnY = SCREEN_H - BOTBAR_H - 18;

    // Clear Page
    if (y >= btnY && y < btnY + 16 && x >= 6 && x < 96) {
        cryptoPageClear(s_selectedPage);
        // If clearing made page empty and it's not page 0, auto-disable it
        if (s_selectedPage != 0 && cryptoPageCountActive(s_selectedPage) == 0) {
            cryptoPageSetEnabled(s_selectedPage, false);
            // Select the first enabled page
            for (int p = 0; p < MAX_CRYPTO_PAGES; p++) {
                if (cryptoPageIsEnabled(p)) { s_selectedPage = p; break; }
            }
        }
        return;
    }

    // Done button
    int doneX = SCREEN_W - 56;
    if (y >= btnY && y < btnY + 16 && x >= doneX && x < doneX + 50) {
        s_shouldExit = true;
        return;
    }

    // ── Coin list ──────────────────────────────────────────────────────────
    int listY = LIST_Y;
    int contentH = SCREEN_H - CONTENT_Y - BOTBAR_H;
    int remainingH = SCREEN_H - listY - BOTBAR_H - 20;
    int visibleRows = remainingH / ROW_H;
    if (visibleRows < 3) visibleRows = 3;
    if (visibleRows > VISIBLE_ROWS) visibleRows = VISIBLE_ROWS;

    for (int i = 0; i < visibleRows; i++) {
        int idx = s_scrollOffset + i;
        if (idx >= coinTotalCount()) break;
        int ry = listY + i * ROW_H;
        if (y >= ry && y < ry + ROW_H) {
            // Toggle coin on current page
            if (cryptoPageHasCoin(s_selectedPage, idx)) {
                // Remove from this page
                for (int s = 0; s < MAX_COINS_PER_PAGE; s++) {
                    if (cryptoPageGetCoin(s_selectedPage, s) == idx) {
                        cryptoPageSetCoin(s_selectedPage, s, -1);
                        break;
                    }
                }
                s_showFullMsg = false;
            } else {
                // Add to first free slot
                int active = cryptoPageCountActive(s_selectedPage);
                if (active >= MAX_COINS_PER_PAGE) {
                    s_showFullMsg = true;
                    s_fullMsgMs = millis();
                    return;
                }
                // If coin is on another page, remove it from there first
                for (int p = 0; p < MAX_CRYPTO_PAGES; p++) {
                    if (p != s_selectedPage && cryptoPageHasCoin(p, idx)) {
                        for (int s = 0; s < MAX_COINS_PER_PAGE; s++) {
                            if (cryptoPageGetCoin(p, s) == idx) {
                                cryptoPageSetCoin(p, s, -1);
                                break;
                            }
                        }
                    }
                }
                // Find first free slot
                for (int s = 0; s < MAX_COINS_PER_PAGE; s++) {
                    if (cryptoPageGetCoin(s_selectedPage, s) < 0) {
                        cryptoPageSetCoin(s_selectedPage, s, idx);
                        break;
                    }
                }
                // Auto-enable page when first coin is added
                if (!cryptoPageIsEnabled(s_selectedPage)) {
                    cryptoPageSetEnabled(s_selectedPage, true);
                }
                s_showFullMsg = false;
            }
            return;
        }
    }

    // Tap on bottombar = go back
    if (y >= SCREEN_H - BOTBAR_H) {
        s_shouldExit = true;
    }
}

void screenCoinPickerSwipe(int dir) {
    int listY = LIST_Y;
    int remainingH = SCREEN_H - listY - BOTBAR_H - 20;
    int visibleRows = remainingH / ROW_H;
    if (visibleRows < 3) visibleRows = 3;
    if (visibleRows > VISIBLE_ROWS) visibleRows = VISIBLE_ROWS;

    if (dir > 0) {
        s_scrollOffset += 3;
        int maxOff = coinTotalCount() - visibleRows;
        if (s_scrollOffset > maxOff) s_scrollOffset = maxOff;
    } else {
        s_scrollOffset -= 3;
        if (s_scrollOffset < 0) s_scrollOffset = 0;
    }
}

bool screenCoinPickerShouldExit() {
    if (s_shouldExit) {
        s_shouldExit = false;
        return true;
    }
    return false;
}
