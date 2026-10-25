# TODO

## NEXT_RELEASE (v1.2.0)

- [ ] **Battery % — Layer 1 fix** (alpha 0.12→0.25, sag 30→100mV, ±2% hysteresis)
- [ ] **Multi-Period Charts — re-attempt** (async worker, streaming parser, SD mutex)
- [ ] **TBD**

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


## Battery % — discharge curve, sag compensation, EMA tuning

**Observed (2026-07-06):** Charged to 100%. Unplugged — percentage ticks down every ~5s screen refresh. Stabilizes around 87-88% with occasional ±1% bounce. Plug back in — jumps to 94% instantly, then climbs to 99% within 1-2 minutes.

**Root causes (diagnosed, not yet fixed):**

1. **EMA alpha too low for discharge** (0.12). Each refresh only moves 12% toward raw reading — takes ~8 refreshes (40s) to cover 63% of the gap. Filter slowly chases load sag, creating the illusion of rapid draining.

2. **Sag compensation too small** (BATT_SAG_OFFSET_MV = 30mV at pin = 60mV at battery). Real WiFi TX + display backlight sag can be 100-300mV at the battery. Compensated voltage is still ~0.2V low under load.

3. **No display hysteresis.** Every tiny EMA change redraws the percentage — looks jittery and unstable.

4. **Sharp piecewise curve boundaries.** 0.1V segments in the voltage→% mapping cause jumps when ADC noise crosses a boundary.

5. **Samples taken during WiFi TX bursts.** WiFi radio spikes current draw; sporadic low readings feed into the average.

**Proposed fix (Layer 1 — low risk, high impact):**
- Raise discharge EMA alpha 0.12 → 0.25
- Increase BATT_SAG_OFFSET_MV 30mV → 100mV at pin (200mV at battery)
- Add ±2% display hysteresis — only update on-screen if changed 2+ points

**Proposed fix (Layer 2 — smoother mapping):**
- Replace 8-segment piecewise linear with smooth polynomial fit of real LiPo discharge curve

**Proposed fix (Layer 3 — smarter sampling):**
- Median filter instead of mean (throws out WiFi TX spike outliers)
- Or sample during WiFi-idle windows

**Note:** This code was ported from CYD-Weather; same fixes needed there. See CYD-Weather/TODO.txt.
