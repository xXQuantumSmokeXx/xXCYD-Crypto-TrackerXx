#pragma once

#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

// ── The VSPI bus is shared ────────────────────────────────────────────────
// On this board the SD card and the touch controller are both on VSPI, but on different
// pins: the card on the default 18/19/23, the touch remapped to 25/39/32. SPIClass::begin()
// returns early once the bus is started, so the instance that began it last owns the pin mux.
// That is why every SD mount after touchInit() failed - the SD library's own spi.begin() was
// a no-op and the card was left talking to the touch pins (found 2026-10-25). One shared
// SPIClass now serves both: each side calls end() (which detaches the pins it attached last)
// and begins it with its own pins before use.
//
// Only one of them may be mid-transaction at a time, too: the cache write runs on the fetch
// worker (core 0) while the UI polls touch on core 1, so every SD block and every touch read
// takes g_spiMutex.
//
// Both mutexes here are created in spiBusInit(), not in a static initialiser: FreeRTOS
// objects belong after the scheduler's heap exists, and a failed creation should be
// visible rather than a silent NULL.
extern SemaphoreHandle_t g_spiMutex;
extern SPIClass          g_vspiBus;   // VSPI, shared by the SD card and the touch controller

void spiBusInit();                    // once, at the top of setup()
void spiBusSetRestore(void (*fn)());  // touch registers its re-init here
void spiBusRestore();                 // called by SdSession after SD.end()

// Hold the bus for the life of the object. Scope it tightly - never across a network call,
// or the UI stops seeing touches for the duration.
class SpiLock {
public:
    SpiLock()  { if (g_spiMutex) xSemaphoreTake(g_spiMutex, portMAX_DELAY); }
    ~SpiLock() { if (g_spiMutex) xSemaphoreGive(g_spiMutex); }
    SpiLock(const SpiLock &) = delete;
    SpiLock &operator=(const SpiLock &) = delete;
};

// Mount -> use -> unmount, bus held throughout and put back on every exit path.
class SdSession {
public:
    explicit SdSession(uint8_t csPin = SD_CS) : _lock(), _mounted(false) {
        g_vspiBus.end();                                    // detach the touch pins
        g_vspiBus.begin(SD_SCLK, SD_MISO, SD_MOSI, csPin);  // attach the card's
        _mounted = SD.begin(csPin, g_vspiBus);
    }

    ~SdSession() {
        if (_mounted) SD.end();
        spiBusRestore();
    }

    bool ok() const { return _mounted; }

    SdSession(const SdSession &) = delete;
    SdSession &operator=(const SdSession &) = delete;

private:
    SpiLock _lock;   // declared first: the lock must be held before SD.begin() runs
    bool    _mounted;
};
