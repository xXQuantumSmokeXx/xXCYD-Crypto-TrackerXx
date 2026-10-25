#include "spi_bus.h"
#include <SPI.h>

SemaphoreHandle_t g_spiMutex = nullptr;
SPIClass          g_vspiBus(VSPI);

static void (*s_restore)() = nullptr;

void spiBusInit() {
    if (g_spiMutex) return;
    g_spiMutex = xSemaphoreCreateMutex();
    if (!g_spiMutex) Serial.println("[spi] mutex creation FAILED");
}

void spiBusSetRestore(void (*fn)()) {
    s_restore = fn;
}

void spiBusRestore() {
    if (s_restore) s_restore();
}
