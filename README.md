# xXCYD-Crypto-TrackerXx

Real-time cryptocurrency tracker for CYD devices — live prices, 24H/7D sparkline charts, Fear & Greed index, multi-page coin layout, and intelligent power management.

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
- **1-coin view:** large price, 24H/7D change percentages with themed labels, HIGH/LOW values, inline sparkline graph with tap-to-switch between 24H and 7D timeframes
- **2-coin view:** side-by-side layout with sparklines and percentage changes
- **3-4 coin view:** compact rows with mini sparklines
- Comma-formatted USD prices across all layouts
- Auto-rotate between enabled pages (configurable interval in Settings)
- Disabled pages auto-skipped in navigation

**Live Data:**
- CoinGecko API integration — fetches prices, 24H/7D changes, high/low, and 7-day sparkline data
- Fear & Greed Index displayed in status bar
- SD card cache of the last successful fetch, used whenever the network is unavailable — survives reboots and long outages (marked `CACHED` with its timestamp)
- Background fetch every 60 seconds via FreeRTOS worker on Core 0
- Retry logic on boot — splash screen holds until data loads (up to 4 attempts)
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
- Display orientation auto-detection (2USB MADCTL)

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

**Direct flash:**
```bash
esptool.py --chip esp32 write_flash 0x0 CYD-Crypto-Tracker-1usb.bin
esptool.py --chip esp32 write_flash 0x0 CYD-Crypto-Tracker-2usb.bin
```

**Via M5Launcher:** copy the `.bin` file onto a micro SD card (FAT32), insert into your CYD, launch [M5Launcher](https://github.com/bmorcelli/M5Launcher), select the firmware, and flash.

**SD card files (place on root):**
- `wifi.txt` — WiFi credentials: line 1 = SSID, line 2 = password
- `custom_coins.txt` — optional custom CoinGecko IDs (one per line)
- `pages.txt` — optional page layout (see above)
- `apikey.txt` — optional CoinGecko API key (single line, avoids anonymous rate limits)
- `cache/btc.bin` — auto-generated price cache (binary; older `btc.json` is ignored)

## Serial Commands

- `0`–`5` — switch screens (0–4 = crypto pages, 5 = settings)
- `C` — open the coin picker
- `F` — force refresh crypto data
- `M` — cycle display rotation (2USB)
- `T` — cycle touch rotation (2USB)
- `R` — ready check
- `V` — print the firmware version
- `S` — screenshot capture (RGB332)

## Build

```bash
# 1-USB variant
pio run -e cyd_crypto

# 2-USB variant
pio run -e cyd_crypto_2usb
```

Requires PlatformIO with `espressif32@6.3.0` platform. The only build flag difference between variants is `-DCYD_USB_VERSION=1` vs `-DCYD_USB_VERSION=2`.

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
