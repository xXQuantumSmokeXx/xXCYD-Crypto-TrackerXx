#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include <math.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "config/config.h"
#include "config/nvs_config.h"
#include "modules/brightness.h"
#include "modules/wifi_config.h"
#include "ui/theme.h"
#include "ui/theme_color.h"
#include "ui/widgets.h"
#include "touch/touch.h"
#include "modules/time_sync.h"
#include "modules/crypto.h"
#include "ui/screens/screen_crypto.h"
#include "ui/screens/screen_detail.h"
#include "ui/screens/screen_settings.h"
#include "ui/screens/screen_coinpicker.h"

static TFT_eSPI tft;

// ── App state ─────────────────────────────────────────────────────────────
enum AppScreen {
    SCR_CRYPTO = 0,     // 0-4 = crypto pages 1-5
    SCR_SETTINGS = 5,   // settings
    SCR_COINPICKER = 6, // coin picker (from settings)
    SCR_DETAIL = 7,     // coin detail overlay
    SCR_COUNT = 6       // normal screens (0-5)
};

static int           s_screen         = 0;
static int           s_cryptoPage     = 0;   // which crypto page (0-4) we're viewing
static bool          s_needsRedraw    = true;
static bool          s_wifiOk         = false;
static char          s_detailCoinId[32] = "";  // coin ID for detail view
static unsigned long s_lastMinute     = 0;
static unsigned long s_lastAutoRotate = 0;
static unsigned long s_lastTouchMs    = 0;
static bool          s_backlightOff   = false;
static bool          s_scheduleSleeping = false;
static unsigned long s_schedGraceUntil = 0;

// ── RGB LED ───────────────────────────────────────────────────────────────
static void ledSet(bool r, bool g, bool b) {
    digitalWrite(LED_R, r ? LOW : HIGH);
    digitalWrite(LED_G, g ? LOW : HIGH);
    digitalWrite(LED_B, b ? LOW : HIGH);
}

// ── Async fetch worker (Core 0) ────────────────────────────────────────────
enum FetchCmd : uint8_t {
    FETCH_NONE = 0,
    FETCH_CRYPTO,
};

static TaskHandle_t      s_fetchTask    = nullptr;
static SemaphoreHandle_t s_dataMutex    = nullptr;
static volatile FetchCmd s_fetchCmd     = FETCH_NONE;
static volatile bool     s_fetchDone    = false;

static void fetchWorker(void *param) {
    (void)param;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        FetchCmd cmd = s_fetchCmd;

        switch (cmd) {
            case FETCH_CRYPTO: {
                cryptoFetch(false);
                xSemaphoreTake(s_dataMutex, portMAX_DELAY);
                s_fetchDone = true;
                xSemaphoreGive(s_dataMutex);
                break;
            }
            default:
                break;
        }

        s_fetchCmd = FETCH_NONE;
        ledSet(false, false, false);
    }
}

static bool workerBusy() { return s_fetchCmd != FETCH_NONE; }

static void triggerFetch() {
    if (!s_fetchTask || workerBusy()) return;
    s_fetchDone = false;
    s_fetchCmd  = FETCH_CRYPTO;
    ledSet(false, false, true);
    xTaskNotifyGive(s_fetchTask);
}

// ── Splash art — abstract faceted crypto core ───────────────────────────
static void drawSplashArt(TFT_eSPI &tft) {
    const int cx = 160, cy = 50;

    // ── Outer orbital rings ─────────────────────────────────────────────
    tft.drawCircle(cx, cy, 48, COL_DIM);
    tft.drawCircle(cx, cy, 44, COL_DIM);

    // ── 8 orbital nodes ─────────────────────────────────────────────────
    for (int i = 0; i < 8; i++) {
        float a = i * M_PI / 4.0f - M_PI / 2.0f;
        int nx = cx + (int)(46.0f * cosf(a));
        int ny = cy + (int)(46.0f * sinf(a));
        if (ny > 106) continue;
        bool major = (i % 2 == 0);
        tft.fillCircle(nx, ny, major ? 3 : 1,
                       major ? g_themeColor : COL_DIM);
    }

    // ── Connection spokes from center to nodes ──────────────────────────
    for (int i = 0; i < 8; i++) {
        float a = i * M_PI / 4.0f - M_PI / 2.0f;
        int nx = cx + (int)(46.0f * cosf(a));
        int ny = cy + (int)(46.0f * sinf(a));
        if (ny > 106) continue;
        tft.drawLine(cx, cy, nx, ny, COL_DIM);
    }

    // ── Outer octagon (facets of the gem) ───────────────────────────────
    const int OR = 34;  // octagon radius
    int ox[8], oy[8];
    for (int i = 0; i < 8; i++) {
        float a = i * M_PI / 4.0f - M_PI / 2.0f + M_PI / 8.0f;  // offset 22.5°
        ox[i] = cx + (int)(OR * cosf(a));
        oy[i] = cy + (int)(OR * sinf(a));
    }
    // Draw filled octagon
    for (int i = 0; i < 8; i++)
        tft.drawLine(ox[i], oy[i], ox[(i+1)&7], oy[(i+1)&7], g_themeColor);
    // Cross-facet lines (connect opposite vertices through center)
    for (int i = 0; i < 4; i++)
        tft.drawLine(ox[i], oy[i], ox[i+4], oy[i+4], COL_DIM);

    // ── Inner diamond (rotated square) ──────────────────────────────────
    const int IR = 20;
    int ix[4], iy[4];
    for (int i = 0; i < 4; i++) {
        float a = i * M_PI / 2.0f - M_PI / 2.0f;
        ix[i] = cx + (int)(IR * cosf(a));
        iy[i] = cy + (int)(IR * sinf(a));
    }
    // Filled diamond (themed, on dark bg it pops)
    for (int i = 0; i < 4; i++)
        tft.drawLine(ix[i], iy[i], ix[(i+1)&3], iy[(i+1)&3], g_themeColor);
    // Inner cross
    tft.drawLine(ix[0], iy[0], ix[2], iy[2], COL_DIM);
    tft.drawLine(ix[1], iy[1], ix[3], iy[3], COL_DIM);

    // ── Core node (bright center) ───────────────────────────────────────
    tft.fillCircle(cx, cy, 4, g_themeColor);
    tft.drawCircle(cx, cy, 6, g_themeColor);
    tft.drawCircle(cx, cy, 8, COL_DIM);

    // ── Mid-ring accent dots at octagon vertices ────────────────────────
    for (int i = 0; i < 8; i++)
        tft.fillCircle(ox[i], oy[i], 2, g_themeColor);

    // ── Accent lines flanking the core ──────────────────────────────────
    int lineY = cy + 46;
    int lineW = 30;
    tft.drawFastHLine(cx - 56, lineY, lineW, g_themeColor);
    tft.drawFastHLine(cx + 26, lineY, lineW, g_themeColor);
    tft.fillCircle(cx - 56,          lineY, 2, g_themeColor);
    tft.fillCircle(cx - 56 + lineW,  lineY, 2, g_themeColor);
    tft.fillCircle(cx + 26,          lineY, 2, g_themeColor);
    tft.fillCircle(cx + 26 + lineW,  lineY, 2, g_themeColor);

    // ── Price-candle motif ──────────────────────────────────────────────
    int candleY = lineY + 7;
    for (int i = 0; i < 5; i++) {
        int kx = cx - 36 + i * 18;
        int bodyH = 6 + (i % 3) * 4;
        int bodyTop = candleY + (14 - bodyH) / 2;
        tft.drawFastVLine(kx + 3, candleY, 14, COL_DIM);
        tft.fillRect(kx, bodyTop, 6, bodyH, g_themeColor);
    }
}

static void showSplash(const char *msg) {
    tft.fillScreen(COL_BG);
    drawSplashArt(tft);

    int tw;

    tft.setTextFont(FONT_LG);
    tft.setTextColor(g_themeColor, COL_BG);
    tw = tft.textWidth("xXMayDayXx");
    tft.setCursor((SCREEN_W - tw) / 2, 118);
    tft.print("xXMayDayXx");

    tft.setTextFont(FONT_MD);
    tft.setTextColor(COL_WHITE, COL_BG);
    tw = tft.textWidth("xXCYD-Crypto-TrackerXx");
    tft.setCursor((SCREEN_W - tw) / 2, 152);
    tft.print("xXCYD-Crypto-TrackerXx");

    tft.setTextColor(g_themeColor, COL_BG);
    tw = tft.textWidth("xXQuantum-SmokeXx");
    tft.setCursor((SCREEN_W - tw) / 2, 177);
    tft.print("xXQuantum-SmokeXx");

    if (msg && msg[0]) {
        tft.setTextFont(FONT_SM);
        tft.setTextColor(COL_DIM, COL_BG);
        tw = tft.textWidth(msg);
        tft.setCursor((SCREEN_W - tw) / 2, 210);
        tft.print(msg);
    }
}

// ── WiFi ──────────────────────────────────────────────────────────────────
static void connectWifi() {
    showSplash("Connecting to WiFi...");
    char ssid[64], pass[64];
    wifiGetSSID(ssid, sizeof(ssid));
    wifiGetPass(pass, sizeof(pass));
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) delay(100);
    s_wifiOk = (WiFi.status() == WL_CONNECTED);
    if (!s_wifiOk) showSplash("WiFi failed!");
}

// ── Screen navigation ─────────────────────────────────────────────────────
static void gotoScreen(int n) {
    s_screen = constrain(n, 0, SCR_COUNT - 1);
    // If landing on a disabled crypto page, redirect to next enabled
    if (s_screen < MAX_CRYPTO_PAGES && !cryptoPageIsEnabled(s_screen)) {
        int next = cryptoGetNextEnabledPage(s_screen);
        if (next != s_screen) s_screen = next;
    }
    s_lastAutoRotate = millis();
    s_needsRedraw    = true;
}

// Navigate forward (swipe left / right arrow) — skips disabled pages
static void navNext() {
    if (s_screen == SCR_SETTINGS) {
        // From settings → first enabled crypto page
        s_screen = cryptoGetNextEnabledPage(-1);
    } else if (s_screen < MAX_CRYPTO_PAGES) {
        int next = cryptoGetNextEnabledPage(s_screen);
        // If next wraps around (<= current), we're at the last enabled page → settings
        if (next <= s_screen)
            s_screen = SCR_SETTINGS;
        else
            s_screen = next;
    }
    s_lastAutoRotate = millis();
}

// Navigate backward (swipe right / left arrow)
static void navPrev() {
    if (s_screen == SCR_SETTINGS) {
        // From settings → last enabled crypto page
        s_screen = cryptoGetPrevEnabledPage(MAX_CRYPTO_PAGES);
    } else if (s_screen < MAX_CRYPTO_PAGES) {
        int prev = cryptoGetPrevEnabledPage(s_screen);
        // If prev wraps around (>= current), we're at the first enabled page → settings
        if (prev >= s_screen)
            s_screen = SCR_SETTINGS;
        else
            s_screen = prev;
    }
    s_lastAutoRotate = millis();
}

static void gotoDetail(const char *coinId) {
    strlcpy(s_detailCoinId, coinId, sizeof(s_detailCoinId));
    s_screen      = SCR_DETAIL;
    s_needsRedraw = true;
}

static void gotoCoinPicker() {
    s_screen      = SCR_COINPICKER;
    s_needsRedraw = true;
}

// Render current screen to any TFT_eSPI target (used by screenshot capture)
static void redrawTo(TFT_eSPI &display) {
    switch (s_screen) {
        case SCR_CRYPTO:
        case SCR_CRYPTO + 1:
        case SCR_CRYPTO + 2:
        case SCR_CRYPTO + 3:
        case SCR_CRYPTO + 4:
            s_cryptoPage = s_screen;
            screenCryptoDraw(display, s_wifiOk, s_cryptoPage);
            break;
        case SCR_SETTINGS:
            screenSettingsDraw(display, s_wifiOk);
            break;
        case SCR_COINPICKER:
            screenCoinPickerDraw(display, s_wifiOk);
            break;
        case SCR_DETAIL:
            screenDetailDraw(display, s_wifiOk, s_detailCoinId);
            break;
    }
}

static void redraw() {
    redrawTo(tft);
    s_needsRedraw = false;
}

// ── First-boot calibration (2USB only) ────────────────────────────────────

static int  s_rotation = 1;
static uint8_t s_madctl = 0x80;

static void applyRotation() {
#if CYD_USB_VERSION == 2
    tft.setRotation(1);
    tft.writecommand(TFT_MADCTL);
    tft.writedata(s_madctl);
#else
    tft.setRotation(s_rotation);
#endif
}

static uint8_t madctlForCombo(int idx) {
    switch (idx & 3) {
        case 0:  return TFT_MAD_MV | TFT_MAD_BGR;           // 0x28
        case 1:  return TFT_MAD_MV | TFT_MAD_MY | TFT_MAD_BGR; // 0xA8
        case 2:  return 0x00;
        default: return TFT_MAD_MY;                          // 0x80
    }
}

#define CURRENT_CAL_VER  2

static void displayCalibrate() {
#if CYD_USB_VERSION == 2
    if (nvsGetInt("cal_ver", -1) >= CURRENT_CAL_VER) {
        s_madctl = (uint8_t)nvsGetInt("madctl", 0x80);
        return;
    }

    s_madctl = madctlForCombo(0);
    digitalWrite(TFT_BL, HIGH);

    auto drawDisplayCal = [&]() {
        tft.fillScreen(COL_BG);
        applyRotation();
        tft.fillScreen(COL_BG);

        tft.fillTriangle(2, 2, 60, 2, 2, 60, COL_AMBER);
        tft.fillTriangle(4, 4, 56, 4, 4, 56, COL_BG);

        tft.fillRect(SCREEN_W - 50, 2, 48, 8, g_themeColor);
        tft.fillRect(SCREEN_W - 8, 2, 6, 48, g_themeColor);

        tft.fillCircle(24, SCREEN_H - 24, 20, COL_AMBER);
        tft.fillCircle(24, SCREEN_H - 24, 16, COL_BG);
        tft.fillCircle(24, SCREEN_H - 24, 20, COL_AMBER);

        tft.drawLine(SCREEN_W - 40, SCREEN_H - 24, SCREEN_W - 8, SCREEN_H - 24, g_themeColor);
        tft.drawLine(SCREEN_W - 24, SCREEN_H - 40, SCREEN_W - 24, SCREEN_H - 8, g_themeColor);
        tft.drawCircle(SCREEN_W - 24, SCREEN_H - 24, 14, g_themeColor);

        tft.fillRect(SCREEN_W / 2 - 16, SCREEN_H / 2 - 24, 32, 6, COL_WHITE);
        tft.fillRect(SCREEN_W / 2 - 4, SCREEN_H / 2 - 24, 8, 48, COL_WHITE);

        int idx;
        if      (s_madctl == (TFT_MAD_MV | TFT_MAD_BGR))             idx = 0;
        else if (s_madctl == (TFT_MAD_MV | TFT_MAD_MY | TFT_MAD_BGR)) idx = 1;
        else if (s_madctl == 0x00)                                    idx = 2;
        else                                                          idx = 3;

        tft.setTextFont(FONT_LG);
        tft.setTextColor(g_themeColor, COL_BG);
        char buf[16]; snprintf(buf, sizeof(buf), "MODE %d", idx);
        int tw = tft.textWidth(buf);
        tft.setCursor((SCREEN_W - tw) / 2, 68);
        tft.print(buf);

        tft.setTextFont(FONT_MD);
        tft.setTextColor(COL_WHITE, COL_BG);
        const char *msg = "Tap to change";
        tw = tft.textWidth(msg);
        tft.setCursor((SCREEN_W - tw) / 2, SCREEN_H - 72);
        tft.print(msg);

        tft.setTextFont(FONT_SM);
        tft.setTextColor(COL_DIM, COL_BG);
        msg = "Hold 2s to confirm";
        tw = tft.textWidth(msg);
        tft.setCursor((SCREEN_W - tw) / 2, SCREEN_H - 52);
        tft.print(msg);
    };

    drawDisplayCal();

    {
        unsigned long holdStart = 0;
        bool wasTouched = false;
        int  curCombo = 0;

        while (true) {
            bool nowTouched = touchIsHeld();

            if (nowTouched && !wasTouched) {
                holdStart = millis();
            } else if (!nowTouched && wasTouched && holdStart > 0) {
                if (millis() - holdStart < 1200) {
                    curCombo = (curCombo + 1) & 3;
                    s_madctl = madctlForCombo(curCombo);
                    drawDisplayCal();
                }
            }

            if (nowTouched && wasTouched && holdStart > 0) {
                if (millis() - holdStart >= 2000) break;
            }

            wasTouched = nowTouched;
            delay(30);
        }

        while (touchIsHeld()) { delay(30); }
        delay(200);
    }

    nvsPutInt("madctl", s_madctl);
#endif
}

static void touchCalibrate() {
#if CYD_USB_VERSION == 2
    if (nvsGetInt("cal_ver", -1) >= CURRENT_CAL_VER) return;

    digitalWrite(TFT_BL, HIGH);

    auto drawStatic = []() {
        tft.fillScreen(COL_BG);

        tft.setTextFont(FONT_LG);
        tft.setTextColor(g_themeColor, COL_BG);
        char buf[4]; snprintf(buf, sizeof(buf), "%d", touchGetRotation());
        int tw = tft.textWidth(buf);
        tft.setCursor((SCREEN_W - tw) / 2, SCREEN_H / 2 - 40);
        tft.print(buf);

        tft.setTextFont(FONT_MD);
        tft.setTextColor(COL_WHITE, COL_BG);
        const char *msg = "Tap to cycle touch";
        tw = tft.textWidth(msg);
        tft.setCursor((SCREEN_W - tw) / 2, SCREEN_H / 2);
        tft.print(msg);

        tft.setTextFont(FONT_SM);
        tft.setTextColor(COL_DIM, COL_BG);
        msg = "Hold 2s to confirm";
        tw = tft.textWidth(msg);
        tft.setCursor((SCREEN_W - tw) / 2, SCREEN_H / 2 + 30);
        tft.print(msg);

        const int CX = 14, CY = 14, CS = 18;
        uint16_t tc = COL_DIM;
        tft.drawRect(CX, CY, CS, CS, tc);
        tft.drawLine(CX, CY, CX + CS, CY + CS, tc);
        tft.drawLine(CX, CY + CS, CX + CS, CY, tc);

        tft.drawRect(SCREEN_W - CX - CS, CY, CS, CS, tc);
        tft.drawLine(SCREEN_W - CX - CS, CY, SCREEN_W - CX, CY + CS, tc);
        tft.drawLine(SCREEN_W - CX - CS, CY + CS, SCREEN_W - CX, CY, tc);

        tft.drawRect(CX, SCREEN_H - CY - CS, CS, CS, tc);
        tft.drawLine(CX, SCREEN_H - CY, CX + CS, SCREEN_H - CY - CS, tc);
        tft.drawLine(CX, SCREEN_H - CY - CS, CX + CS, SCREEN_H - CY, tc);

        tft.drawRect(SCREEN_W - CX - CS, SCREEN_H - CY - CS, CS, CS, tc);
        tft.drawLine(SCREEN_W - CX, SCREEN_H - CY, SCREEN_W - CX - CS, SCREEN_H - CY - CS, tc);
        tft.drawLine(SCREEN_W - CX - CS, SCREEN_H - CY, SCREEN_W - CX, SCREEN_H - CY - CS, tc);
    };

    drawStatic();

    unsigned long holdStart = 0;
    bool wasTouched = false;
    int  curX = -1, curY = -1;
    int  lastX = -1, lastY = -1;
    bool dirty = false;

    while (true) {
        int16_t tx, ty;
        bool nowTouched = touchIsHeld(&tx, &ty);

        if (nowTouched) {
            curX = tx; curY = ty;
        }

        if (nowTouched && !wasTouched) {
            holdStart = millis();
            lastX = curX; lastY = curY;
            dirty = true;
        } else if (!nowTouched && wasTouched && holdStart > 0) {
            if (millis() - holdStart < 1200) {
                touchSetRotation((touchGetRotation() + 1) % 4);
                drawStatic();
            }
            tft.fillCircle(lastX, lastY, 7, COL_BG);
            lastX = lastY = -1;
            dirty = false;
        } else if (nowTouched && wasTouched && holdStart > 0) {
            if (millis() - holdStart >= 2000) {
                if (lastX >= 0) tft.fillCircle(lastX, lastY, 7, COL_BG);
                break;
            }
        }

        if (nowTouched && dirty && (curX != lastX || curY != lastY)) {
            if (lastX >= 0) tft.fillCircle(lastX, lastY, 7, COL_BG);
            tft.fillCircle(curX, curY, 6, COL_AMBER);
            tft.drawCircle(curX, curY, 6, COL_WHITE);
            lastX = curX; lastY = curY;
        }

        wasTouched = nowTouched;
        delay(30);
    }

    while (touchIsHeld()) { delay(30); }
    delay(200);

    nvsPutInt("cal_ver", CURRENT_CAL_VER);
    nvsPutInt("touch_cal", 1);
#endif
}

// ─────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
    ledSet(false, false, false);

    nvsInit();

    // ── Migrate calibration keys from CYD-Weather's "cydwx" namespace ─────
    // Avoids re-running display/touch calibration on 2USB boards.
    {
        Preferences cydwx;
        if (cydwx.begin("cydwx", true)) {  // read-only
            if (nvsGetInt("cal_ver", -1) < 0) {
                int calVer = cydwx.getInt("cal_ver", -1);
                if (calVer >= 0) {
                    nvsPutInt("cal_ver", calVer);
                    nvsPutInt("madctl",  cydwx.getInt("madctl", 0x80));
                    nvsPutInt("touch_rot", cydwx.getInt("touch_rot", 0));
                    nvsPutInt("touch_cal", cydwx.getInt("touch_cal", 0));
                    Serial.println("Calibration keys migrated from cydwx");
                }
            }
            cydwx.end();
        }
    }

    wifiConfigLoad();   // reads wifi.txt from SD if present, saves to NVS
    themeColorInit();
    cryptoInit();       // loads page assignments from NVS

    tft.init();
    applyRotation();
    tft.fillScreen(COL_BG);
    brightnessInit();

    touchInit();

    // First-boot calibrations — only on 2USB, only once each
    displayCalibrate();
    applyRotation();
    touchCalibrate();

    showSplash("Starting up...");
    ledSet(false, true, false);

    connectWifi();
    s_wifiOk = (WiFi.status() == WL_CONNECTED);

    s_dataMutex = xSemaphoreCreateMutex();

    if (s_wifiOk) {
        showSplash("Syncing time...");
        char tz[32];
        nvsGetStr("tz_string", tz, sizeof(tz), "EST5EDT");
        timeSyncInitTZ(tz);   // NVS-backed timezone, default US Eastern
        for (int i = 0; i < 30 && !timeIsValid(); i++) delay(100);

        // Retry fetch until we have live data (CoinGecko can be slow / rate-limited)
        bool gotData = false;
        for (int attempt = 0; attempt < 4; attempt++) {
            char msg[32];
            snprintf(msg, sizeof(msg), "Fetching crypto %d/4...", attempt + 1);
            showSplash(msg);
            if (cryptoFetch(true)) { gotData = true; break; }
            if (attempt < 3) delay(2000);
        }
        if (!gotData) {
            // Last resort: try SD cache
            showSplash("Trying SD cache...");
            cryptoFetch(false);
        }
    } else {
        // Try loading from cache
        cryptoFetch(false);
    }

    // Launch async fetch worker on Core 0
    xTaskCreatePinnedToCore(fetchWorker, "fetch", 20480, nullptr, 1, &s_fetchTask, 0);

    ledSet(false, false, false);
    s_lastTouchMs = millis();
    s_needsRedraw = true;
    Serial.println("READY");
}

// ─────────────────────────────────────────────────────────────────────────
void loop() {
    // Brightness update
    static unsigned long lastLdr = 0;
    if (millis() - lastLdr > 5000) {
        if (!s_backlightOff) brightnessAutoUpdate();
        lastLdr = millis();
    }

    // Periodic crypto refresh — every 60 seconds if WiFi is up
    static unsigned long lastCryptoFetch = 0;
    if (s_wifiOk && !workerBusy() && millis() - lastCryptoFetch > 60000) {
        lastCryptoFetch = millis();
        triggerFetch();
    }

    // Fetch completion
    if (s_fetchDone) {
        xSemaphoreTake(s_dataMutex, portMAX_DELAY);
        s_fetchDone = false;
        xSemaphoreGive(s_dataMutex);
        if (s_screen < SCR_COUNT) s_needsRedraw = true;
    }

    // Clock tick — update time in topbar without full redraw
    if (millis() - s_lastMinute > 60000) {
        if (s_screen < SCR_COUNT) {
            char timeStr[10]; timeGetShort(timeStr);
            static const char *labels[] = {"PAGE 1","PAGE 2","PAGE 3","PAGE 4","PAGE 5","SETTINGS"};
            drawTopbarTime(tft, timeStr, labels[s_screen]);
        }
        if (s_screen == SCR_DETAIL) {
            char timeStr[10]; timeGetShort(timeStr);
            const CoinData *c = cryptoGetCoinData(s_detailCoinId);
            char title[40];
            snprintf(title, sizeof(title), "%s", c ? c->symbol : "DETAIL");
            drawTopbarTime(tft, timeStr, title);
        }
        s_lastMinute = millis();
    }

    // Auto-rotate: crypto pages only
    if (screenSettingsGetAutoRotate() &&
        s_screen < MAX_CRYPTO_PAGES &&
        millis() - s_lastAutoRotate > screenSettingsGetAutoRotateMs()) {
        int next = cryptoGetNextEnabledPage(s_screen);
        if (next != s_screen) {
            s_screen = next;
            s_lastAutoRotate = millis();
            s_needsRedraw = true;
        }
    }

    // ── Touch ──────────────────────────────────────────────────────────────
    TouchEvent evt = touchPoll();

    if (s_screen < SCR_COUNT) {
        // Normal screen navigation — skip disabled pages
        if (evt.swipe == SwipeDir::Left) {
            navNext();
            s_needsRedraw = true;
        } else if (evt.swipe == SwipeDir::Right) {
            navPrev();
            s_needsRedraw = true;
        } else if (evt.tap == TapEvent::Tap) {
            int tx = evt.tapX, ty = evt.tapY;

            // Bottom bar: arrow taps for navigation
            int botY = SCREEN_H - BOTBAR_H;
            if (ty >= botY) {
                if (tx < 50) {
                    navPrev();
                    s_needsRedraw = true;
                } else if (tx > SCREEN_W - 50) {
                    navNext();
                    s_needsRedraw = true;
                }
            }

            // Screen-specific taps
            if (s_screen == SCR_SETTINGS) {
                bool changed = screenSettingsTap(tft, tx, ty);
                if (changed) s_needsRedraw = true;
                if (screenSettingsCoinPickerTapped()) {
                    gotoCoinPicker();
                }
            } else if (s_screen < MAX_CRYPTO_PAGES) {
                // Crypto page: tap to switch graph mode (1-coin) or nothing (multi)
                int coinCount = cryptoPageCountActive(s_screen);
                screenCryptoTap(tft, tx, ty, s_screen, s_wifiOk);
                if (coinCount == 1) s_needsRedraw = true;  // graph mode may have changed
                const char *coinId = screenCryptoGetTappedCoin();
                if (coinId) {
                    gotoDetail(coinId);
                }
            }
        }
    } else if (s_screen == SCR_COINPICKER) {
        // Coin picker screen
        if (evt.swipe == SwipeDir::Up) {
            screenCoinPickerSwipe(1);
            s_needsRedraw = true;
        } else if (evt.swipe == SwipeDir::Down) {
            screenCoinPickerSwipe(-1);
            s_needsRedraw = true;
        } else if (evt.tap == TapEvent::Tap) {
            screenCoinPickerTap(tft, evt.tapX, evt.tapY, s_wifiOk);
            if (screenCoinPickerShouldExit()) {
                s_screen = SCR_SETTINGS;
                s_needsRedraw = true;
            } else {
                s_needsRedraw = true;
            }
        }
    } else if (s_screen == SCR_DETAIL) {
        // Detail screen
        if (evt.tap == TapEvent::Tap) {
            screenDetailTap(tft, evt.tapX, evt.tapY, s_wifiOk);
            if (screenDetailShouldExit()) {
                s_screen = s_cryptoPage;   // back to the crypto page we came from
                s_needsRedraw = true;
            } else {
                s_needsRedraw = true;      // graph tab changed
            }
        }
    }

    // ── Sleep timer ──────────────────────────────────────────────────────────
    if (evt.swipe != SwipeDir::None || evt.tap != TapEvent::None) {
        s_lastTouchMs = millis();
        if (s_backlightOff) {
            s_backlightOff = false;
            brightnessRestore();
            if (s_scheduleSleeping) {
                s_schedGraceUntil = millis() + 30000;
            }
        }
    }

    int slpSecs = screenSettingsGetSleepTimerSecs();
    if (slpSecs > 0 && !g_invert && !s_backlightOff) {
        if (millis() - s_lastTouchMs >= (unsigned long)slpSecs * 1000UL) {
            s_backlightOff = true;
            brightnessOff();
        }
    }

    // ── Schedule-based sleep ──────────────────────────────────────────────────
    {
        bool schedEnabled = screenSettingsGetScheduleEnabled();
        if (!schedEnabled && s_scheduleSleeping) {
            s_scheduleSleeping = false;
            if (s_backlightOff) {
                s_backlightOff = false;
                brightnessRestore();
                s_needsRedraw = true;
            }
        }

        if (schedEnabled && !g_invert && timeIsValid()) {
            time_t now = time(nullptr);
            int curHour = localtime(&now)->tm_hour;
            int sleepHr  = screenSettingsGetSleepHour();
            int wakeHr   = screenSettingsGetWakeHour();

            bool inWindow;
            if (sleepHr < wakeHr) {
                inWindow = (curHour >= sleepHr && curHour < wakeHr);
            } else {
                inWindow = (curHour >= sleepHr || curHour < wakeHr);
            }

            if (inWindow && !s_backlightOff && millis() > s_schedGraceUntil) {
                s_scheduleSleeping = true;
                s_backlightOff = true;
                brightnessOff();
            } else if (!inWindow && s_scheduleSleeping) {
                s_scheduleSleeping = false;
                if (s_backlightOff) {
                    s_backlightOff = false;
                    brightnessRestore();
                    s_needsRedraw = true;
                }
            }
        }
    }

    // ── Serial commands ──────────────────────────────────────────────────────
    if (Serial.available()) {
        int cmd = Serial.read();
        if (cmd == 'R' || cmd == 'r') {
            Serial.println("READY");
        }
        if (cmd == 'M' || cmd == 'm') {
            int cur = 3;
            if      (s_madctl == (TFT_MAD_MV | TFT_MAD_BGR))                cur = 0;
            else if (s_madctl == (TFT_MAD_MV | TFT_MAD_MY | TFT_MAD_BGR))  cur = 1;
            else if (s_madctl == 0x00)                                       cur = 2;
            cur = (cur + 1) & 3;
            s_madctl = madctlForCombo(cur);
            nvsPutInt("madctl", s_madctl);
            nvsPutInt("cal_ver", CURRENT_CAL_VER);
            applyRotation();
            Serial.print("MADCTL_MODE:");
            Serial.println(cur);
            s_needsRedraw = true;
        }
        if (cmd == 'T' || cmd == 't') {
            int rot = (touchGetRotation() + 1) % 4;
            touchSetRotation(rot);
            nvsPutInt("touch_cal", 1);
            Serial.print("TOUCH_ROT:");
            Serial.println(rot);
            s_needsRedraw = true;
        }
        if (cmd >= '0' && cmd <= '5') {
            int n = cmd - '0';
            if (s_backlightOff) { s_backlightOff = false; brightnessRestore(); }
            gotoScreen(n);
            redraw();
        }
        if (cmd == 'F' || cmd == 'f') {
            // Force refresh
            if (s_wifiOk && !workerBusy()) {
                nvsPutInt("btc_cached_at", 0);
                triggerFetch();
            }
        }
        if (cmd == 'S' || cmd == 's') {
            // Screenshot capture → RGB332 framebuffer over serial
            bool blWasOff = s_backlightOff;
            if (blWasOff) { s_backlightOff = false; brightnessRestore(); }

            TFT_eSprite spr(&tft);
            spr.setColorDepth(8);
            uint8_t *fb = (uint8_t*)spr.createSprite(SCREEN_W, SCREEN_H);
            if (fb) {
                redrawTo(spr);
                Serial.print("RGB332:");
                Serial.write(fb, SCREEN_W * SCREEN_H);
                Serial.flush();
                spr.deleteSprite();
            } else {
                // Sprite alloc failed — read back from TFT line-by-line
                redraw();
                uint16_t lineBuf[SCREEN_W];
                Serial.print("RGB332:");
                for (int y = 0; y < SCREEN_H; y++) {
                    tft.readRect(0, y, SCREEN_W, 1, lineBuf);
                    for (int x = 0; x < SCREEN_W; x++) {
                        uint16_t c = lineBuf[x];
                        uint8_t b = ((c >> 13) & 0x07) << 5
                                  | ((c >>  8) & 0x07) << 2
                                  | ((c >>  3) & 0x03);
                        Serial.write(b);
                    }
                }
                Serial.flush();
            }

            if (blWasOff) { s_backlightOff = true; brightnessOff(); }
        }
    }

    // Rate-limit redraws to prevent flicker (max ~5/sec)
    static unsigned long s_lastRedraw = 0;
    if (s_needsRedraw && millis() - s_lastRedraw > 200) {
        redraw();
        s_lastRedraw = millis();
    }
    delay(20);
}
