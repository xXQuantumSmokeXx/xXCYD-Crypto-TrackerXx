#include "crypto.h"
#include "../config/config.h"
#include "../config/nvs_config.h"
#include "../config/spi_bus.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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

// ── Cross-core access to the cache ────────────────────────────────────────
// The fetch worker writes this on core 0 while the UI draws from it on core 1. A coin is
// updated field by field - a double, five floats, 168 sparkline floats, three strlcpy's -
// so an unlocked read can draw a torn price or a half-copied name. Every access takes this
// mutex, and readers COPY under it (cryptoCopyCoinData): a pointer handed back from inside
// the lock outlives the lock by definition, which is how the old getter was unsafe even
// though it "only read" (2026-10-25).
//
// Lock order, where both are held: SPI bus first, then this one. Nothing takes them in the
// other order, so the pair cannot deadlock.
static SemaphoreHandle_t s_dataMutex = nullptr;

struct DataLock {
    DataLock()  { if (s_dataMutex) xSemaphoreTake(s_dataMutex, portMAX_DELAY); }
    ~DataLock() { if (s_dataMutex) xSemaphoreGive(s_dataMutex); }
    DataLock(const DataLock &) = delete;
    DataLock &operator=(const DataLock &) = delete;
};
static char     s_apiKey[48] = "";       // loaded from /apikey.txt on SD
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

// ── Page layout from SD ───────────────────────────────────────────────────
// Set the whole layout without touching the picker, the same way wifi.txt and apikey.txt
// work. /pages.txt, one page per line, coins comma-separated, any order:
//
//     P1: BTC, AVAX, SUI, ZEC
//     P2: ICP, STRK, JUP, ADA
//
// A token is a CoinGecko id or a symbol, case-insensitive, from the built-in 50 or from
// custom_coins.txt. The file is authoritative for the layout WHEN IT CHANGES: its hash is
// kept in NVS, so edits made in the picker survive a reboot, and editing the file re-applies
// it on the next boot. Pages not mentioned are cleared and disabled - list them if you want
// to keep them. Unknown coins and over-full pages are logged and skipped rather than
// silently dropped, and a file with nothing usable in it changes nothing.
#define PAGES_FILE "/pages.txt"

static uint32_t fnv1a(const char *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 16777619u;
    }
    return h;
}

static int findCoinIndex(const String &token) {
    for (int i = 0; i < TOP50_COUNT; i++) {
        if (token.equalsIgnoreCase(g_top50[i].id) ||
            token.equalsIgnoreCase(g_top50[i].symbol))
            return i;
    }
    for (int i = 0; i < g_customCoinCount; i++) {
        if (token.equalsIgnoreCase(g_customCoins[i].id) ||
            token.equalsIgnoreCase(g_customCoins[i].symbol))
            return TOP50_COUNT + i;
    }
    return -1;
}

static void cryptoApplyPageConfigFromSD() {
    String text;
    {
        SdSession sd;
        if (!sd.ok()) return;
        if (!SD.exists(PAGES_FILE)) return;
        File f = SD.open(PAGES_FILE, FILE_READ);
        if (!f) return;
        while (f.available()) text += (char)f.read();
        f.close();
    }
    if (text.length() == 0) return;

    uint32_t hash = fnv1a(text.c_str(), text.length());
    if ((uint32_t)nvsGetInt("pgcfg_hash", 0) == hash) return;   // already applied

    int8_t layout[MAX_CRYPTO_PAGES][MAX_COINS_PER_PAGE];
    int    count[MAX_CRYPTO_PAGES] = {};
    for (int p = 0; p < MAX_CRYPTO_PAGES; p++)
        for (int s = 0; s < MAX_COINS_PER_PAGE; s++)
            layout[p][s] = -1;

    int applied = 0, skipped = 0, lineNo = 0, pos = 0;
    while (pos < (int)text.length()) {
        int nl = text.indexOf('\n', pos);
        if (nl < 0) nl = text.length();
        String line = text.substring(pos, nl);
        pos = nl + 1;
        lineNo++;
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;

        int colon = line.indexOf(':');
        if (colon <= 1 || (line[0] != 'P' && line[0] != 'p')) {
            Serial.printf("pages.txt:%d: expected 'P<n>: COIN, COIN' - skipped\n", lineNo);
            skipped++;
            continue;
        }
        int pageNo = line.substring(1, colon).toInt();
        if (pageNo < 1 || pageNo > MAX_CRYPTO_PAGES) {
            Serial.printf("pages.txt:%d: page %d out of range 1-%d - skipped\n",
                          lineNo, pageNo, MAX_CRYPTO_PAGES);
            skipped++;
            continue;
        }
        int page = pageNo - 1;

        String rest = line.substring(colon + 1);
        for (int s = 0; s <= (int)rest.length(); ) {
            int comma = rest.indexOf(',', s);
            if (comma < 0) comma = rest.length();
            String token = rest.substring(s, comma);
            token.trim();
            s = comma + 1;
            if (token.length() == 0) continue;

            int idx = findCoinIndex(token);
            if (idx < 0) {
                Serial.printf("pages.txt:%d: unknown coin '%s' - skipped\n",
                              lineNo, token.c_str());
                skipped++;
                continue;
            }
            bool dup = false;
            for (int k = 0; k < count[page]; k++)
                if (layout[page][k] == idx) dup = true;
            if (dup) continue;
            if (count[page] >= MAX_COINS_PER_PAGE) {
                Serial.printf("pages.txt:%d: page %d is full (%d) - '%s' skipped\n",
                              lineNo, pageNo, MAX_COINS_PER_PAGE, token.c_str());
                skipped++;
                continue;
            }
            layout[page][count[page]++] = (int8_t)idx;
            applied++;
        }
    }

    if (applied == 0) {
        Serial.println("pages.txt: nothing usable - page layout left as it was");
        return;
    }

    // Authoritative: clear every page, then write exactly what the file asked for.
    for (int p = 0; p < MAX_CRYPTO_PAGES; p++) {
        cryptoPageClear(p);
        for (int s = 0; s < MAX_COINS_PER_PAGE; s++)
            cryptoPageSetCoin(p, s, layout[p][s]);
        cryptoPageSetEnabled(p, count[p] > 0);
    }
    nvsPutInt("pgcfg_hash", (int32_t)hash);

    Serial.printf("pages.txt: applied %d coin(s) to %d page(s), %d skipped\n",
                  applied, cryptoGetEnabledPageCount(), skipped);
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
// Binary, not the raw JSON. The old cache stored the whole response - ~79 KB for 20 coins
// (measured against the live API), written and then re-parsed on reload - and every one of
// those bytes was a String allocation on a device with ~300 KB of heap. This is one
// fixed-size record per coin, 15.5 KB for 20, needs no parser to read back, and cannot
// fail for want of memory. The old btc.json is simply ignored; no migration needed.
#define CACHE_FILE    "/cache/btc.bin"
#define CACHE_MAGIC   0x314B5943u   // "CYK1"
#define CACHE_VERSION 1

struct CacheHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
};

// The file is raw CoinData records, so the layout IS the format. If the struct ever
// changes, CACHE_VERSION has to change with it or old files read back as garbage - fail
// the build instead of finding that out on the device.
static_assert(sizeof(CoinData) == 776, "CoinData changed - bump CACHE_VERSION");

static void setSyncTimeFrom(time_t t) {
    struct tm ti;
    if (!localtime_r(&t, &ti)) return;
    int h = ti.tm_hour;
    const char *ap = h >= 12 ? "PM" : "AM";
    if (h > 12) h -= 12;
    else if (h == 0) h = 12;
    snprintf(s_syncTime, sizeof(s_syncTime), "%d:%02d %s", h, ti.tm_min, ap);
}

static bool saveCacheToSD() {
    SdSession sd;
    if (!sd.ok()) return false;
    if (!SD.exists("/cache")) SD.mkdir("/cache");
    File f = SD.open(CACHE_FILE, FILE_WRITE);
    if (!f) return false;

    bool ok;
    {
        DataLock lock;   // the snapshot must be one set, not a mix of two fetches
        CacheHeader h { CACHE_MAGIC, CACHE_VERSION, (uint16_t)s_coinCount };
        size_t want = sizeof(CoinData) * (size_t)s_coinCount;
        ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h)
          && f.write((const uint8_t *)s_coinCache, want) == want;
    }
    f.close();
    if (ok) nvsPutInt("btc_cached_at", (int)time(nullptr));
    return ok;
}

static bool loadCacheFromSD() {
    // No age limit (2026-10-25). The cache answers "what were the prices when we last heard
    // from the network", and an hour-old answer beats a blank screen. The stored time is
    // carried to the status bar, which marks the data CACHED - the age is visible, not
    // hidden. Before this, the cache refused anything older than CACHE_TTL_SEC, and
    // cryptoFetch(force) zeroed the stamp before even trying the network; between them an
    // offline CYD showed nothing at all five minutes after the last successful fetch.
    int cachedAt = nvsGetInt("btc_cached_at", 0);
    if (cachedAt == 0) return false;

    SdSession sd;
    if (!sd.ok()) return false;
    if (!SD.exists(CACHE_FILE)) return false;
    File f = SD.open(CACHE_FILE, FILE_READ);
    if (!f) return false;

    CacheHeader h {};
    bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h)
           && h.magic == CACHE_MAGIC && h.version == CACHE_VERSION && h.count <= COIN_MAX;
    if (ok) {
        size_t want = sizeof(CoinData) * (size_t)h.count;
        DataLock lock;
        ok = f.read((uint8_t *)s_coinCache, want) == want;
        if (ok) {
            s_coinCount = h.count;
            setSyncTimeFrom((time_t)cachedAt);
        }
    }
    f.close();
    return ok;
}

// ── JSON parser for coins/markets ─────────────────────────────────────────
// CoinGecko sends 28 fields per coin and the UI reads nine of them (checked against the
// live API on 2026-10-25). The 168-point sparkline is most of the bytes either way, so the
// saving here is per-coin overhead rather than a step change - the cache below is what
// removes the big allocation.
static void buildCoinFilter(JsonDocument &filter) {
    JsonArray  arr  = filter.to<JsonArray>();
    JsonObject coin = arr.add<JsonObject>();
    coin["id"] = true;
    coin["symbol"] = true;
    coin["name"] = true;
    coin["current_price"] = true;
    coin["price_change_percentage_24h"] = true;
    coin["price_change_percentage_7d_in_currency"] = true;
    coin["high_24h"] = true;
    coin["low_24h"] = true;
    coin["sparkline_in_7d"]["price"] = true;
}

static void applyCoinsJson(JsonArray arr) {
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

            // 7d sparkline - only when the response carried one. A price-only fetch leaves
            // the previous points untouched, which is the point of splitting the two: prices
            // move every minute, sparklines are hourly data (2026-10-25).
            JsonArray spark = arr[ai]["sparkline_in_7d"]["price"].as<JsonArray>();
            int sparkSize = spark.isNull() ? 0 : (int)spark.size();
            if (sparkSize > 0) {
                int n = 0;
                for (int si = 0; si < sparkSize && n < SPARK_7D; si++)
                    s_coinCache[ci].spark7d[n++] = spark[si].as<float>();
                s_coinCache[ci].spark7dCount = n;
            }
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
static bool fetchCoins(bool withSparkline) {
    const char *ids[COIN_MAX];
    const int count = cryptoCollectActiveIds(ids, COIN_MAX);
    if (count == 0) return false;

    // Publish the new set, and mark any changed coin not-yet-valid, under the lock.
    {
        DataLock lock;
        s_coinCount = count;
        for (int i = 0; i < count; i++) {
            bool isNew = (strcmp(s_coinCache[i].id, ids[i]) != 0);
            strlcpy(s_coinCache[i].id, ids[i], sizeof(s_coinCache[i].id));
            if (isNew) {
                s_coinCache[i].valid = false;
                s_coinCache[i].spark7dCount = 0;
            }
        }
    }

    char idsStr[384] = "";
    for (int i = 0; i < count; i++) {
        if (i > 0) strlcat(idsStr, ",", sizeof(idsStr));
        strlcat(idsStr, ids[i], sizeof(idsStr));
    }

    char url[512];
    snprintf(url, sizeof(url),
        "https://api.coingecko.com/api/v3/coins/markets"
        "?vs_currency=usd&ids=%s&order=market_cap_desc"
        "&sparkline=%s&price_change_percentage=7d",
        idsStr, withSparkline ? "true" : "false");

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(15000);
    http.addHeader("Accept", "application/json");
    if (s_apiKey[0]) http.addHeader("x-cg-demo-api-key", s_apiKey);
#ifdef COINGECKO_API_KEY
    else http.addHeader("x-cg-demo-api-key", COINGECKO_API_KEY);
#endif
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    // getString(), NOT getStream(). CoinGecko answers "Transfer-Encoding: chunked" with no
    // Content-Length (checked against the live API on 2026-10-25), and the chunk framing is
    // stripped only inside HTTPClient's own body readers - getStream() hands back the raw
    // socket, so parsing it would feed chunk-size lines to the JSON parser. The copy this
    // costs is repaid by the filter below, which keeps the document to the nine fields the
    // UI reads instead of every field CoinGecko sends.
    String json = http.getString();
    http.end();
    if (json.length() == 0) return false;

    JsonDocument filter;
    buildCoinFilter(filter);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
                                               DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("coins/markets parse failed: %s\n", err.c_str());
        return false;   // cryptoFetch falls through to the cache
    }

    JsonArray coins = doc.as<JsonArray>();
    if (coins.isNull() || coins.size() == 0) {
        // Never write an empty cache over a good one: if the filter or the response shape
        // ever drifts, the screen should keep showing the last real prices.
        Serial.println("coins/markets returned no usable coins");
        return false;
    }
    {
        DataLock lock;
        applyCoinsJson(coins);
    }
    if (withSparkline) nvsPutInt("spark_fetched_at", (int)time(nullptr));
    saveCacheToSD();
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
    const char *cls = doc["data"][0]["value_classification"] | "";
    DataLock lock;
    s_fgValue = val;
    strlcpy(s_fgLabel, cls, sizeof(s_fgLabel));
    for (char *p = s_fgLabel; *p; p++) *p = toupper((unsigned char)*p);
}

// ── Main fetch entry point ────────────────────────────────────────────────
// Sparklines are hourly data on an endpoint whose prices move every minute. Fetching them
// with every refresh made 79% of the payload - 80,831 bytes against 17,035 for 20 coins,
// measured 2026-10-25 - and the largest heap allocation in the app. They are fetched when
// stale, or when any assigned coin has none (a newly added coin, or an older cache).
static bool needSparklineRefresh() {
    int sparkAt = nvsGetInt("spark_fetched_at", 0);
    if (sparkAt == 0) return true;
    time_t now = time(nullptr);
    if (now > 1000000 && (now - sparkAt) > SPARK_TTL_SEC) return true;

    DataLock lock;
    for (int i = 0; i < s_coinCount; i++)
        if (s_coinCache[i].spark7dCount == 0) return true;
    return false;
}

bool cryptoFetch() {
    s_fromCache = false;

    if (WiFi.isConnected()) {
        if (fetchCoins(needSparklineRefresh())) {
            fetchFearGreed();
            return true;
        }
    }

    // Fall back to the SD cache - it holds the same records the network path fills in.
    if (loadCacheFromSD()) {
        s_fromCache = true;
        return true;
    }
    return false;
}

// ── Getters ───────────────────────────────────────────────────────────────
bool cryptoCopyCoinData(const char *coinId, CoinData *out) {
    if (!coinId || !out) return false;
    DataLock lock;
    for (int i = 0; i < s_coinCount; i++) {
        if (strcmp(s_coinCache[i].id, coinId) == 0 && s_coinCache[i].valid) {
            memcpy(out, &s_coinCache[i], sizeof(CoinData));
            return true;
        }
    }
    return false;
}

bool cryptoIsFromCache()  { return s_fromCache; }
void cryptoGetSyncTime(char *buf, size_t len) {
    DataLock lock;
    strlcpy(buf, s_syncTime, len);
}

int  cryptoGetFearGreed() { return s_fgValue; }
void cryptoGetFearGreedLabel(char *buf, size_t len) {
    DataLock lock;
    strlcpy(buf, s_fgLabel, len);
}

// ── Custom coins from SD ──────────────────────────────────────────────────
void cryptoLoadCustomCoins() {
    g_customCoinCount = 0;
    SdSession sd;
    if (!sd.ok()) return;

    if (!SD.exists("/custom_coins.txt")) return;

    File f = SD.open("/custom_coins.txt", FILE_READ);
    if (!f) return;

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
    Serial.printf("Loaded %d custom coins from SD\n", g_customCoinCount);
}

// ── Init ──────────────────────────────────────────────────────────────────
#define CRYPTO_CFG_VERSION 2  // bump to force NVS wipe on schema changes

void cryptoInit() {
    // Created before anything can read the cache; the fetch worker starts later in setup().
    if (!s_dataMutex) s_dataMutex = xSemaphoreCreateMutex();

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

    // Load API key from SD (if present) — overrides compile-time define
    {
        SdSession sd;
        if (sd.ok() && SD.exists("/apikey.txt")) {
            File f = SD.open("/apikey.txt", FILE_READ);
            if (f) {
                String key = f.readStringUntil('\n');
                key.trim();
                if (key.length() > 0) {
                    strlcpy(s_apiKey, key.c_str(), sizeof(s_apiKey));
                    Serial.println("CoinGecko API key loaded from SD");
                }
                f.close();
            }
        }
    }
    cryptoLoadCustomCoins();        // reads /custom_coins.txt from SD
    cryptoPagesLoad();              // NVS layout
    cryptoApplyPageConfigFromSD();  // /pages.txt overrides it, when the file has changed
    // Don't fetch here — caller decides when (after WiFi connects)
}
