// geo_tz_client.h — optional automatic timezone detection via IP geolocation.
//
// Answer to the original question: we *can* auto-detect a timezone, but
// only once WiFi is up. Strategy:
//
//   1. After a successful STA connect, hit http://ip-api.com/json/?fields=timezone
//      (free, no key, IP-based) and parse the IANA tz name out of the
//      response ("timezone":"America/New_York").
//   2. Map the IANA name to one of the POSIX strings in tz_data.cpp.
//
// The detected value is suggested to the user — it is not applied silently —
// so they still get a chance to override it via the captive portal on
// first boot. On subsequent boots the stored value wins and the geo call
// is skipped.
//
// This file intentionally depends only on esp_http_client so that the
// detection can run before SNTP is configured.

#pragma once

#include <cstddef>

// Fetch a POSIX tz string derived from the current public IP and copy it
// into `out` (nul-terminated, truncated to out_size). Returns true on
// success, false if the request failed, the response couldn't be parsed,
// or the IANA name didn't match any of our known POSIX strings.
bool geoFetchPosixTz(char* out, size_t out_size);
