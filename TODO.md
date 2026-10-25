# TODO

## NEXT_RELEASE (v1.2.0)

- [ ] **Battery % — what's left** — Layer 1 shipped (alpha, hysteresis, off-draw sampling).
      Still open: sag compensation (needs a quiet-radio sample, i.e. Layer 3, not a bigger
      constant), the sharper curve (Layer 2), and whether both 1-USB boards genuinely sit
      that low or their divider constant differs — compare a resting cell against a meter.
- [x] **Sparkline split** (2026-10-25) — prices every minute (17,035 bytes for 20 coins),
      sparklines hourly (80,831). See "Landed".
- [x] **Multi-Period Charts — 24H / 7D / 1M / 3M** (2026-10-25) — see "Landed".
- [x] **Charts — 3M is the longest period** (decided 2026-10-25). 6M, 1Y and ALL are not
      planned; 3M covers what the Commander wants and the tabs stay at four. For the record
      if that ever changes: ALL is 5000+ points for BTC and would need a ~24 KB buffer or
      decimation while parsing, and on 2026-10-25 the demo API returned 401 for `days=180`
      and `days=730` while `30` and `365` worked — probably quota, but worth re-checking
      before assuming a range is available.
- [x] **Shared VSPI bus mutex + heap-light cache** (2026-10-25) — see "Landed" below
- [ ] **TBD**

## Landed (2026-10-25)

**Shared VSPI bus** — `src/config/spi_bus.h`. The SD card and the touch controller are both
on VSPI, and `SD.begin()` re-muxes the bus to the SD pins; the touch driver's `SPIClass`
has no way to know, so a touch read during an SD session returns garbage. One mutex now
covers every SD session and every touch read, and because `SD.end()` leaves the bus on the
SD pins, each session re-inits touch on the way out (`SdSession` → `spiBusRestore()`).

**Cache is binary** — 776-byte `CoinData` records instead of raw JSON. ~79 KB of JSON per
fetch (measured against the live API) becomes 15.5 KB; reading it back needs no parser and
no `String`. A `static_assert` pins the record size, so a struct change cannot silently
corrupt old files. The old `/cache/btc.json` is ignored, not migrated.

**Filtered parse** — the `/coins/markets` document keeps the nine fields the UI reads
(CoinGecko sends 28). Parse failures now return false so the cache is used, and an empty
result is never written over a good cache.

**Cross-core cache access** — the fetch worker (core 0) rewrote coins field by field while
the UI (core 1) drew from them, with no lock anywhere: a torn price, a half-copied name, a
sparkline filling mid-draw. Every access now takes one mutex, and the getter COPIES
(`cryptoCopyCoinData`) instead of returning a pointer that outlives the lock. Reading is
`CACHED`/`LIVE`-labelled as before.

**Custom coins were reading out of bounds** — `screen_crypto` resolved the page's coin with
`g_top50[idx].id`, but a custom coin has an index of 50-59, past the end of the array. Any
custom coin assigned to a page looked up garbage. Now resolved through `coinGetDef`, and
fixed in the same pass as the lock because it is the same three lines.

**Device check (2026-10-25)** — v1.1.3 on a 2-USB CYD: boot ~13 s, touch registers,
`pages.txt` applied from SD, prices and sparklines accurate. v1.1.4 on a 1-USB CYD: boot
~4 s, `pages.txt` applied, and the battery gauge held at 4% without flicker — the hysteresis
change confirmed, though a full charge/discharge cycle is still needed to judge the EMA.

v1.1.5 on a 2-USB CYD: page 3 with a single ETH works through all four chart periods
(24H/7D/1M/3M) on tap, and a tap on a multi-coin page opens that coin's detail chart, which
now paints the loaded chart without needing a second tap — `SCR_DETAIL` is 7 while
`SCR_COUNT` is 6, so the old `s_screen < SCR_COUNT` redraw guard excluded it. Two
findings from the serial capture that are worth remembering: a 3M fetch only starts after 1M
finishes on the worker, so anything that resets the device in that window leaves 3M
unloaded (the SD cache makes the next attempt instant); and the app's partition slot
(`cydcry`, 1,088 KB) is 5% free against a 1,058 KB binary.

Not yet exercised on hardware: the offline cache (WiFi off, >5 min, reboot), `F` with no
network, and a custom coin on a page.

**Offline cache actually works** — it did not. `cryptoFetch(force)` zeroed `btc_cached_at`
before trying the network, so a failed forced fetch (startup's retries, the `F` key)
invalidated the cache it would have fallen back to; and the cache refused anything older
than a 5-minute TTL. Between them, an offline CYD showed nothing. The TTL and the `force`
flag are gone: the network is always attempted when connected, and a failure falls through
to the cache at any age, labelled `CACHED` with its timestamp. Startup also drops from four
attempts to two, so a dead network costs a shorter splash.

**Page layout from SD** — `/pages.txt` (`P1: BTC, AVAX, SUI, ZEC`), so the layout can be set
without the picker, the way `wifi.txt` and `apikey.txt` already work. Applied when the file's
hash changes: picker edits survive a reboot, editing the file re-applies on the next boot.

**Multi-period charts (24H/7D/1M/3M)** — `src/modules/chart.*`. 24H/7D stay on the cached
7d sparkline; 1M (30 days) and 3M (90) come from `/coins/{id}/market_chart`, handled on the
fetch worker so no HTTP ever runs on the UI core (that was July's watchdog reboot). The
responses are 74 KB and 223 KB, so the body is parsed as a stream and never held: HTTP/1.0
avoids chunked framing on `getStream()`, and a small state machine pulls the prices out of
the `"prices"` array as it arrives, downsampling to 168 points. Cached per coin+period as
binary on SD (1 h / 6 h). The parser was checked against the live API at 1, 30 and 90 days
and returned exactly the same values as a reference JSON parse.

Fixed in passing: the 24H and 7D tabs were swapped — "24H" drew the full 7-day series and
"7D" drew the last 24 points.

**Sparklines split from prices** — a `/coins/markets` call with `sparkline=true` is 80,831
bytes for 20 coins against 17,035 without (79% sparkline, measured). Sparklines are hourly
data, so they are now fetched only when `spark_fetched_at` is over an hour old or when some
assigned coin has none (a new coin, or an older cache); price-only refreshes leave the
existing points untouched. The `String` and document behind a normal minute are ~64 KB
smaller.

**Finding for the chart work** — CoinGecko answers `Transfer-Encoding: chunked` with no
`Content-Length`, and only `HTTPClient`'s own body readers strip the chunk framing.
`http.getStream()` hands back the raw socket, so a stream parser would be fed chunk-size
lines. Anything that parses incrementally has to de-chunk itself or go through
`getString()`.

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
1. **Streaming JSON parser** — parse `/market_chart` response incrementally (character-by-character) rather than buffering the full string + building a DOM. Already prototyped; the manual `strstr`/`strchr` scanner works but could be hardened against API format changes. **Caveat added 2026-10-25:** the response is `Transfer-Encoding: chunked`, and the framing is only stripped inside `HTTPClient`'s body readers — `getStream()` is the raw socket. An incremental parser must de-chunk first, or read from `getString()` and accept the copy. A scanner fed chunk sizes would fail on every fetch while looking like bad data.
2. **Fully async from the start** — no synchronous HTTP anywhere in the UI thread. Queue chart requests via a lock-free ring buffer; worker processes them one at a time; completion flag triggers redraw.
3. **SD card mutex — DONE 2026-10-25** (`src/config/spi_bus.h`). The old note guessed that the `SD.begin()`/`SD.end()` pattern might already handle this; it does not. `SD.begin()` re-muxes VSPI to the SD pins, so the real bug was never interleaving but the bus being left pointed at the wrong device afterwards — a mutex alone would not have fixed it. One mutex now covers both, and every SD session re-inits touch before releasing. Chart work can lean on it.
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

**Layer 1 — applied 2026-10-25, except the sag change:**
- [x] Raise discharge EMA alpha 0.12 → 0.25
- [x] Add ±2% display hysteresis (full and empty always pass straight through)
- [x] Move sampling off the draw path — `batteryPct()` ran 50 ADC samples (~50 ms) inside
      every status-bar redraw; it now samples on a 10 s tick in `loop()` and the draw path
      reads the cached value.
- [ ] **Deliberately NOT done: BATT_SAG_OFFSET_MV 30 → 100 mV.** The offset is a constant
      added to every reading, so +200 mV at the battery would pin the gauge near 100% at
      rest (4.08 V idle + 0.2 V = 4.28 V, which the curve clamps). Sag that only exists
      under load needs a sample taken with the radio quiet — that is Layer 3, not a bigger
      constant.

**Proposed fix (Layer 2 — smoother mapping):**
- Replace 8-segment piecewise linear with smooth polynomial fit of real LiPo discharge curve

**Proposed fix (Layer 3 — smarter sampling):**
- Median filter instead of mean (throws out WiFi TX spike outliers)
- Or sample during WiFi-idle windows

**Note:** This code was ported from CYD-Weather; same fixes needed there. See CYD-Weather/TODO.txt.
