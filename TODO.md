# TODO

## Multi-Period Charts (24H / 7D / 1M / 3M / 6M / 1Y / ALL)

Tap-to-cycle through chart periods on single-coin view and detail screen.

**Attempted:** 2026-07-06 — full implementation across crypto.cpp, screen_crypto.cpp, screen_detail.cpp, and main.cpp. Reverted due to stability issues.

**What worked:**
- `ChartPeriod` enum + `PeriodDef` metadata table (period label, CoinGecko `days` param)
- CoinGecko `/coins/{id}/market_chart` endpoint returns correct data for all periods
- Manual JSON string scanner (zero heap, `strstr` + `strchr` + `atof`) parsed all response sizes reliably — 24 points (24H) through 5000+ (ALL/BTC)
- SD card caching per coin+period with TTLs graduated by period (1hr for 1M → 24hr for ALL)
- On-the-fly downsampling to 168 display points regardless of source granularity
- UI: directional tap zones (left = prev period, right = next) with `< PERIOD >` label

**What broke:**
- **Task watchdog reboots** — synchronous HTTP in tap handler blocks Core 1. CoinGecko `/market_chart` responses are too slow for the 5-second TWDT threshold, especially the hourly-granularity periods (1M = 720pts, 3M = 2160pts).
- **`DynamicJsonDocument(98304)` heap exhaustion** — 96KB contiguous allocation fails on fragmented ESP32 heap. Fixed by switching to manual string scanning, but the WDT issue remained.
- **Core 0 worker approach** — moved HTTP to the existing fetch worker task (Core 0, WDT-unsubscribed). Still experienced instability, likely from SPI bus contention between the SD card (VSPI) and touch controller (VSPI remapped) when accessed from different cores simultaneously.

**Lessons for next attempt:**
1. **Streaming JSON parser** — parse `/market_chart` response incrementally (character-by-character) rather than buffering the full string + building a DOM. Already prototyped; the manual `strstr`/`strchr` scanner works but could be hardened against API format changes.
2. **Fully async from the start** — no synchronous HTTP anywhere in the UI thread. Queue chart requests via a lock-free ring buffer; worker processes them one at a time; completion flag triggers redraw.
3. **SD card mutex** — if the worker (Core 0) accesses the SD card while the main loop (Core 1) is using the touch controller on the same SPI bus, add a semaphore around VSPI transactions. The `SD.begin()`/`SD.end()` pattern in the existing codebase may already handle this, but the chart fetches increase SD access frequency dramatically.
4. **Pre-warm the cache** — when the user enters a 1-coin or detail view, pre-fetch the 1M and 3M periods in the background before they even tap. Those are the heavy ones (hourly data). 6M/1Y/ALL are daily data and much lighter.
5. **Smaller pages / fewer coins** during chart viewing — the 168-point `s_activeSpark` buffer per coin is fine, but consider whether the display can show a meaningful chart with fewer points (e.g., 84) to reduce memory pressure.

**Reverted:** All chart period code removed in commit range leading up to v1.1.0. Original 24H/7D toggle (using CoinData `spark7d` from the main `/coins/markets` endpoint) remains and is stable.
