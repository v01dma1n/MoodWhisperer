// base_preferences.cpp — concrete NVS backing for BasePreferences.

#include "base_preferences.h"
#include "logging.h"

#include "nvs_flash.h"
#include "nvs.h"

#include <cstring>

// NVS keys for the base fields. Subclasses use their own keys — but
// should avoid collisions with these four.
static constexpr const char* KEY_SSID      = "ssid";
static constexpr const char* KEY_PASSWORD  = "password";
static constexpr const char* KEY_TIMEZONE  = "timezone";
static constexpr const char* KEY_LOG_LEVEL = "loglevel";

BasePreferences::BasePreferences(BaseConfig& config)
    : _config(config), _nvs_handle(0), _nvs_open(false), _initialized(false) {}

BasePreferences::~BasePreferences() {
    if (_nvs_open) closeNvs();
}

void BasePreferences::setup() {
    if (_initialized) return;

    // Initialize NVS. If the partition is full or a new version of the
    // layout was flashed, wipe and retry once.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOGINF("NVS needs erase; reformatting");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    // Start with empty strings so uninitialized NVS yields a blank form
    // in the captive portal (as opposed to random stack garbage).
    std::memset(&_config, 0, sizeof(_config));
    _config.logLevel = APP_LOG_INFO;

    _initialized = true;
}

bool BasePreferences::openNvs(bool read_write) {
    if (_nvs_open) return true;

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(PREF_NAMESPACE,
                             read_write ? NVS_READWRITE : NVS_READONLY,
                             &h);
    if (err != ESP_OK) {
        LOGERR("nvs_open(%s) failed: %d", read_write ? "RW" : "RO", err);
        return false;
    }
    _nvs_handle = static_cast<uint32_t>(h);
    _nvs_open = true;
    return true;
}

void BasePreferences::closeNvs() {
    if (!_nvs_open) return;
    nvs_close(static_cast<nvs_handle_t>(_nvs_handle));
    _nvs_handle = 0;
    _nvs_open = false;
}

bool BasePreferences::readString(const char* key, char* out, size_t out_size) {
    if (!_nvs_open) return false;
    size_t len = out_size;
    esp_err_t err = nvs_get_str(static_cast<nvs_handle_t>(_nvs_handle),
                                key, out, &len);
    if (err == ESP_OK) return true;
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        LOGERR("nvs_get_str(%s) failed: %d", key, err);
    }
    if (out_size > 0) out[0] = '\0';
    return false;
}

void BasePreferences::writeString(const char* key, const char* value) {
    if (!_nvs_open) return;
    esp_err_t err = nvs_set_str(static_cast<nvs_handle_t>(_nvs_handle),
                                key, value ? value : "");
    if (err != ESP_OK) LOGERR("nvs_set_str(%s) failed: %d", key, err);
}

bool BasePreferences::readBool(const char* key, bool default_value) {
    if (!_nvs_open) return default_value;
    uint8_t v = default_value ? 1 : 0;
    esp_err_t err = nvs_get_u8(static_cast<nvs_handle_t>(_nvs_handle), key, &v);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        LOGERR("nvs_get_u8(%s) failed: %d", key, err);
    }
    return v != 0;
}

void BasePreferences::writeBool(const char* key, bool value) {
    if (!_nvs_open) return;
    esp_err_t err = nvs_set_u8(static_cast<nvs_handle_t>(_nvs_handle),
                               key, value ? 1 : 0);
    if (err != ESP_OK) LOGERR("nvs_set_u8(%s) failed: %d", key, err);
}

int32_t BasePreferences::readInt(const char* key, int32_t default_value) {
    if (!_nvs_open) return default_value;
    int32_t v = default_value;
    esp_err_t err = nvs_get_i32(static_cast<nvs_handle_t>(_nvs_handle), key, &v);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        LOGERR("nvs_get_i32(%s) failed: %d", key, err);
    }
    return v;
}

void BasePreferences::writeInt(const char* key, int32_t value) {
    if (!_nvs_open) return;
    esp_err_t err = nvs_set_i32(static_cast<nvs_handle_t>(_nvs_handle),
                                key, value);
    if (err != ESP_OK) LOGERR("nvs_set_i32(%s) failed: %d", key, err);
}

void BasePreferences::getPreferences() {
    if (!_initialized) setup();
    if (!openNvs(false)) return;

    readString(KEY_SSID,     _config.ssid,      sizeof(_config.ssid));
    readString(KEY_PASSWORD, _config.password,  sizeof(_config.password));
    readString(KEY_TIMEZONE, _config.time_zone, sizeof(_config.time_zone));

    // Persist log level as int to avoid bit-order assumptions on the enum.
    int32_t lvl = readInt(KEY_LOG_LEVEL, APP_LOG_INFO);
    if (lvl < APP_LOG_ERROR || lvl > APP_LOG_DEBUG) lvl = APP_LOG_INFO;
    _config.logLevel = static_cast<AppLogLevel>(lvl);

    closeNvs();
}

void BasePreferences::putPreferences() {
    if (!_initialized) setup();
    if (!openNvs(true)) return;

    writeString(KEY_SSID,     _config.ssid);
    writeString(KEY_PASSWORD, _config.password);
    writeString(KEY_TIMEZONE, _config.time_zone);
    writeInt   (KEY_LOG_LEVEL, static_cast<int32_t>(_config.logLevel));

    // Commit to flash. Without this, a reset could lose the writes.
    nvs_commit(static_cast<nvs_handle_t>(_nvs_handle));

    closeNvs();
}

void BasePreferences::dumpPreferences() {
    LOGDBG("Pref=%s: %s", KEY_SSID,     _config.ssid);
    LOGDBG("Pref=%s: %s", KEY_PASSWORD, "***");          // never log the pw
    LOGDBG("Pref=%s: %s", KEY_TIMEZONE, _config.time_zone);
    LOGDBG("Pref=%s: %d", KEY_LOG_LEVEL, static_cast<int>(_config.logLevel));
}
