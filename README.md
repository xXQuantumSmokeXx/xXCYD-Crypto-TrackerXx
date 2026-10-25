# xXCYD-Crypto-TrackerXx

Real-time cryptocurrency tracker for CYD devices — live prices, 24H/7D/1M/3M charts, Fear & Greed index, multi-page coin layout, offline SD caching, and intelligent power management.

[![Support on Patreon](https://img.shields.io/badge/Support-Patreon-orange)](https://www.patreon.com/c/xXQuantumSmokeXx)

## Screenshots

| Page 1 | Page 2 | Page 3 |
|--------|--------|--------|
| ![Page 1](ScreenShots/Page1.png) | ![Page 2](ScreenShots/Page2.png) | ![Page 3](ScreenShots/Page3.png) |

| Page 4 | Coin Picker | Settings |
|--------|-------------|----------|
| ![Page 4](ScreenShots/Page4.png) | ![Coin Picker](ScreenShots/CoinPicker.png) | ![Settings](ScreenShots/Settings.png) |

## Features

**Crypto Pages (1-5):**
- Up to 5 configurable pages, each holding 1-4 coins
- **1-coin view:** large price, 24H/7D change percentages with themed labels, HIGH/LOW values, and a full-width chart — tap to cycle 24H → 7D → 1M → 3M
- **2-coin view:** side-by-side layout with sparklines and percentage changes
- **3-4 coin view:** compact rows with mini sparklines
- Comma-formatted USD prices across all layouts
- Auto-rotate between enabled pages (configurable interval in Settings)
- Disabled pages auto-skipped in navigation

**Live Data:**
- CoinGecko API integration — fetches prices, 24H/7D changes, high/low, and the 7-day sparkline
- 1M and 3M charts from `/coins/{id}/market_chart`, streamed rather than buffered (those responses are 74 KB and 223 KB) and cached per coin+period on SD — 1 hour for 1M, 6 hours for 3M
- Fear & Greed Index displayed in status bar
- SD card cache of the last successful fetch, used whenever the network is unavailable — survives reboots and long outages (marked `CACHED` with its timestamp)
- Background fetch every 60 seconds via FreeRTOS worker on Core 0
- Retry logic on boot — two fetch attempts, then the SD cache, so a dead network costs a short splash rather than a minute
- LIVE / CACHED indicator with last sync timestamp

**Coin Picker:**
- 50 built-in coins (see list below)
- Custom coin support via `custom_coins.txt` on SD card — add any CoinGecko-listed token
- Page layout via `pages.txt` on SD card — pin coins to pages without the picker
- Page management: enable/disable pages, clear pages, move coins between pages
- Visual indicators for coins assigned to other pages
- Gapless page tabs for easy touch navigation

**Power Management:**
- Auto-brightness via LDR light sensor
- Sleep timer — display off after configurable idle time, tap to wake
- Schedule-based sleep — set sleep/wake hours, display auto-off during window
- E-Ink mode — white background for outdoor/glare readability

**Display & UI:**
- 9 theme accent colors (CYAN, GREEN, RED, ORANGE, YELLOW, GRAY, PURPLE, PINK, WHITE) saved to NVS
- Themed top bar with live clock and screen label
- Themed bottom bar with date and battery percentage
- Status bar with data source, Fear & Greed, and LIVE/CACHED indicator
- Abstract faceted crypto-core splash logo
- Touch calibration with rotation cycling (2USB)
- Display rotation for the 2USB boards (MADCTL), persisted to NVS

**Screenshot Capture:**
- Serial screenshot via RGB332 protocol — send `S` at 115200 baud
- Compatible with included `screenshot.py` — auto-detects COM port, saves BMP to `ScreenShots/`
- Interactive mode (double-click) or command line: `python screenshot.py COM10 0`

## Default Coins (Top 50)

BTC, ETH, USDT, BNB, SOL, XRP, DOGE, ADA, TRX, AVAX, SHIB, **SUI**, WBTC, DOT, LINK, BCH, NEAR, UNI, LTC, MATIC, ICP, XLM, XMR, ATOM, CRO, FIL, VET, ARB, OP, HBAR, GRT, INJ, RNDR, **STRK**, IMX, MKR, RUNE, ALGO, AAVE, **KAS**, QNT, FLOW, EGLD, FTM, **COQ**, **ZEC**, **JUP**, **BONK**, **TIA**, XTZ

## Custom Coins

Drop `custom_coins.txt` on the SD card root with one CoinGecko ID per line:

```
# Format: coin_id or coin_id,SYMBOL
pepe,PEPE
fetch-ai,FET
```

Up to 10 custom coins. Appear below the 50 built-in coins in the Coin Picker. [Find CoinGecko IDs](https://www.coingecko.com/en/coins/list).

## Page Layout from SD

Drop `pages.txt` on the SD card root to set the pages and their coins without using the
picker. Five pages, four coins each; a token is a CoinGecko ID or a symbol, case-insensitive:

```
# P<n>: coin, coin, coin, coin
P1: BTC, AVAX, SUI, ZEC
P2: ICP, STRK, JUP, ADA
```

Pages you don't list are cleared and disabled. The file is authoritative **when its
contents change** — the hash is kept in NVS, so layout edits made in the picker survive a
reboot, and editing the file re-applies it on the next boot. Unknown coins and over-full
pages are reported on serial and skipped; a file with nothing usable in it changes nothing.

## Setup

| Board | Firmware File |
|-------|--------------|
| **ESP32-32E** (1-USB) | `CYD-Crypto-Tracker-1usb.bin` |
| **2USB** (all variants) | `CYD-Crypto-Tracker-2usb.bin` |

These are **application images**, not merged flash images — they hold the app only, with no
bootloader and no partition table. Where they go depends on the partition table already on the
board, so "flash at 0x0" is not valid for them.

**Via M5Launcher (recommended):** copy the `.bin` file onto a micro SD card (FAT32), insert it
into your CYD, launch [M5Launcher](https://github.com/bmorcelli/M5Launcher), select the
firmware, and flash. The launcher writes to an app slot for you.

**Via esptool:** write to the application partition of *your* table. On the default ESP32
layout that offset is `0x10000`:

```bash
esptool.py --chip esp32 --port COM10 write_flash 0x10000 CYD-Crypto-Tracker-2usb.bin
```

On a board running a launcher with its own partition table (M5Launcher's slots sit at
`0x10000`, `0x180000` and `0x2D0000`), use the offset of the slot you mean to replace.

**SD card files (place on root):**
- `wifi.txt` — WiFi credentials: line 1 = SSID, line 2 = password
- `custom_coins.txt` — optional custom CoinGecko IDs (one per line)
- `pages.txt` — optional page layout (see above)
- `apikey.txt` — optional CoinGecko API key (single line, avoids anonymous rate limits)
- `cache/btc.bin` — auto-generated price cache (binary; older `btc.json` is ignored)
- `batt.csv` — auto-generated battery log, one line a minute (rotates to `batt.old.csv` at
  64 KB). Columns: `epoch,uptime_s,pin_mv,cell_raw_v,cell_v,pct,wifi`. Exists so a discharge
  that happens away from a USB cable can still be read afterwards.

## Serial Commands

- `0`–`5` — switch screens (0–4 = crypto pages, 5 = settings)
- `C` — open the coin picker
- `F` — force refresh crypto data
- `M` — cycle display rotation (2USB)
- `T` — cycle touch rotation (2USB)
- `R` — ready check
- `V` — print the build stamp (compile date and time)
- `S` — screenshot capture (RGB332)

## Build

```bash
# 1-USB variant
pio run -e cyd_crypto

# 2-USB variant
pio run -e cyd_crypto_2usb
```

Requires PlatformIO with `espressif32@6.3.0` platform. The only build flag difference between variants is `-DCYD_USB_VERSION=1` vs `-DCYD_USB_VERSION=2`.

There are no releases or version numbers (removed 2026-10-25). A build is identified by its
commit and the compile date/time printed at boot; `V` prints the same stamp.

## Project Structure

```
src/
  main.cpp                       — app loop, WiFi, fetch worker, splash art
  config/
    config.h                     — pins, geometry, constants
    secrets.h                    — API keys (gitignored)
    nvs_config.h/.cpp            — Preferences/NVS wrapper
    spi_bus.h/.cpp               — shared VSPI mutex, SD session + touch re-init
  modules/
    wifi_config.h/.cpp           — SD-based WiFi bootstrapping
    time_sync.h/.cpp             — NTP time sync
    brightness.h/.cpp            — LDR auto-brightness, battery %
    crypto.h/.cpp                — CoinGecko API, Fear & Greed, data model
    chart.h/.cpp                 — 1M/3M market_chart: stream parser, per-period cache, prefetch
  ui/
    theme.h                      — colors, fonts, invert mode
    theme_color.h/.cpp           — runtime theme switching (9 colors)
    widgets.h/.cpp               — topbar, bottombar, statusbar, pct chip
    screens/
      screen_crypto.h/.cpp       — main coin page display (1-4 coins)
      screen_detail.h/.cpp       — single-coin detail + graph tabs
      screen_settings.h/.cpp     — settings (sleep, rotate, theme, coins)
      screen_coinpicker.h/.cpp   — top-50 coin picker + custom coins
  touch/
    touch.h/.cpp                 — XPT2046 resistive touch (swipe + tap)
screenshot.py                    — PC-side screenshot capture script
sd_card/
    wifi.txt                     — example WiFi config
    custom_coins.txt             — example custom coins
    pages.txt                    — example page layout
```

## Changelog

> Versioning was removed on 2026-10-25. The entries below are historical; changes after that
> date are recorded by date, not by version number.

### v1.1.12
- **Fixed:** the SD card only worked before the touchscreen was initialised. SD and touch
  share VSPI on *different* pins, and `SPIClass::begin()` does nothing once the bus has been
  started — so after `touchInit()` re-muxed the bus, every later `SD.begin()` kept talking to
  the touch pins and failed. The price cache, the chart cache and `/batt.csv` never reached
  the card; only the boot reads (wifi.txt, apikey.txt, pages.txt), which happen before touch
  init, worked. Both now share one `SPIClass` that ends and re-attaches its pins per device.
- **Changed:** the boot splash no longer claims "WiFi failed!". It waits 20 s instead of 12,
  and if the association is slow it says "WiFi not up yet - retrying..." — the loop keeps
  trying and usually gets there seconds later.

### v1.1.11
- **Changed:** sparkline batches cover five coins instead of four, and the standalone price
  request is skipped when every coin already has one (the batches carry current prices
  anyway). A 19-coin sparkline refresh is now four calls instead of six, which fits under the
  anonymous ~5/min ceiling — so every coin gets a sparkline, including the last batch that
  used to 429.

### v1.1.10
- **Diagnostics:** the SD price cache no longer fails silently. Mount, mkdir, open and write
  failures print to serial, along with `cache: created /cache/btc.bin (…)` the first time the
  file is written. It had been failing with no output at all, which is why an offline boot
  always looked like "no data".

### v1.1.9
- **Fixed:** a key CoinGecko does not accept no longer blanks the screen. A rejected key
  (401/403) is logged, dropped for the session, and the request is retried anonymously; a
  key that does not start with `CG-` is called out at boot instead of showing up as a wall of
  401s.

### v1.1.8
- **Fixed:** a boot that missed WiFi stayed offline for the whole session. `setup()` waited
  12 s for the association once, and nothing ever re-checked `WiFi.status()`, so a slow
  router left `s_wifiOk` false with no retry — "Loading data..." until a reboot. The link is
  now checked every 10 s, the driver is nudged every 30 s while down, SNTP starts when it
  comes up, and a fetch runs immediately on reconnect.

### v1.1.7
- **Fixed:** the device could sit on "Loading data..." forever. Sparklines were fetched as
  one ~80 KB response, and on the device `getString()` returned that body truncated, so every
  parse failed with `IncompleteInput`. The SD cache had been hiding it — pull the card, and
  there was nothing left to draw. Prices now come in one ~17 KB request (the size the minute
  refresh has always used) and sparklines go four coins at a time.

### v1.1.6
- **New:** `/batt.csv` on the SD card, one line a minute — raw pin mV, cell voltage before
  and after sag compensation, percentage and WiFi state. A discharge only happens off USB,
  where there is no serial to watch, so this is how the battery curve gets measured.

### v1.1.5
- **New:** multi-period charts — 24H, 7D, 1M and 3M. A single-coin page cycles all four on
  tap; a multi-coin page opens that coin's detail chart on tap. 1M/3M come from
  `/coins/{id}/market_chart`, fetched on the worker and cached per coin+period on the SD card
  (1 hour for 1M, 6 hours for 3M). Both long periods are prefetched when a coin page opens
  and each keeps its own memory slot, so tapping between 1M and 3M is instant.
- **Fixed:** tapping a multi-coin page did nothing, which also left the detail screen
  unreachable — nothing ever set the tapped coin.
- **Fixed:** the 24H and 7D tabs were swapped — "24H" drew the whole 7-day series and "7D"
  drew the last 24 points.
- **Note:** the 1M and 3M responses are 74 KB and 223 KB, so they are parsed as a stream and
  never held in memory. HTTP/1.0 is used deliberately: CoinGecko only chunk-encodes HTTP/1.1
  replies, and `getStream()` returns the raw socket.

### v1.1.4
- **Changed:** sparklines are fetched hourly instead of every minute. A 20-coin refresh drops
  from ~81 KB to ~17 KB (measured); prices still update every 60 s and the existing chart
  points are kept between sparkline fetches.
- **Changed:** battery % settles faster (EMA alpha 0.12 → 0.25) and no longer flickers —
  a ±2% display hysteresis holds the shown value until the reading is two points away.
- **Changed:** the battery is sampled on a 10-second tick instead of inside every status-bar
  redraw. That read is 50 ADC samples, about 50 ms, paid on every full refresh before.
- **Deliberately not changed:** the sag offset stays at 30 mV. A larger constant would bias
  the gauge high when idle; sag that only appears under load needs a quiet-radio sample.

### v1.1.3
- **Fixed:** the UI could draw a coin while the fetch worker was rewriting it on the other
  core — a torn price or a half-copied name. Reads now copy the coin under a lock.
- **Fixed:** a custom coin assigned to a page was looked up with an out-of-bounds index
  (`g_top50[idx]` with `idx >= 50`), so it showed the wrong data or none.
- **Fixed:** the SD cache is actually usable offline. It refused anything older than five
  minutes, and any failed forced fetch (startup retries, the `F` key) zeroed the cache
  stamp — so an offline device showed nothing at all. A failed fetch now falls through to
  the cache at any age, marked `CACHED` with its timestamp.
- **Changed:** startup tries two fetches instead of four, so a dead network costs a much
  shorter splash before cached prices appear.

### v1.1.2
- **New:** `V` serial command prints the firmware version, and the same line is logged at
  boot — after a reflash, "which build is this?" no longer needs a commit hash.

### v1.1.1
- **New:** `pages.txt` on the SD card sets the coin layout at boot
  (`P1: BTC, AVAX, SUI, ZEC`). Applied when the file's contents change, so layout edits made
  in the picker survive a reboot.
- **Fixed:** touch could misread while the SD card was being accessed — both sit on VSPI and
  mounting the card re-muxes the bus away from the touch controller. One mutex now covers
  both, and each SD session re-inits touch before releasing.
- **Fixed:** a failed `/coins/markets` parse now falls through to the cache instead of
  leaving the screen without data, and an empty result is never written over a good cache.
- **Changed:** the cache is binary — 15.5 KB for 20 coins instead of ~79 KB of JSON, with no
  parser needed to read it back. The old `cache/btc.json` is ignored.
- **Changed:** the parsed document is filtered to the nine fields the UI reads (CoinGecko
  sends 28).

See the [releases page](https://github.com/xXQuantumSmokeXx/xXCYD-Crypto-TrackerXx/releases)
for earlier versions.

## Credits

**Check out my other projects:**
- [xXCYD-PokerXx](https://github.com/xXQuantumSmokeXx/xXCYD-PokerXx) — classic Joker Poker + Texas Hold'em for CYD
- [xXCYD-Weather-StationXx](https://github.com/xXQuantumSmokeXx/xXCYD-Weather-StationXx) — tactical weather & monitoring station

Built by xXQuantum-SmokeXx, with development assistance from Codex & Claude Code.
