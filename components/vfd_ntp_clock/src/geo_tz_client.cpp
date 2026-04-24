// geo_tz_client.cpp — HTTP call to ip-api.com + IANA→POSIX mapping.

#include "geo_tz_client.h"
#include "logging.h"

#include "esp_http_client.h"
#include "cJSON.h"

#include <cstring>
#include <cstdio>

// A small map from the most common IANA tz names to POSIX strings. We
// intentionally keep the table short — it only needs to cover the tz
// values ip-api.com is likely to return. Anything not listed falls
// back to "UTC0" with a warning, and the user picks manually.
//
// Extend freely; keep entries in lockstep with tz_data.cpp so the geo
// value actually appears in the dropdown preselected.
struct IanaPosix { const char* iana; const char* posix; };

static const IanaPosix s_ianaMap[] = {
    { "Pacific/Honolulu",          "HST10"                        },
    { "America/Anchorage",         "AKST9AKDT,M3.2.0,M11.1.0"     },
    { "America/Los_Angeles",       "PST8PDT,M3.2.0,M11.1.0"       },
    { "America/Denver",            "MST7MDT,M3.2.0,M11.1.0"       },
    { "America/Phoenix",           "MST7"                         },
    { "America/Chicago",           "CST6CDT,M3.2.0,M11.1.0"       },
    { "America/New_York",          "EST5EDT,M3.2.0,M11.1.0"       },
    { "America/Halifax",           "AST4ADT,M3.2.0,M11.1.0"       },
    { "America/Argentina/Buenos_Aires", "ART3"                    },
    { "Europe/London",             "GMT0BST,M3.5.0/1,M10.5.0"     },
    { "Europe/Dublin",             "GMT0BST,M3.5.0/1,M10.5.0"     },
    { "Europe/Berlin",             "CET-1CEST,M3.5.0,M10.5.0/3"   },
    { "Europe/Warsaw",             "CET-1CEST,M3.5.0,M10.5.0/3"   },
    { "Europe/Paris",              "CET-1CEST,M3.5.0,M10.5.0/3"   },
    { "Europe/Madrid",             "CET-1CEST,M3.5.0,M10.5.0/3"   },
    { "Europe/Rome",               "CET-1CEST,M3.5.0,M10.5.0/3"   },
    { "Europe/Athens",             "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Helsinki",           "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Kyiv",               "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Kiev",               "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Moscow",             "MSK-3"                        },
    { "Europe/Istanbul",           "MSK-3"                        },
    { "Asia/Kolkata",              "IST-5:30"                     },
    { "Asia/Shanghai",             "CST-8"                        },
    { "Asia/Singapore",            "CST-8"                        },
    { "Asia/Hong_Kong",            "CST-8"                        },
    { "Asia/Tokyo",                "JST-9"                        },
    { "Asia/Seoul",                "JST-9"                        },
    { "Australia/Sydney",          "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Pacific/Auckland",          "NZST-12NZDT,M9.5.0,M4.1.0/3"  },
};
static const size_t s_ianaMapLen = sizeof(s_ianaMap) / sizeof(s_ianaMap[0]);

// --- HTTP response accumulator ---------------------------------------------

struct HttpBuf {
    char data[512];
    size_t len;
};

static esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        auto* buf = static_cast<HttpBuf*>(evt->user_data);
        size_t room = sizeof(buf->data) - buf->len - 1;
        if (room > 0) {
            size_t copy = evt->data_len < static_cast<int>(room)
                ? static_cast<size_t>(evt->data_len) : room;
            std::memcpy(buf->data + buf->len, evt->data, copy);
            buf->len += copy;
            buf->data[buf->len] = '\0';
        }
    }
    return ESP_OK;
}

bool geoFetchPosixTz(char* out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';

    HttpBuf buf = {};

    esp_http_client_config_t cfg = {};
    cfg.url           = "http://ip-api.com/json/?fields=timezone";
    cfg.event_handler = &httpEventHandler;
    cfg.user_data     = &buf;
    cfg.timeout_ms    = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        LOGERR("geo_tz: http_client_init failed");
        return false;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || buf.len == 0) {
        LOGERR("geo_tz: http perform failed (err=%d status=%d)", err, status);
        return false;
    }

    // Expected payload: {"timezone":"America/New_York"}
    cJSON* root = cJSON_Parse(buf.data);
    if (!root) {
        LOGERR("geo_tz: JSON parse failed: %s", buf.data);
        return false;
    }
    cJSON* tzNode = cJSON_GetObjectItemCaseSensitive(root, "timezone");
    if (!cJSON_IsString(tzNode) || !tzNode->valuestring) {
        LOGERR("geo_tz: 'timezone' field missing");
        cJSON_Delete(root);
        return false;
    }

    const char* iana = tzNode->valuestring;
    const char* posix = nullptr;
    for (size_t i = 0; i < s_ianaMapLen; ++i) {
        if (std::strcmp(iana, s_ianaMap[i].iana) == 0) {
            posix = s_ianaMap[i].posix;
            break;
        }
    }

    if (!posix) {
        LOGINF("geo_tz: IANA '%s' not in map; user must choose manually", iana);
        cJSON_Delete(root);
        return false;
    }

    std::strncpy(out, posix, out_size - 1);
    out[out_size - 1] = '\0';
    LOGINF("geo_tz: detected '%s' -> POSIX '%s'", iana, out);
    cJSON_Delete(root);
    return true;
}
