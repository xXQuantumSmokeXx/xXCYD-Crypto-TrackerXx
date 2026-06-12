#include "crypto.h"
#include "../config/config.h"
#include "../config/nvs_config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <time.h>
#include <cstring>
#include <cstdio>

// ── Custom coins (loaded from SD) ─────────────────────────────────────────
CoinDef g_customCoins[CUSTOM_COINS_MAX] = {};
int    g_customCoinCount = 0;

// ── Top-50 coins ──────────────────────────────────────────────────────────
const CoinDef g_top50[TOP50_COUNT] = {
    {"bitcoin",              "BTC",  "Bitcoin"},
    {"ethereum",             "ETH",  "Ethereum"},
    {"tether",               "USDT", "Tether"},
    {"binancecoin",          "BNB",  "BNB"},
    {"solana",               "SOL",  "Solana"},
    {"ripple",               "XRP",  "XRP"},
    {"dogecoin",             "DOGE", "Dogecoin"},
    {"cardano",              "ADA",  "Cardano"},
    {"tron",                 "TRX",  "TRON"},
    {"avalanche-2",          "AVAX", "Avalanche"},
    {"shiba-inu",            "SHIB", "Shiba Inu"},
    {"sui",                  "SUI",  "Sui"},
    {"wrapped-bitcoin",      "WBTC", "Wrapped BTC"},
    {"polkadot",             "DOT",  "Polkadot"},
    {"chainlink",            "LINK", "Chainlink"},
    {"bitcoin-cash",         "BCH",  "Bitcoin Cash"},
    {"near",                 "NEAR", "NEAR Protocol"},
    {"uniswap",              "UNI",  "Uniswap"},
    {"litecoin",             "LTC",  "Litecoin"},
    {"matic-network",        "MATIC","Polygon"},
    {"internet-computer",    "ICP",  "Internet Computer"},
    {"stellar",              "XLM",  "Stellar"},
    {"monero",               "XMR",  "Monero"},
    {"cosmos",               "ATOM", "Cosmos"},
    {"crypto-com-chain",     "CRO",  "Cronos"},
    {"filecoin",             "FIL",  "Filecoin"},
    {"vechain",              "VET",  "VeChain"},
    {"arbitrum",             "ARB",  "Arbitrum"},
    {"optimism",             "OP",   "Optimism"},
    {"hedera-hashgraph",     "HBAR", "Hedera"},
    {"the-graph",            "GRT",  "The Graph"},
    {"injective-protocol",   "INJ",  "Injective"},
    {"render-token",         "RNDR", "Render"},
    {"starknet",             "STRK", "Starknet"},
    {"immutable-x",          "IMX",  "Immutable X"},
    {"maker",                "MKR",  "Maker"},
    {"thorchain",            "RUNE", "THORChain"},
    {"algorand",             "ALGO", "Algorand"},
    {"aave",                 "AAVE", "Aave"},
    {"kaspa",                "KAS",  "Kaspa"},
    {"quant-network",        "QNT",  "Quant"},
    {"flow",                 "FLOW", "Flow"},
    {"multiversx",           "EGLD", "MultiversX"},
    {"fantom",               "FTM",  "Fantom"},
    {"coq-inu",              "COQ",  "Coq Inu"},
    {"zcash",                "ZEC",  "Zcash"},
    {"jupiter-exchange-solana","JUP","Jupiter"},
    {"bonk",                 "BONK", "Bonk"},
    {"celestia",             "TIA",  "Celestia"},
    {"tezos",                "XTZ",  "Tezos"},
};

// ── Internal state ────────────────────────────────────────────────────────
static CoinData s_coinCache[COIN_MAX];   // runtime cache for all assigned coins
static int      s_coinCount = 0;
static bool     s_fromCache = false;
static char     s_syncTime[12] = "--:--";
static int      s_fgValue   = -1;
static char     s_fgLabel[16] = "";

// Per-page coin assignments: -1 = empty. Persisted in NVS.
static int8_t s_pages[MAX_CRYPTO_PAGES][MAX_COINS_PER_PAGE];
// Page enable bitmask: bit 0 = page 0, etc. Persisted in NVS.
static uint8_t s_pageEnabled = 0x01;  // default: only page 0 enabled

// ── NVS page helpers ──────────────────────────────────────────────────────
bool cryptoPageSetCoin(int page, int slot, int coinIdx) {
    if (page < 0 || page >= MAX_CRYPTO_PAGES) return false;
    if (slot < 0 || slot >= MAX_COINS_PER_PAGE) return false;
    if (coinIdx < -1 || coinIdx >= TOP50_COUNT + CUSTOM_COINS_MAX) return false;

    s_pages[page][slot] = (int8_t)coinIdx;

    char key[12];
    snprintf(key, sizeof(key), "pg%d_c%d", page, slot);
    nvsPutInt(key, (int32_t)coinIdx);
    return true;
}

int cryptoPageGetCoin(int page, int slot) {
    if (page < 0 || page >= MAX_CRYPTO_PAGES) return -1;
    if (slot < 0 || slot >= MAX_COINS_PER_PAGE) return -1;
    return (int)s_pages[page][slot];
}

int cryptoPageCountActive(int page) {
    int n = 0;
    for (int s = 0; s < MAX_COINS_PER_PAGE; s++)
        if (s_pages[page][s] >= 0) n++;
    return n;
}

void cryptoPageClear(int page) {
    if (page < 0 || page >= MAX_CRYPTO_PAGES) return;
    for (int s = 0; s < MAX_COINS_PER_PAGE; s++) {
        s_pages[page][s] = -1;
        char key[12];
        snprintf(key, sizeof(key), "pg%d_c%d", page, s);
        nvsPutInt(key, -1);
    }
}

bool cryptoPageHasCoin(int page, int coinIdx) {
    if (page < 0 || page >= MAX_CRYPTO_PAGES) return false;
    for (int s = 0; s < MAX_COINS_PER_PAGE; s++)
        if (s_pages[page][s] == coinIdx) return true;
    return false;
}

// ── Page enable/disable ──────────────────────────────────────────────────
bool cryptoPageIsEnabled(int page) {
    if (page < 0 || page >= MAX_CRYPTO_PAGES) return false;
    return (s_pageEnabled >> page) & 1;
}

void cryptoPageSetEnabled(int page, bool enabled) {
    if (page < 0 || page >= MAX_CRYPTO_PAGES) return;
    if (enabled)
        s_pageEnabled |= (1 << page);
    else
        s_pageEnabled &= ~(1 << page);
    nvsPutInt("pg_enabled", (int32_t)s_pageEnabled);
}

int cryptoGetEnabledPageCount() {
    int n = 0;
    for (int p = 0; p < MAX_CRYPTO_PAGES; p++)
        if (cryptoPageIsEnabled(p)) n++;
    return n;
}

int cryptoGetNextEnabledPage(int current) {
    for (int i = 1; i <= MAX_CRYPTO_PAGES; i++) {
        int p = (current + i) % MAX_CRYPTO_PAGES;
        if (cryptoPageIsEnabled(p)) return p;
    }
    return current;  // no other enabled pages, stay put
}

int cryptoGetPrevEnabledPage(int current) {
    for (int i = 1; i <= MAX_CRYPTO_PAGES; i++) {
        int p = (current - i + MAX_CRYPTO_PAGES) % MAX_CRYPTO_PAGES;
        if (cryptoPageIsEnabled(p)) return p;
    }
    return current;
}

// ── Page loading from NVS ─────────────────────────────────────────────────
static void cryptoPagesLoad() {
    for (int p = 0; p < MAX_CRYPTO_PAGES; p++) {
        for (int s = 0; s < MAX_COINS_PER_PAGE; s++) {
            char key[12];
            snprintf(key, sizeof(key), "pg%d_c%d", p, s);
            int32_t val = nvsGetInt(key, -1);
            int maxIdx = TOP50_COUNT + CUSTOM_COINS_MAX;
            s_pages[p][s] = (int8_t)((val >= 0 && val < maxIdx) ? val : -1);
        }
    }

    // Defaults if nothing configured: page 0 gets BTC + ETH
    bool any = false;
    for (int p = 0; p < MAX_CRYPTO_PAGES && !any; p++)
        for (int s = 0; s < MAX_COINS_PER_PAGE && !any; s++)
            if (s_pages[p][s] >= 0) any = true;

    if (!any) {
        s_pages[0][0] = 0;  // BTC
        s_pages[0][1] = 1;  // ETH
    }

    // Load enabled page mask
    int32_t mask = nvsGetInt("pg_enabled", -1);
    if (mask < 0) {
        // First boot: enable any page that has coins
        s_pageEnabled = 0;
        for (int p = 0; p < MAX_CRYPTO_PAGES; p++)
            if (cryptoPageCountActive(p) > 0)
                s_pageEnabled |= (1 << p);
        if (s_pageEnabled == 0) s_pageEnabled = 0x01;  // at least page 0
        nvsPutInt("pg_enabled", (int32_t)s_pageEnabled);
    } else {
        s_pageEnabled = (uint8_t)(mask & 0x1F);
    }
}

// ── Collect all unique coin IDs from active pages ─────────────────────────
static int cryptoCollectActiveIds(const char **idsOut, int maxIds) {
    bool seen[TOP50_COUNT + CUSTOM_COINS_MAX] = {};
    int count = 0;
    for (int p = 0; p < MAX_CRYPTO_PAGES; p++) {
        for (int s = 0; s < MAX_COINS_PER_PAGE; s++) {
            int idx = s_pages[p][s];
            if (idx >= 0 && idx < TOP50_COUNT + CUSTOM_COINS_MAX && !seen[idx]) {
                seen[idx] = true;
                const CoinDef *def = coinGetDef(idx);
                if (def) idsOut[count++] = def->id;
                if (count >= maxIds) return count;
            }
        }
    }
    return count;
}

// ── SD cache ─────────────────────────────────────────────────────────────
static bool saveCacheToSD(const String &json) {
    if (!SD.begin(SD_CS)) return false;
    if (!SD.exists("/cache")) SD.mkdir("/cache");
    File f = SD.open("/cache/btc.json", FILE_WRITE);
    if (!f) { SD.end(); return false; }
    f.print(json);
    f.close();
    SD.end();
    nvsPutInt("btc_cached_at", (int)time(nullptr));
    return true;
}

static bool loadCacheFromSD(String &json) {
    int cachedAt = nvsGetInt("btc_cached_at", 0);
    if (cachedAt == 0) return false;
    time_t now = time(nullptr);
    if (now > 1000000 && (now - cachedAt) > CACHE_TTL_SEC) return false;
    if (!SD.begin(SD_CS)) return false;
    if (!SD.exists("/cache/btc.json")) { SD.end(); return false; }
    File f = SD.open("/cache/btc.json", FILE_READ);
    if (!f) { SD.end(); return false; }
    json = "";
    while (f.available()) json += (char)f.read();
    f.close();
    SD.end();
    return json.length() > 10;
}

// ── JSON parser for coins/markets ─────────────────────────────────────────
static void parseCoinsJson(const String &json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return;
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) return;

    // Don't invalidate existing data — only update coins found in the response.
    // This prevents "Loading data..." flicker during re-fetches.

    int arrSize = arr.size();
    for (int ai = 0; ai < arrSize; ai++) {
        const char *id = arr[ai]["id"].as<const char *>();
        if (!id) continue;

        for (int ci = 0; ci < s_coinCount; ci++) {
            if (strcmp(s_coinCache[ci].id, id) != 0) continue;

            const char *sym  = arr[ai]["symbol"].as<const char *>();
            const char *name = arr[ai]["name"].as<const char *>();
            strlcpy(s_coinCache[ci].symbol, sym  ? sym  : "???", sizeof(s_coinCache[ci].symbol));
            strlcpy(s_coinCache[ci].name,   name ? name : "???", sizeof(s_coinCache[ci].name));
            for (char *p = s_coinCache[ci].symbol; *p; p++) *p = toupper((unsigned char)*p);

            s_coinCache[ci].priceUsd  = arr[ai]["current_price"].as<double>();
            s_coinCache[ci].change24h = arr[ai]["price_change_percentage_24h"].as<float>();
            s_coinCache[ci].change7d  = arr[ai]["price_change_percentage_7d_in_currency"].as<float>();
            s_coinCache[ci].high24h   = arr[ai]["high_24h"].as<float>();
            s_coinCache[ci].low24h    = arr[ai]["low_24h"].as<float>();

            // 7d sparkline
            JsonArray spark = arr[ai]["sparkline_in_7d"]["price"].as<JsonArray>();
            int n = 0, sparkSize = spark.size();
            for (int si = 0; si < sparkSize && n < SPARK_7D; si++)
                s_coinCache[ci].spark7d[n++] = spark[si].as<float>();
            s_coinCache[ci].spark7dCount = n;
            s_coinCache[ci].valid = true;
            break;
        }
    }

    struct tm ti;
    if (getLocalTime(&ti, 0)) {
        int h = ti.tm_hour;
        const char *ap = h >= 12 ? "PM" : "AM";
        if (h > 12) h -= 12;
        else if (h == 0) h = 12;
        snprintf(s_syncTime, sizeof(s_syncTime), "%d:%02d %s", h, ti.tm_min, ap);
    }
}

// ── Network fetch: coins/markets ──────────────────────────────────────────
static bool fetchCoins() {
    const char *ids[COIN_MAX];
    s_coinCount = cryptoCollectActiveIds(ids, COIN_MAX);
    if (s_coinCount == 0) return false;

    // Build coin cache entries
    for (int i = 0; i < s_coinCount; i++) {
        bool isNew = (strcmp(s_coinCache[i].id, ids[i]) != 0);
        strlcpy(s_coinCache[i].id, ids[i], sizeof(s_coinCache[i].id));
        if (isNew) {
            s_coinCache[i].valid = false;
            s_coinCache[i].spark7dCount = 0;
        }
    }

    char idsStr[384] = "";
    for (int i = 0; i < s_coinCount; i++) {
        if (i > 0) strlcat(idsStr, ",", sizeof(idsStr));
        strlcat(idsStr, ids[i], sizeof(idsStr));
    }

    char url[512];
    snprintf(url, sizeof(url),
        "https://api.coingecko.com/api/v3/coins/markets"
        "?vs_currency=usd&ids=%s&order=market_cap_desc"
        "&sparkline=true&price_change_percentage=7d",
        idsStr);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(15000);
    http.addHeader("Accept", "application/json");
#ifdef COINGECKO_API_KEY
    http.addHeader("x-cg-demo-api-key", COINGECKO_API_KEY);
#endif
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    String json = http.getString();
    http.end();

    parseCoinsJson(json);
    saveCacheToSD(json);
    s_fromCache = false;
    return true;
}

// ── Fear & Greed ──────────────────────────────────────────────────────────
static void fetchFearGreed() {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.alternative.me/fng/?limit=1");
    http.setTimeout(8000);
    if (http.GET() != 200) { http.end(); return; }
    String json = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, json)) return;
    int val = doc["data"][0]["value"].as<int>();
    if (val < 0 || val > 100) return;
    s_fgValue = val;
    const char *cls = doc["data"][0]["value_classification"] | "";
    strlcpy(s_fgLabel, cls, sizeof(s_fgLabel));
    for (char *p = s_fgLabel; *p; p++) *p = toupper((unsigned char)*p);
}

// ── Main fetch entry point ────────────────────────────────────────────────
bool cryptoFetch(bool force) {
    if (force) nvsPutInt("btc_cached_at", 0);
    s_fromCache = false;

    if (WiFi.isConnected()) {
        if (fetchCoins()) {
            fetchFearGreed();
            return true;
        }
    }

    // Fall back to SD cache
    String json;
    if (loadCacheFromSD(json)) {
        const char *ids[COIN_MAX];
        s_coinCount = cryptoCollectActiveIds(ids, COIN_MAX);
        for (int i = 0; i < s_coinCount; i++) {
            strlcpy(s_coinCache[i].id, ids[i], sizeof(s_coinCache[i].id));
        }
        parseCoinsJson(json);
        s_fromCache = true;
        return true;
    }
    return false;
}

// ── Getters ───────────────────────────────────────────────────────────────
const CoinData* cryptoGetCoinData(const char *coinId) {
    for (int i = 0; i < s_coinCount; i++)
        if (strcmp(s_coinCache[i].id, coinId) == 0 && s_coinCache[i].valid)
            return &s_coinCache[i];
    return nullptr;
}

bool cryptoIsFromCache()  { return s_fromCache; }
void cryptoGetSyncTime(char *buf, size_t len) {
    strlcpy(buf, s_syncTime, len);
}

int  cryptoGetFearGreed() { return s_fgValue; }
void cryptoGetFearGreedLabel(char *buf, size_t len) {
    strlcpy(buf, s_fgLabel, len);
}

// ── Custom coins from SD ──────────────────────────────────────────────────
void cryptoLoadCustomCoins() {
    g_customCoinCount = 0;
    if (!SD.begin(SD_CS)) return;

    if (!SD.exists("/custom_coins.txt")) { SD.end(); return; }

    File f = SD.open("/custom_coins.txt", FILE_READ);
    if (!f) { SD.end(); return; }

    while (f.available() && g_customCoinCount < CUSTOM_COINS_MAX) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;

        // Parse: "coin_id" or "coin_id,SYMBOL"
        int comma = line.indexOf(',');
        String id, sym;
        if (comma > 0) {
            id = line.substring(0, comma);
            sym = line.substring(comma + 1);
        } else {
            id = line;
            sym = id.substring(0, min((int)id.length(), 8));
        }
        id.trim(); sym.trim();
        if (id.length() == 0) continue;

        // Check for duplicate
        bool dup = false;
        for (int i = 0; i < g_customCoinCount; i++) {
            if (id.equalsIgnoreCase(g_customCoins[i].id)) { dup = true; break; }
        }
        for (int i = 0; i < TOP50_COUNT && !dup; i++) {
            if (id.equalsIgnoreCase(g_top50[i].id)) { dup = true; break; }
        }
        if (dup) continue;

        // Store in custom array (strings live in a static buffer)
        static char idBufs[CUSTOM_COINS_MAX][32];
        static char symBufs[CUSTOM_COINS_MAX][12];
        strlcpy(idBufs[g_customCoinCount],  id.c_str(),  32);
        strlcpy(symBufs[g_customCoinCount], sym.c_str(), 12);
        for (char *p = symBufs[g_customCoinCount]; *p; p++) *p = toupper((unsigned char)*p);

        g_customCoins[g_customCoinCount].id     = idBufs[g_customCoinCount];
        g_customCoins[g_customCoinCount].symbol = symBufs[g_customCoinCount];
        g_customCoins[g_customCoinCount].name   = idBufs[g_customCoinCount];  // name = id until API returns real name
        g_customCoinCount++;
    }
    f.close();
    SD.end();
    Serial.printf("Loaded %d custom coins from SD\n", g_customCoinCount);
}

// ── Init ──────────────────────────────────────────────────────────────────
#define CRYPTO_CFG_VERSION 1  // bump to force NVS wipe on schema changes

void cryptoInit() {
    // ── NVS migration: wipe stale data if config version changed ────────────
    int32_t storedVer = nvsGetInt("cfg_ver", -1);
    if (storedVer != CRYPTO_CFG_VERSION) {
        Serial.printf("Crypto NVS migration: v%d → v%d — wiping old data\n",
                      (int)storedVer, CRYPTO_CFG_VERSION);
        // Wipe all crypto page keys
        for (int p = 0; p < MAX_CRYPTO_PAGES; p++) {
            for (int s = 0; s < MAX_COINS_PER_PAGE; s++) {
                char key[12];
                snprintf(key, sizeof(key), "pg%d_c%d", p, s);
                nvsPutInt(key, -1);
            }
        }
        nvsPutInt("pg_enabled", 0x01);  // only page 0 enabled
        nvsPutInt("btc_cached_at", 0);  // invalidate cache
        nvsPutInt("cfg_ver", CRYPTO_CFG_VERSION);
    }

    cryptoLoadCustomCoins();  // reads /custom_coins.txt from SD
    cryptoPagesLoad();
    // Don't fetch here — caller decides when (after WiFi connects)
}
