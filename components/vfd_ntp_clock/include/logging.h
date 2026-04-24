// logging.h — thin wrapper over esp_log that respects a runtime AppLogLevel.
//
// The original ESP32NTPClock uses Arduino Serial.printf. We route everything
// through ESP_LOG so it plays nicely with idf.py monitor's color codes, and
// we gate on the user-configured g_appLogLevel so the captive portal's
// "log level" dropdown remains meaningful.

#pragma once

#include "enc_types.h"
#include "esp_log.h"

// Set in main once preferences are loaded; defaults to INFO until then.
extern AppLogLevel g_appLogLevel;

#define LOG_TAG "VFDW"

// Emit if the runtime level is at or above the line's level. ESP_LOG macros
// do their own compile-time gating too; this is the *runtime* gate that
// lets users dial verbosity from the web portal without reflashing.
#define LOGMSG(level, fmt, ...)                                              \
    do {                                                                     \
        if ((level) <= g_appLogLevel) {                                      \
            if ((level) == APP_LOG_ERROR)      ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__); \
            else if ((level) == APP_LOG_INFO)  ESP_LOGI(LOG_TAG, fmt, ##__VA_ARGS__); \
            else                               ESP_LOGD(LOG_TAG, fmt, ##__VA_ARGS__); \
        }                                                                    \
    } while (0)

#define LOGERR(fmt, ...) LOGMSG(APP_LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOGINF(fmt, ...) LOGMSG(APP_LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOGDBG(fmt, ...) LOGMSG(APP_LOG_DEBUG, fmt, ##__VA_ARGS__)
