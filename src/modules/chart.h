#pragma once

#include <cstdint>

// ── Multi-period charts ───────────────────────────────────────────────────
// 24H and 7D are already served from the cached 7d sparkline (hourly points). 1M and 3M
// need CoinGecko's /coins/{id}/market_chart, which is a much bigger response: 74 KB for 30
// days and 223 KB for 90 (measured 2026-10-25). Neither fits in a String on a fragmented
// heap, so the body is parsed as a stream and never held: see chart.cpp.
//
// The UI never fetches. It asks for a period and draws whatever is loaded; the fetch worker
// services the request on core 0, which is what keeps the watchdog happy (the July attempt
// blocked the tap handler and rebooted).
enum ChartPeriod : uint8_t {
    CHART_24H = 0,
    CHART_7D  = 1,
    CHART_1M  = 2,
    CHART_3M  = 3,
    CHART_PERIOD_COUNT
};

const char *chartPeriodLabel(int period);   // "24H", "7D", "1M", "3M"
int  chartPeriodDays(int period);           // 30 / 90; 0 when the sparkline already serves it

using ChartWakeFn = void (*)();
void chartSetWakeHook(ChartWakeFn fn);      // main.cpp registers "notify the fetch worker"

void chartInit();

// UI side. Never blocks and never touches the network; safe to call from a draw.
void chartRequest(const char *coinId, int period);

// Fetch-worker side. Returns true if it serviced a request (so callers can redraw).
bool chartServicePending();

// Points for coin+period, downsampled to at most maxPoints. Returns the count, or 0 when
// that coin+period is not loaded yet.
int chartCopyPoints(const char *coinId, int period, float *out, int maxPoints);

// True once after a chart finishes loading, so the UI can redraw. The fetch happens on the
// worker; without this the detail screen would sit on "Loading chart..." until something
// else triggered a redraw.
bool chartConsumeUpdated();
