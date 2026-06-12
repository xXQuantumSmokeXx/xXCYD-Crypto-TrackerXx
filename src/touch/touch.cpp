#include "touch.h"
#include "../config/config.h"
#include "../config/nvs_config.h"
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

static SPIClass            g_touchSPI(VSPI);
static XPT2046_Touchscreen g_ts(TOUCH_CS);

static int s_touchRotation = 2;   // 0-3, NVS-backed; default rotates 2 (180°)

#define SWIPE_THRESHOLD 30
#define SWIPE_RATIO      2
#define DEBOUNCE_MS     50

static bool     s_held    = false;
static int16_t  s_startX  = 0, s_startY = 0;
static int16_t  s_lastX   = 0, s_lastY  = 0;
static uint32_t s_touchMs = 0;

static bool rawToScreen(int16_t *sx, int16_t *sy) {
    if (!g_ts.touched()) return false;
    TS_Point p = g_ts.getPoint();
    *sx = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0,           SCREEN_W - 1);
    *sy = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, SCREEN_H - 1, 0);
#if CYD_USB_VERSION == 2
    *sx = SCREEN_W - 1 - *sx;
    *sy = SCREEN_H - 1 - *sy;
#endif
    *sx = constrain(*sx, 0, SCREEN_W - 1);
    *sy = constrain(*sy, 0, SCREEN_H - 1);
    return true;
}

void touchInit() {
    g_touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    g_ts.begin(g_touchSPI);
#if CYD_USB_VERSION == 2
    int rot = nvsGetInt("touch_rot", -1);
    if (rot < 0) {
        int oldFlip = nvsGetInt("touch_flip", 1);
        rot = oldFlip ? 2 : 0;
        nvsPutInt("touch_rot", rot);
    }
    s_touchRotation = rot;
    g_ts.setRotation(s_touchRotation);
#else
    g_ts.setRotation(0);
#endif
}

TouchEvent touchPoll() {
    TouchEvent evt;
    int16_t tx, ty;
    bool touched = rawToScreen(&tx, &ty);

    if (touched && !s_held) {
        if (millis() - s_touchMs < DEBOUNCE_MS) return evt;
        s_startX = s_lastX = tx;
        s_startY = s_lastY = ty;
        s_held   = true;
        s_touchMs = millis();
    } else if (touched && s_held) {
        s_lastX = tx;
        s_lastY = ty;
    } else if (!touched && s_held) {
        int dx = s_lastX - s_startX;
        int dy = s_lastY - s_startY;
        int ax = abs(dx), ay = abs(dy);

        if (ax > SWIPE_THRESHOLD && ax * 10 > ay * 15) {
            evt.swipe = (dx < 0) ? SwipeDir::Left : SwipeDir::Right;
        } else if (ay > SWIPE_THRESHOLD && ay * 10 > ax * 15) {
            evt.swipe = (dy < 0) ? SwipeDir::Up : SwipeDir::Down;
        } else if (ax < 30 && ay < 30) {
            evt.tap  = TapEvent::Tap;
            evt.tapX = s_startX;
            evt.tapY = s_startY;
        }
        s_held    = false;
        s_touchMs = millis();
    }
    return evt;
}

bool touchIsHeld(int16_t *ox, int16_t *oy) {
    int16_t tx, ty;
    bool t = rawToScreen(&tx, &ty);
    if (t && ox) *ox = tx;
    if (t && oy) *oy = ty;
    return t;
}

void touchSetRotation(int rotation) {
    s_touchRotation = rotation & 3;
    nvsPutInt("touch_rot", s_touchRotation);
#if CYD_USB_VERSION == 2
    g_ts.setRotation(s_touchRotation);
#endif
}

int touchGetRotation() {
    return s_touchRotation;
}
