#pragma once
#include <cstdint>
#include <cstddef>

void timeSyncInitTZ(const char *tz);   // IANA timezone string, handles DST
bool timeIsValid();
void timeGetShort(char *buf);         // "2:05 PM"
void timeGetDateLong(char *buf, size_t len);     // "Tuesday, May 20, 2026"
