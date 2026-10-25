#include "chart.h"
#include "../config/config.h"
#include "../config/spi_bus.h"
#include "crypto.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Arduino.h>
#include <cstring>
#include <cstdlib>

// ── Sizes ─────────────────────────────────────────────────────────────────
// Display resolution matches the sparkline; the raw buffer holds a whole 3M response
// (90 days hourly = 2160) so the downsample can stride over all of it.
#define CHART_POINTS    168
#define CHART_RAW_MAX   2300
#define CHART_MAGIC     0x31545243u   // "CRT1"
#define CHART_VERSION   1

struct ChartCacheHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t period;
    uint32_t count;
    uint32_t fetchedAt;
};

// ── State ─────────────────────────────────────────────────────────────────
// Written by the fetch worker (core 0), read by the UI (core 1) - same arrangement as the
// coin cache, so the same rule: copy under the lock, never hand out a pointer.
static SemaphoreHandle_t s_lock = nullptr;
struct ChartLock {
    ChartLock()  { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~ChartLock() { if (s_lock) xSemaphoreGive(s_lock); }
    ChartLock(const ChartLock &) = delete;
    ChartLock &operator=(const ChartLock &) = delete;
};

static float s_raw[CHART_RAW_MAX];       // worker-only scratch

// One slot per fetched period, so 1M and 3M can both be resident and switching between
// them is instant. A single slot meant every swap re-fetched - and asking for 3M while 1M
// was loading cancelled it (2026-10-25).
#define CHART_SLOTS (CHART_PERIOD_COUNT - CHART_1M)
struct ChartSlot {
    float points[CHART_POINTS];
    int   count = 0;
    char  coin[32] = "";
    bool  valid = false;
};
static ChartSlot s_slots[CHART_SLOTS];

static char        s_pendingCoin[32] = "";
static uint8_t     s_pendingMask = 0;       // bit n = period CHART_1M + n
static uint8_t     s_failedMask  = 0;       // same bits: the last fetch for that slot failed
static uint32_t    s_coolUntil   = 0;       // ms; back off after a failed fetch
static ChartWakeFn s_wake = nullptr;
static volatile bool s_updated = false;     // set on publish, consumed by the UI loop

// ── Small helpers ─────────────────────────────────────────────────────────
const char *chartPeriodLabel(int p) {
    switch (p) {
        case CHART_24H: return "24H";
        case CHART_7D:  return "7D";
        case CHART_1M:  return "1M";
        case CHART_3M:  return "3M";
    }
    return "?";
}

int chartPeriodDays(int p) {
    if (p == CHART_1M) return 30;
    if (p == CHART_3M) return 90;
    return 0;   // 24H / 7D come from the sparkline
}

static uint32_t chartTtlSec(int period) {
    return period == CHART_3M ? 21600 : 3600;   // 3M is daily-scale data; 1M hourly
}

void chartInit() {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
}

void chartSetWakeHook(ChartWakeFn fn) { s_wake = fn; }

// ── Streaming reader ──────────────────────────────────────────────────────
// The body arrives close-delimited (see the fetch below), so it is consumed a byte at a
// time from a small buffer. Nothing holds the response: 223 KB never touches the heap.

class ByteStream {
public:
    explicit ByteStream(Stream &s) : _s(s) {}
    int peek() {
        if (_pos >= _len) {
            _len = _s.readBytes(_buf, sizeof(_buf));
            _pos = 0;
            if (_len == 0) return -1;
        }
        return _buf[_pos];
    }
    int get() { int c = peek(); if (c >= 0) _pos++; return c; }
private:
    Stream &_s;
    uint8_t _buf[256];
    size_t  _pos = 0, _len = 0;
};

static bool isNumberChar(int c) {
    return c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' ||
           (c >= '0' && c <= '9');
}

// Pull the price of every [timestamp, price] pair out of the "prices" array.
static bool parsePrices(Stream &src, float *out, int outMax, int &count) {
    ByteStream in(src);

    // Find "prices" (the response opens with it, but match rather than assume).
    static const char *KEY = "\"prices\"";
    const int keyLen = (int)strlen(KEY);
    int matched = 0, c;
    while ((c = in.get()) >= 0) {
        if (c == KEY[matched]) {
            if (++matched == keyLen) break;
        } else {
            matched = (c == KEY[0]) ? 1 : 0;
        }
    }
    if (c < 0) return false;

    while ((c = in.get()) >= 0 && c != '[') {}   // the array itself
    if (c < 0) return false;

    count = 0;
    int  depth = 1;          // inside the prices array
    bool expectPrice = false; // the next number is a price, not a timestamp
    while (depth > 0) {
        c = in.peek();
        if (c < 0) break;                     // truncated body: keep what we have
        if (c == '[') { depth++; in.get(); }
        else if (c == ']') { depth--; in.get(); }
        else if (c == ',' || c == ' ' || c == '\n' || c == '\r' || c == '\t') { in.get(); }
        else if (isNumberChar(c)) {
            char num[32];
            int n = 0;
            while (n < (int)sizeof(num) - 1) {
                int d = in.peek();
                if (!isNumberChar(d)) break;
                num[n++] = (char)d;
                in.get();
            }
            num[n] = '\0';
            if (depth == 2) {                 // inside a pair: timestamp then price
                if (expectPrice) {
                    if (count < outMax) out[count++] = (float)atof(num);
                    expectPrice = false;
                } else {
                    expectPrice = true;
                }
            }
        } else {
            in.get();                         // unexpected byte: skip it
        }
    }
    return count > 0;
}

// Evenly spaced sample of the raw series, keeping the first and last points.
static int downsample(const float *raw, int n, float *out, int maxPoints) {
    if (n <= maxPoints) {
        memcpy(out, raw, (size_t)n * sizeof(float));
        return n;
    }
    for (int i = 0; i < maxPoints; i++) {
        int idx = (int)((int64_t)i * (n - 1) / (maxPoints - 1));
        out[i] = raw[idx];
    }
    return maxPoints;
}

// ── Cache ─────────────────────────────────────────────────────────────────
static void cachePath(char *out, size_t len, const char *coin, int period) {
    snprintf(out, len, "/cache/c_%s_%d.bin", coin, chartPeriodDays(period));
}

static bool loadCache(const char *coin, int period, float *out, int &count) {
    char path[80];
    cachePath(path, sizeof(path), coin, period);

    SdSession sd;
    if (!sd.ok()) return false;
    if (!SD.exists(path)) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    ChartCacheHeader h {};
    bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h)
           && h.magic == CHART_MAGIC && h.version == CHART_VERSION
           && (int)h.period == period && h.count > 0 && h.count <= CHART_POINTS;
    if (ok) {
        time_t now = time(nullptr);
        if (now > 1000000 && h.fetchedAt > 0 &&
            (now - (time_t)h.fetchedAt) > (time_t)chartTtlSec(period)) {
            ok = false;   // stale
        }
    }
    if (ok) {
        size_t want = sizeof(float) * h.count;
        ok = f.read((uint8_t *)out, want) == want;
        if (ok) count = (int)h.count;
    }
    f.close();
    return ok;
}

static void saveCache(const char *coin, int period, const float *points, int count) {
    char path[80];
    cachePath(path, sizeof(path), coin, period);

    SdSession sd;
    if (!sd.ok()) return;
    if (!SD.exists("/cache")) SD.mkdir("/cache");
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;
    ChartCacheHeader h { CHART_MAGIC, CHART_VERSION, (uint32_t)period,
                         (uint32_t)count, (uint32_t)time(nullptr) };
    f.write((const uint8_t *)&h, sizeof(h));
    f.write((const uint8_t *)points, sizeof(float) * (size_t)count);
    f.close();
}

// ── Fetch ─────────────────────────────────────────────────────────────────
// HTTP/1.0 on purpose: CoinGecko answers HTTP/1.1 requests with Transfer-Encoding: chunked,
// and HTTPClient only strips the chunk framing inside its own body readers. With 1.0 the
// server closes the connection to end the body instead, so getStream() is plain JSON and
// can be parsed as it arrives (verified against the live API, 2026-10-25).
static bool fetchChart(const char *coin, int period, float *points, int &count) {
    int days = chartPeriodDays(period);
    if (days == 0) return false;

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.coingecko.com/api/v3/coins/%s/market_chart"
             "?vs_currency=usd&days=%d", coin, days);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.useHTTP10(true);
    http.setTimeout(15000);
    http.addHeader("Accept", "application/json");
    const char *key = cryptoApiKey();
    if (key && key[0]) http.addHeader("x-cg-demo-api-key", key);

    uint32_t t0 = millis();
    int code = http.GET();
    if (code != 200) {
        Serial.printf("chart %s/%d: HTTP %d after %lums\n", coin, days, code,
                      (unsigned long)(millis() - t0));
        http.end();
        return false;
    }

    int raw = 0;
    bool ok = parsePrices(http.getStream(), s_raw, CHART_RAW_MAX, raw);
    http.end();
    if (!ok) {
        Serial.printf("chart %s/%d: no prices after %lums\n", coin, days,
                      (unsigned long)(millis() - t0));
        return false;
    }
    count = downsample(s_raw, raw, points, CHART_POINTS);
    Serial.printf("chart %s/%d: %d points -> %d in %lums\n", coin, days, raw, count,
                  (unsigned long)(millis() - t0));
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────
void chartRequest(const char *coinId, int period) {
    if (!coinId || period < CHART_1M || period >= CHART_PERIOD_COUNT) return;

    bool wake = false;
    {
        ChartLock lock;
        if (millis() < s_coolUntil) return;                     // a fetch just failed
        ChartSlot &sl = s_slots[period - CHART_1M];
        if (sl.valid && strcmp(sl.coin, coinId) == 0) return;   // already resident

        // A different coin retires anything still queued for the old one.
        if (strcmp(s_pendingCoin, coinId) != 0) {
            strlcpy(s_pendingCoin, coinId, sizeof(s_pendingCoin));
            s_pendingMask = 0;
        }
        uint8_t bit = (uint8_t)(1u << (period - CHART_1M));
        if (s_pendingMask & bit) return;                        // already queued
        s_pendingMask |= bit;
        s_failedMask  &= (uint8_t)~bit;                         // a new attempt, new odds
        wake = true;
    }
    if (wake && s_wake) s_wake();
}

void chartPrefetch(const char *coinId) {
    chartRequest(coinId, CHART_1M);
    chartRequest(coinId, CHART_3M);
}

bool chartServicePending() {
    char coin[32] = "";
    int  period = -1;
    {
        ChartLock lock;
        if (!s_pendingMask) return false;
        int slot = 0;
        while (slot < CHART_SLOTS && !(s_pendingMask & (1u << slot))) slot++;
        if (slot >= CHART_SLOTS) { s_pendingMask = 0; return false; }
        s_pendingMask &= (uint8_t)~(1u << slot);
        period = CHART_1M + slot;
        strlcpy(coin, s_pendingCoin, sizeof(coin));
    }

    float points[CHART_POINTS];
    int   count = 0;

    if (!loadCache(coin, period, points, count)) {
        if (!fetchChart(coin, period, points, count)) {
            s_coolUntil = millis() + 60000;   // do not hammer a failing endpoint
            s_failedMask |= (uint8_t)(1u << (period - CHART_1M));
            return true;                      // the request was still consumed
        }
        s_failedMask &= (uint8_t)~(1u << (period - CHART_1M));
        saveCache(coin, period, points, count);
    }

    {
        ChartLock lock;
        ChartSlot &sl = s_slots[period - CHART_1M];
        memcpy(sl.points, points, sizeof(float) * (size_t)count);
        sl.count = count;
        strlcpy(sl.coin, coin, sizeof(sl.coin));
        sl.valid = true;
    }
    s_updated = true;
    return true;
}

bool chartConsumeUpdated() {
    if (!s_updated) return false;
    s_updated = false;
    return true;
}

int chartStatus(const char *coinId, int period) {
    if (!coinId || period < CHART_1M || period >= CHART_PERIOD_COUNT) return CHART_NONE;
    ChartLock lock;
    ChartSlot &sl = s_slots[period - CHART_1M];
    if (sl.valid && strcmp(sl.coin, coinId) == 0) return CHART_READY;
    if (s_failedMask & (uint8_t)(1u << (period - CHART_1M))) return CHART_FAILED;
    return CHART_NONE;
}

int chartCopyPoints(const char *coinId, int period, float *out, int maxPoints) {
    if (!coinId || !out || maxPoints <= 0) return 0;
    if (period < CHART_1M || period >= CHART_PERIOD_COUNT) return 0;
    ChartLock lock;
    ChartSlot &sl = s_slots[period - CHART_1M];
    if (!sl.valid || strcmp(sl.coin, coinId) != 0) return 0;
    int n = sl.count < maxPoints ? sl.count : maxPoints;
    memcpy(out, sl.points, (size_t)n * sizeof(float));
    return n;
}
