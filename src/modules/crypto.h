#pragma once
#include <cstdint>
#include <cstddef>

#define SPARK_7D    168   // 7 days × 24h hourly

struct CoinData {
    char   id[32];
    char   symbol[12];
    char   name[24];
    double priceUsd;
    float  change24h;
    float  change7d;
    float  high24h;
    float  low24h;
    float  spark7d[SPARK_7D];
    int    spark7dCount;
    bool   valid;
};

// ── Top-50 coins (CoinGecko IDs) ─────────────────────────────────────────
#define TOP50_COUNT 50
#define CUSTOM_COINS_MAX 10
struct CoinDef {
    const char *id;
    const char *symbol;
    const char *name;
};
extern const CoinDef g_top50[TOP50_COUNT];
extern CoinDef g_customCoins[CUSTOM_COINS_MAX];   // user-added from SD
extern int    g_customCoinCount;                   // number loaded

// Helper: get coin def by global index (0-49 = built-in, 50+ = custom)
inline const CoinDef* coinGetDef(int idx) {
    if (idx < 0) return nullptr;
    if (idx < TOP50_COUNT) return &g_top50[idx];
    int ci = idx - TOP50_COUNT;
    if (ci < g_customCoinCount) return &g_customCoins[ci];
    return nullptr;
}
inline int coinTotalCount() { return TOP50_COUNT + g_customCoinCount; }

// ── Coin page assignment ─────────────────────────────────────────────────
// Each page holds up to MAX_COINS_PER_PAGE coin indices (into g_top50).
// NVS-backed: "pgN_c0".."pgN_c3" store coin indices (-1 = empty slot).
bool cryptoPageSetCoin(int page, int slot, int coinIdx);   // coinIdx = -1 to clear
int  cryptoPageGetCoin(int page, int slot);                // returns coinIdx or -1
int  cryptoPageCountActive(int page);                      // number of non-empty slots on this page
void cryptoPageClear(int page);                            // remove all coins from a page
bool cryptoPageHasCoin(int page, int coinIdx);             // check if coin is on this page

// ── Page enable/disable ──────────────────────────────────────────────────
// Disabled pages are skipped in swipe navigation and auto-rotate.
// NVS-backed bitmask (key "pg_enabled"). Default: page 0 enabled.
bool cryptoPageIsEnabled(int page);
void cryptoPageSetEnabled(int page, bool enabled);
int  cryptoGetEnabledPageCount();                          // number of enabled pages (0-5)
int  cryptoGetNextEnabledPage(int current);                // next enabled page (wraps), or -1
int  cryptoGetPrevEnabledPage(int current);                // previous enabled page (wraps), or -1

// ── API fetch ────────────────────────────────────────────────────────────
// Fetch all coins across all active pages in one API call. Returns success.
// If no WiFi, loads from SD cache. Fills CoinData arrays.
bool cryptoFetch(bool force = false);
// ── Fear & Greed ─────────────────────────────────────────────────────────
int  cryptoGetFearGreed();          // 0-100, -1 if unknown
void cryptoGetFearGreedLabel(char *buf, size_t len);

// ── Getters ──────────────────────────────────────────────────────────────
const CoinData* cryptoGetCoinData(const char *coinId);     // null if not loaded
bool cryptoIsFromCache();
void cryptoGetSyncTime(char *buf, size_t len);             // "2:05 PM"

// ── Custom coins from SD ─────────────────────────────────────────────────
void cryptoLoadCustomCoins();

// ── Init ─────────────────────────────────────────────────────────────────
void cryptoInit();
