#include "screen_settings.h"
#include "../theme.h"
#include "../theme_color.h"
#include "../widgets.h"
#include "../../config/config.h"
#include "../../config/nvs_config.h"
#include "../../modules/time_sync.h"
#include "../../modules/brightness.h"
#include "../../touch/touch.h"
#include <esp_sleep.h>
#include <cstdio>
#include <cstring>

// ── Layout constants ──────────────────────────────────────────────────────────
#define SEC2_X       8
#define SEC2_W       (SCREEN_W - 2 * SEC2_X)   // 304

// Section 1 — Sleep Timer
#define SLP_LABEL_Y   (CONTENT_Y + 2)
#define SLP_BTN_Y0    38    // moved down 5px
#define SLP_BTN_H     13
#define SLP_BTN_W     28
#define SLP_COUNT      5
#define SLP_GAP        2

// Schedule row
#define SCHED_BTN_Y    59    // moved down 3px
#define SCHED_PAD      3
#define SLEEP_W        64
#define SCHED_GAP      2
#define SCHED_COUNT    5
#define WAKE_COUNT     5

static int s_schedX1 = 0, s_schedW = 0;
static int s_schedX2 = 0, s_sleepW = 0;
static int s_schedX3 = 0, s_wakeW  = 0;

// Right column — 2×2: E-Ink + Power on top row, Manage Coins + (empty) on bottom
#define BTN_X1       180
#define BTN_X2       (BTN_X1 + 65 + 2)   // 247
#define BTN_SQ_W     65
#define BTN_SQ_H     18

#define EINK_X       BTN_X1
#define EINK_Y       28    // moved down 3px
#define PWR_X        BTN_X2
#define PWR_Y        28    // moved down 3px
#define COINS_X      BTN_X1
#define COINS_Y      55    // moved up 2px
#define COINS_W      (BTN_X2 + BTN_SQ_W - BTN_X1)   // spans both columns

#define DIV1_Y       79

// Section 2 — Brightness + Rotate (labels up 5px, buttons stay)
#define BRI_LABEL_Y  84
#define BRI_BTN_Y0   97    // buttons unchanged
#define BRI_BTN_H    18
#define BRI_BTN_W    ((SEC2_W - (BRI_LEVELS - 1) * 3) / BRI_LEVELS)

#define ROT_LABEL_Y  125
#define ROT_BTN_Y0   138   // buttons unchanged
#define ROT_BTN_H    18
#define ROT_BTN_COUNT 5
#define ROT_BTN_W    ((SEC2_W - (ROT_BTN_COUNT - 1) * 3) / ROT_BTN_COUNT)

// Section 3 — Theme Color (label up 5px, swatches stay)
#define THEME_LABEL_Y 166
#define SWATCH_Y0     179   // swatches unchanged
#define SWATCH_H      20
#define SWATCH_W      ((SEC2_W - (THEME_COUNT - 1) * 2) / THEME_COUNT)
#define SWATCH_PAD    2

// ── Sleep constants ──────────────────────────────────────────────────────────
static const char *s_slpLabels[SLP_COUNT] = {"OFF","15s","30s","1m","5m"};
static const uint32_t s_slpSecs[SLP_COUNT] = {0, 15, 30, 60, 300};

static int s_slpTimer = -1;

static int slpCacheLoad() {
    if (s_slpTimer < 0) {
        s_slpTimer = nvsGetInt("slp_timer", 0);
        if (s_slpTimer < 0 || s_slpTimer >= SLP_COUNT) s_slpTimer = 0;
    }
    return s_slpTimer;
}

static void slpCacheSave() {
    nvsPutInt("slp_timer", s_slpTimer);
}

// ── Schedule state ──────────────────────────────────────────────────────────
static const int   s_sleepHours[SCHED_COUNT] = {20, 21, 22, 23, 0};
static const char *s_sleepHourLabels[SCHED_COUNT] = {"8PM","9PM","10PM","11PM","12AM"};
static const int   s_wakeHours[WAKE_COUNT]   = {5, 6, 7, 8, 9};
static const char *s_wakeHourLabels[WAKE_COUNT]   = {"5AM","6AM","7AM","8AM","9AM"};

static bool s_schedEnabled   = false;
static int  s_sleepHourIdx   = 2;    // default: 10PM
static int  s_wakeHourIdx    = 2;    // default: 7AM
static bool s_schedLoaded    = false;

static void schedLoad() {
    if (s_schedLoaded) return;
    s_schedEnabled = nvsGetInt("sched_en", 0) != 0;
    s_sleepHourIdx = nvsGetInt("sched_slp", 2);
    if (s_sleepHourIdx < 0 || s_sleepHourIdx >= SCHED_COUNT) s_sleepHourIdx = 2;
    s_wakeHourIdx  = nvsGetInt("sched_wke", 2);
    if (s_wakeHourIdx  < 0 || s_wakeHourIdx  >= WAKE_COUNT)  s_wakeHourIdx  = 2;
    s_schedLoaded = true;
}

static void schedSave() {
    nvsPutInt("sched_en", s_schedEnabled ? 1 : 0);
    nvsPutInt("sched_slp", s_sleepHourIdx);
    nvsPutInt("sched_wke", s_wakeHourIdx);
}

static const char *s_briLabels[BRI_LEVELS] = {"AUTO","DIM","LOW","MED","HIGH","MAX"};
static bool s_coinPickerRequested = false;
static bool s_pwrConfirm = false;
static unsigned long s_pwrConfirmMs = 0;

// ── Auto-rotate state ─────────────────────────────────────────────────────────
static const uint32_t s_rotMs[]     = { 0, 5000, 10000, 30000, 60000 };
static const char    *s_rotLabels[] = { "OFF", "5s", "10s", "30s", "1m" };
static int   s_autoRotSel    = 0;
static bool  s_autoRotLoaded = false;

static void autoRotLoad() {
    if (s_autoRotLoaded) return;
    s_autoRotSel = nvsGetInt("arot_sel", 0);
    if (s_autoRotSel < 0 || s_autoRotSel >= ROT_BTN_COUNT) s_autoRotSel = 0;
    s_autoRotLoaded = true;
}

// ── Drawing ───────────────────────────────────────────────────────────────────
void screenSettingsDraw(TFT_eSPI &tft, bool wifiOk) {
    char timeStr[10]; timeGetShort(timeStr);

    char dateStr[32];
    timeGetDateLong(dateStr, sizeof(dateStr));

    drawTopbar(tft, "CRYPTO", "SETTINGS", timeStr, wifiOk);
    drawBottombar(tft, dateStr, MAX_CRYPTO_PAGES, MAX_CRYPTO_PAGES + 1);
    tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);

    slpCacheLoad();

    // ── Section 1: Sleep Timer ──────────────────────────────────────────────────
    tft.setTextFont(FONT_SM);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(SEC2_X, SLP_LABEL_Y);
    tft.print("SLEEP TIMER");

    for (int i = 0; i < SLP_COUNT; i++) {
        int bx = SEC2_X + i * (SLP_BTN_W + SLP_GAP);
        int by = SLP_BTN_Y0;
        tft.fillRect(bx, by, SLP_BTN_W, SLP_BTN_H, COL_INPUTBG);
        if (i == s_slpTimer) {
            tft.drawRect(bx - 1, by - 1, SLP_BTN_W + 2, SLP_BTN_H + 2, g_themeColor);
            tft.setTextColor(g_themeColor, COL_INPUTBG);
        } else {
            tft.drawRect(bx, by, SLP_BTN_W, SLP_BTN_H, COL_DIM);
            tft.setTextColor(COL_DIM, COL_INPUTBG);
        }
        int tw = tft.textWidth(s_slpLabels[i]);
        tft.setCursor(bx + (SLP_BTN_W - tw) / 2, by + (SLP_BTN_H - 8) / 2);
        tft.print(s_slpLabels[i]);
    }

    // ── Schedule row ───────────────────────────────────────────────────────────
    schedLoad();
    {
        int by = SCHED_BTN_Y;
        int schedTw = tft.textWidth("SCHED");

        char sleepBuf[12];
        snprintf(sleepBuf, sizeof(sleepBuf), "SLEEP %s", s_sleepHourLabels[s_sleepHourIdx]);
        int sleepTw = tft.textWidth(sleepBuf);

        char wakeBuf[12];
        snprintf(wakeBuf, sizeof(wakeBuf), "WAKE %s", s_wakeHourLabels[s_wakeHourIdx]);
        int wakeTw = tft.textWidth(wakeBuf);

        int schedW = schedTw + SCHED_PAD * 2;
        int sleepW = SLEEP_W;
        int wakeW  = wakeTw + SCHED_PAD * 2;

        int x1 = SEC2_X;
        int x2 = x1 + schedW + SCHED_GAP;
        int x3 = x2 + sleepW + SCHED_GAP;

        s_schedX1 = x1; s_schedW = schedW;
        s_schedX2 = x2; s_sleepW = sleepW;
        s_schedX3 = x3; s_wakeW  = wakeW;

        // SCHED toggle
        {
            tft.fillRect(x1, by, schedW, SLP_BTN_H, COL_INPUTBG);
            if (s_schedEnabled) {
                tft.drawRect(x1 - 1, by - 1, schedW + 2, SLP_BTN_H + 2, g_themeColor);
                tft.setTextColor(g_themeColor, COL_INPUTBG);
            } else {
                tft.drawRect(x1, by, schedW, SLP_BTN_H, COL_DIM);
                tft.setTextColor(COL_DIM, COL_INPUTBG);
            }
            tft.setCursor(x1 + (schedW - schedTw) / 2, by + (SLP_BTN_H - 8) / 2);
            tft.print("SCHED");
        }
        // Sleep time
        {
            tft.fillRect(x2, by, sleepW, SLP_BTN_H, COL_INPUTBG);
            if (s_schedEnabled) {
                tft.drawRect(x2 - 1, by - 1, sleepW + 2, SLP_BTN_H + 2, g_themeColor);
                tft.setTextColor(g_themeColor, COL_INPUTBG);
            } else {
                tft.drawRect(x2, by, sleepW, SLP_BTN_H, COL_DIM);
                tft.setTextColor(COL_DIM, COL_INPUTBG);
            }
            tft.setCursor(x2 + (sleepW - sleepTw) / 2, by + (SLP_BTN_H - 8) / 2);
            tft.print(sleepBuf);
        }
        // Wake time
        {
            tft.fillRect(x3, by, wakeW, SLP_BTN_H, COL_INPUTBG);
            if (s_schedEnabled) {
                tft.drawRect(x3 - 1, by - 1, wakeW + 2, SLP_BTN_H + 2, g_themeColor);
                tft.setTextColor(g_themeColor, COL_INPUTBG);
            } else {
                tft.drawRect(x3, by, wakeW, SLP_BTN_H, COL_DIM);
                tft.setTextColor(COL_DIM, COL_INPUTBG);
            }
            tft.setCursor(x3 + (wakeW - wakeTw) / 2, by + (SLP_BTN_H - 8) / 2);
            tft.print(wakeBuf);
        }
    }

    // ── Right column: E-Ink + Power Off (top row) ─────────────────────────────
    // E-Ink toggle
    {
        tft.fillRect(EINK_X, EINK_Y, BTN_SQ_W, BTN_SQ_H, COL_INPUTBG);
        if (invertGet()) {
            tft.drawRect(EINK_X - 1, EINK_Y - 1, BTN_SQ_W + 2, BTN_SQ_H + 2, COL_WHITE);
            tft.drawRect(EINK_X - 2, EINK_Y - 2, BTN_SQ_W + 4, BTN_SQ_H + 4, COL_WHITE);
            tft.setTextColor(COL_BG, COL_INPUTBG);
        } else {
            tft.drawRect(EINK_X, EINK_Y, BTN_SQ_W, BTN_SQ_H, g_themeColor);
            tft.setTextColor(g_themeColor, COL_INPUTBG);
        }
        tft.setTextFont(FONT_SM);
        char einkLabel[12];
        snprintf(einkLabel, sizeof(einkLabel), "E-INK %s", invertGet() ? "ON" : "OFF");
        int elw = tft.textWidth(einkLabel);
        tft.setCursor(EINK_X + (BTN_SQ_W - elw) / 2, EINK_Y + (BTN_SQ_H - 8) / 2);
        tft.print(einkLabel);
    }

    // Power Off
    {
        tft.fillRect(PWR_X, PWR_Y, BTN_SQ_W, BTN_SQ_H, COL_INPUTBG);
        tft.drawRect(PWR_X, PWR_Y, BTN_SQ_W, BTN_SQ_H, COL_RED);
        tft.setTextFont(FONT_SM);
        tft.setTextColor(COL_RED, COL_INPUTBG);
        const char *pwrLabel = s_pwrConfirm ? "SURE?" : "PWR OFF";
        int plw = tft.textWidth(pwrLabel);
        tft.setCursor(PWR_X + (BTN_SQ_W - plw) / 2, PWR_Y + (BTN_SQ_H - 8) / 2);
        tft.print(pwrLabel);
    }

    // ── Bottom right: Manage Coins button (full width of both columns) ────────
    {
        tft.fillRect(COINS_X, COINS_Y, COINS_W, BTN_SQ_H, COL_INPUTBG);
        tft.drawRect(COINS_X, COINS_Y, COINS_W, BTN_SQ_H, g_themeColor);
        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, COL_INPUTBG);
        const char *coinLabel = "MANAGE COINS";
        int clw = tft.textWidth(coinLabel);
        tft.setCursor(COINS_X + (COINS_W - clw) / 2, COINS_Y + (BTN_SQ_H - 8) / 2);
        tft.print(coinLabel);
    }

    // Divider 1
    tft.drawFastHLine(0, DIV1_Y, SCREEN_W, COL_DIM);

    // ── Section 2: Brightness ─────────────────────────────────────────────────
    tft.setTextFont(FONT_SM);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(SEC2_X, BRI_LABEL_Y);
    tft.print("BRIGHTNESS");

    int activeBri = brightnessGetLevel();
    for (int i = 0; i < BRI_LEVELS; i++) {
        int bx = SEC2_X + i * (BRI_BTN_W + 3);
        int by = BRI_BTN_Y0;
        tft.fillRect(bx, by, BRI_BTN_W, BRI_BTN_H, COL_INPUTBG);
        if (i == activeBri) {
            tft.drawRect(bx - 1, by - 1, BRI_BTN_W + 2, BRI_BTN_H + 2, g_themeColor);
            tft.setTextColor(g_themeColor, COL_INPUTBG);
        } else {
            tft.drawRect(bx, by, BRI_BTN_W, BRI_BTN_H, COL_DIM);
            tft.setTextColor(COL_DIM, COL_INPUTBG);
        }
        int tw = tft.textWidth(s_briLabels[i]);
        tft.setCursor(bx + (BRI_BTN_W - tw) / 2, by + (BRI_BTN_H - 8) / 2);
        tft.print(s_briLabels[i]);
    }

    // ── Section 2: Rotate ─────────────────────────────────────────────────────
    autoRotLoad();
    tft.setTextFont(FONT_SM);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(SEC2_X, ROT_LABEL_Y);
    tft.print("ROTATE");

    for (int i = 0; i < ROT_BTN_COUNT; i++) {
        int bx = SEC2_X + i * (ROT_BTN_W + 3);
        int by = ROT_BTN_Y0;
        tft.fillRect(bx, by, ROT_BTN_W, ROT_BTN_H, COL_INPUTBG);
        if (i == s_autoRotSel) {
            tft.drawRect(bx - 1, by - 1, ROT_BTN_W + 2, ROT_BTN_H + 2, g_themeColor);
            tft.setTextColor(g_themeColor, COL_INPUTBG);
        } else {
            tft.drawRect(bx, by, ROT_BTN_W, ROT_BTN_H, COL_DIM);
            tft.setTextColor(COL_DIM, COL_INPUTBG);
        }
        int tw = tft.textWidth(s_rotLabels[i]);
        tft.setCursor(bx + (ROT_BTN_W - tw) / 2, by + (ROT_BTN_H - 8) / 2);
        tft.print(s_rotLabels[i]);
    }

    // ── Section 3: Theme Color ───────────────────────────────────────────────
    tft.setTextFont(FONT_SM);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(SEC2_X, THEME_LABEL_Y);
    tft.print("THEME COLOR");

    int activeTheme = themeColorGetIdx();
    for (int i = 0; i < THEME_COUNT; i++) {
        int sx = SEC2_X + i * (SWATCH_W + SWATCH_PAD);
        tft.fillRect(sx, SWATCH_Y0, SWATCH_W, SWATCH_H, g_themes[i].color);
        if (i == activeTheme)
            tft.drawRect(sx - 2, SWATCH_Y0 - 2, SWATCH_W + 4, SWATCH_H + 4, COL_WHITE);
    }
}

// ── Tap handling ──────────────────────────────────────────────────────────────
bool screenSettingsTap(TFT_eSPI &tft, int16_t tx, int16_t ty) {
    (void)tft;

    if (s_pwrConfirm && millis() - s_pwrConfirmMs > 5000) {
        s_pwrConfirm = false;
    }

    // E-Ink toggle
    if (tx >= EINK_X && tx < EINK_X + BTN_SQ_W &&
        ty >= EINK_Y && ty < EINK_Y + BTN_SQ_H) {
        s_pwrConfirm = false;
        invertSet(!invertGet());
        return true;
    }

    // Power Off — two-tap confirmation
    if (tx >= PWR_X && tx < PWR_X + BTN_SQ_W &&
        ty >= PWR_Y && ty < PWR_Y + BTN_SQ_H) {
        if (s_pwrConfirm && millis() - s_pwrConfirmMs < 5000) {
            tft.fillScreen(COL_BG);
            tft.setTextFont(FONT_MD);
            tft.setTextColor(g_themeColor, COL_BG);
            tft.setCursor(80, 110);
            tft.print("Shutting down...");
            delay(500);
            esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0);
            esp_deep_sleep_start();
        } else {
            s_pwrConfirm = true;
            s_pwrConfirmMs = millis();
        }
        return true;
    }

    // Manage Coins
    if (tx >= COINS_X && tx < COINS_X + COINS_W &&
        ty >= COINS_Y && ty < COINS_Y + BTN_SQ_H) {
        s_pwrConfirm = false;
        s_coinPickerRequested = true;
        return true;
    }

    // Sleep timer buttons
    slpCacheLoad();
    for (int i = 0; i < SLP_COUNT; i++) {
        int bx = SEC2_X + i * (SLP_BTN_W + SLP_GAP);
        if (tx >= bx && tx < bx + SLP_BTN_W &&
            ty >= SLP_BTN_Y0 && ty < SLP_BTN_Y0 + SLP_BTN_H) {
            s_slpTimer = i;
            slpCacheSave();
            return true;
        }
    }

    // Schedule row buttons
    schedLoad();
    {
        int by = SCHED_BTN_Y;
        if (tx >= s_schedX1 && tx < s_schedX1 + s_schedW &&
            ty >= by && ty < by + SLP_BTN_H) {
            s_schedEnabled = !s_schedEnabled;
            schedSave();
            return true;
        }
        if (tx >= s_schedX2 && tx < s_schedX2 + s_sleepW &&
            ty >= by && ty < by + SLP_BTN_H) {
            s_sleepHourIdx = (s_sleepHourIdx + 1) % SCHED_COUNT;
            schedSave();
            return true;
        }
        if (tx >= s_schedX3 && tx < s_schedX3 + s_wakeW &&
            ty >= by && ty < by + SLP_BTN_H) {
            s_wakeHourIdx = (s_wakeHourIdx + 1) % WAKE_COUNT;
            schedSave();
            return true;
        }
    }

    // Brightness
    for (int i = 0; i < BRI_LEVELS; i++) {
        int bx = SEC2_X + i * (BRI_BTN_W + 3);
        if (tx >= bx && tx < bx + BRI_BTN_W &&
            ty >= BRI_BTN_Y0 && ty < BRI_BTN_Y0 + BRI_BTN_H) {
            brightnessSetLevel(i);
            return true;
        }
    }

    // Auto-rotate
    autoRotLoad();
    for (int i = 0; i < ROT_BTN_COUNT; i++) {
        int bx = SEC2_X + i * (ROT_BTN_W + 3);
        if (tx >= bx && tx < bx + ROT_BTN_W &&
            ty >= ROT_BTN_Y0 && ty < ROT_BTN_Y0 + ROT_BTN_H) {
            s_autoRotSel = i;
            nvsPutInt("arot_sel", i);
            return true;
        }
    }

    // Theme swatches
    for (int i = 0; i < THEME_COUNT; i++) {
        int sx = SEC2_X + i * (SWATCH_W + SWATCH_PAD);
        if (tx >= sx && tx < sx + SWATCH_W &&
            ty >= SWATCH_Y0 && ty < SWATCH_Y0 + SWATCH_H) {
            themeColorSet(i);
            return true;
        }
    }

    return false;
}

bool screenSettingsCoinPickerTapped() {
    if (s_coinPickerRequested) {
        s_coinPickerRequested = false;
        return true;
    }
    return false;
}

int screenSettingsGetSleepTimerSecs() {
    int idx = nvsGetInt("slp_timer", 0);
    if (idx < 0 || idx >= SLP_COUNT) return 0;
    return (int)s_slpSecs[idx];
}

bool screenSettingsGetScheduleEnabled() {
    schedLoad();
    return s_schedEnabled;
}

int screenSettingsGetSleepHour() {
    schedLoad();
    return s_sleepHours[s_sleepHourIdx];
}

int screenSettingsGetWakeHour() {
    schedLoad();
    return s_wakeHours[s_wakeHourIdx];
}

bool screenSettingsGetAutoRotate() {
    autoRotLoad();
    return s_autoRotSel > 0;
}

uint32_t screenSettingsGetAutoRotateMs() {
    autoRotLoad();
    return s_rotMs[s_autoRotSel];
}
