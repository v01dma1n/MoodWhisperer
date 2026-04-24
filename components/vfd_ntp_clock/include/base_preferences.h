// base_preferences.h — NVS-backed preferences with an extensible config struct.
//
// The Arduino original uses the ESP32 Arduino `Preferences` class. Here
// we wrap ESP-IDF's `nvs_flash` / `nvs_handle_t` API. The user-visible
// shape — a `BaseConfig` POD plus a `BasePreferences` class with virtual
// get/put/dump — is preserved so apps extend it exactly the same way:
//
//   struct WhispererConfig : public BaseConfig { ... extra fields ... };
//   class  WhispererPreferences : public BasePreferences { ... overrides ... };
//
// `BaseConfig` carries *only* the credentials/settings the engine itself
// needs: SSID, password, POSIX timezone, and log level. Everything else
// (weather keys, display brightness, mood thresholds, ...) lives in the
// subclass's config struct and is serialized via the subclass's overrides.

#pragma once

#include "enc_types.h"

#include <cstdint>
#include <cstddef>

// Storage namespace inside NVS. Everything goes into one namespace; keys
// are short string slugs that match FormField::id where possible.
static constexpr const char* PREF_NAMESPACE = "config";

// Upper bound for any single string preference. Matches the Arduino
// original. If an app needs longer strings, it can declare its own
// larger fields in its derived config struct (it's just a char[]).
static constexpr size_t MAX_PREF_STRING_LEN = 64;

// Generic fields shared by every clock app.
// Subclasses ADD fields; they do not replace these.
struct BaseConfig {
    char ssid[MAX_PREF_STRING_LEN];
    char password[MAX_PREF_STRING_LEN];
    char time_zone[MAX_PREF_STRING_LEN];   // POSIX TZ, e.g. "EST5EDT,M3.2.0..."
    AppLogLevel logLevel;
};

class BasePreferences {
public:
    // The constructor takes a reference to a subclass's config struct so
    // the base class can read/write the base fields out of it.
    explicit BasePreferences(BaseConfig& config);
    virtual ~BasePreferences();

    // One-time NVS init. Idempotent. Subclasses should NOT override this
    // unless they have extra subsystems to bring up.
    virtual void setup();

    // Virtual read/write/dump — subclasses call the base version first,
    // then serialize their own fields.
    virtual void getPreferences();
    virtual void putPreferences();
    virtual void dumpPreferences();

    // Non-owning reference to the config struct this instance manages.
    BaseConfig& getConfig() { return _config; }

protected:
    // Low-level helpers for subclasses to serialize extra fields without
    // repeating the open/close dance.
    bool readString(const char* key, char* out, size_t out_size);
    void writeString(const char* key, const char* value);
    bool readBool(const char* key, bool default_value);
    void writeBool(const char* key, bool value);
    int32_t readInt(const char* key, int32_t default_value);
    void writeInt(const char* key, int32_t value);

    // Open the NVS namespace in either read-only or read/write mode.
    // Caller must balance with closeNvs(). These are used by the default
    // read/write flow; if a subclass needs finer control it can call them
    // directly in its override.
    bool openNvs(bool read_write);
    void closeNvs();

    BaseConfig& _config;

private:
    // Opaque handle — declared as uint32_t to avoid pulling nvs.h into
    // every include site. The .cpp file does the cast.
    uint32_t _nvs_handle;
    bool     _nvs_open;
    bool     _initialized;
};
