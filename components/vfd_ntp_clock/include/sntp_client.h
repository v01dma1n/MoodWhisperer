// sntp_client.h — very small wrapper over IDF's esp_sntp.

#pragma once

#include <ctime>

// Set once time has ever been received from the NTP server.
extern volatile bool timeAvail;

// Configure and start the SNTP client with the given POSIX timezone
// string (e.g. "EST5EDT,M3.2.0/2,M11.1.0/2"). Call after WiFi is up.
// Safe to call multiple times — the underlying library is idempotent.
void setupSntp(const char* tz);

// Non-blocking poll; returns 1 once we have a valid timestamp, 0 while
// still syncing, -1 after `intervalMillis` has elapsed without success.
int loopSntpGetTime(unsigned intervalMillis);
