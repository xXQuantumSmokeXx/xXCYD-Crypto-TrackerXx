#pragma once

#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

// ── The VSPI bus is shared ────────────────────────────────────────────────
// On this board the SD card and the touch controller are both on VSPI, and mounting the
// card re-muxes those pins to the SD ones. Two consequences, and both are why this file
// exists:
//
//   1. Only one of them may be mid-transaction at a time. The 5-minute cache write runs on
//      the fetch worker (core 0) while the UI polls touch on core 1, so every SD block and
//      every touch read takes g_spiMutex.
//   2. After SD.end() the bus is still pointed at the SD pins, and the touch driver has no
//      idea - its SPIClass object believes the bus is configured for it. A touch read in
//      that state returns garbage. So every SD session re-inits touch on its way out, via
//      spiBusRestore().
//
// Both mutexes here are created in spiBusInit(), not in a static initialiser: FreeRTOS
// objects belong after the scheduler's heap exists, and a failed creation should be
// visible rather than a silent NULL.
extern SemaphoreHandle_t g_spiMutex;

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
    explicit SdSession(uint8_t csPin = SD_CS) : _lock(), _mounted(SD.begin(csPin)) {}

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
