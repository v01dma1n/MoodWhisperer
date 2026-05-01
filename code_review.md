# MoodWhisperer — Technical Code Review

Covers `MoodWhisperer` (main app) and submodules `ESP32WiFi2`,
`ESP32NTPClock2`, `ESP32NTPClockDrivers2`.

---

## 1. Architecture & Logic Alignment

**Overall structure is sound.** `ESP32WiFi2` owns connectivity and prefs,
`ESP32NTPClock2` owns the clock engine / FSM / scene management / display
abstraction, `ESP32NTPClockDrivers2` owns hardware drivers, and
`MoodWhisperer` composes them. The `IBaseClock` → `BaseNtpClockApp` →
`WhispererApp` inheritance chain is clean, and the extension pattern
(subclass, override hardware methods, wire up pointers) is consistent
throughout.

---

### 1a. Three trigger modes are not isolated — shared state balloon

Classic, Thermal, and Geiger modes share a dozen `WhispererApp` member
variables (`_pendingQuote`, `_fadingOut`, `_thermalPhase`,
`_thermalPresenceMs`, `_thermalCooldownMs`, `_thermalLastMm`,
`_geigerRunning`, …) and `loop()` is one large if/else ladder gated on
`std::strcmp`. Adding a fourth mode requires editing `onDistanceReading`,
`loop()`, the header, and the preferences. A small abstract `TriggerMode`
interface would eliminate most of this. Named as technical debt; not
blocking.

---

### 1b. Cross-file extern coupling between preferences and AP manager

`whisperer_access_point_manager.cpp` declares:

```cpp
char s_brightnessBuffer[4] = "7";
char s_moodBuffer[8] = "0";
```

`whisperer_preferences.cpp` externs them:

```cpp
extern char s_brightnessBuffer[];
extern char s_moodBuffer[];
```

This is coupling through hidden global state. If either file is refactored,
the link silently breaks at runtime. Move the round-trip conversion into
`WhispererAccessPointManager` or into `putPreferences` directly so the
dependency is local and visible.

---

### 1c. `applyFormBody` always returns `true`

The signature promises `bool` on success/failure but every code path ends
in `return true`. `handleSave` uses the return value to decide whether to
send a 400. An empty POST body or a completely unrecognized payload is
silently accepted, saved, and triggers a reboot. The 400 branch in
`handleSave` is unreachable code.

---

### 1d. Geo-TZ detection result is fetched and discarded — dead code

`geoFetchPosixTz` is called in `clock_fsm_manager.cpp`, returns a POSIX
timezone string, which is then logged and thrown away:

```cpp
// clock_fsm_manager.cpp:93
if (geoFetchPosixTz(detected, sizeof(detected))) {
    LOGINF("Suggested timezone from geo: %s", detected);
    // detected is never stored, never applied to TZ env, never persisted
}
```

Either wire it through — `setenv("TZ", detected, 1)` + `tzset()` + persist
to NVS — or remove the call. As written it makes an HTTP request at every
boot when no timezone is configured and produces no useful effect.

---

### 1e. `MAX_SCENE_TEXT_LEN` defined in two places

Defined as `64` in both `enc_types.h:17` and `scene_manager.h:21`. They
are identical today and will silently diverge on a future edit. Remove the
definition from `scene_manager.h` and include `enc_types.h` there instead.

---

### 1f. Blocking weather fetch on AppTask

```cpp
// whisperer_app.cpp:199
s_weather = fetchWeather(cfg.owmApiKey, cfg.owmCity);
```

`fetchWeather` carries a 5-second HTTP timeout and runs synchronously
inside `loop()` on AppTask. The display and LED tasks keep running on
Core 1, but the FSM and scene manager are frozen for the full duration of
the call. A slow DNS lookup or routing failure exhausts the entire timeout
and causes visible scene-advance stalls. The fetch should be moved to a
dedicated one-shot task or use the `esp_http_client` async API so AppTask
continues ticking.

---

## 2. Functional Accuracy

---

### 2a. `esp_timer_create` return unchecked — will abort on first WiFi event

```cpp
// whisperer_app.cpp:122-123
esp_timer_create(&ta, &s_ledOffTimer);
esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ledWifiEventHandler, nullptr);
```

If `esp_timer_create` fails (e.g. heap exhaustion during boot), `s_ledOffTimer`
stays `nullptr`. The next WiFi event fires `ledWifiEventHandler`, which calls:

```cpp
esp_timer_start_once(s_ledOffTimer, 80 * 1000ULL);
```

IDF asserts `handle != NULL` inside `esp_timer_start_once` → hard abort.
This fires on the very first WiFi connect attempt.

Fix:

```cpp
if (esp_timer_create(&ta, &s_ledOffTimer) != ESP_OK) {
    LOGERR("led_off timer create failed");
    s_ledOffTimer = nullptr;
}
esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ledWifiEventHandler, nullptr);
esp_event_handler_register(IP_EVENT,   ESP_EVENT_ANY_ID, ledWifiEventHandler, nullptr);
```

`ledWifiEventHandler` already guards on `!s_ledOffTimer`, so the null
check there is already correct — the missing piece is the creation check.

---

### 2b. `_pendingQuote` and `_lastQuoteTriggerMs` accessed from two cores without synchronization

In Classic and Geiger modes, `distanceTask` (Core 1) writes:

```cpp
// triggerDistanceQuote / onDistanceReadingGeiger
_lastQuoteTriggerMs = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
_pendingQuote = true;
```

While `appTask` (Core 0) reads and writes the same fields in `loop()`.

`_inQuoteMode` is correctly declared `std::atomic<bool>`. `_pendingQuote`
(`bool`) and `_lastQuoteTriggerMs` (`int64_t`) are plain types. On a
32-bit Xtensa core, a 64-bit store is not atomic — a torn read of
`_lastQuoteTriggerMs` returns garbage. The result is a spurious quote
trigger or a missed cooldown, not a crash, but it is undefined behavior.

Fix — promote to atomics:

```cpp
// whisperer_app.h
std::atomic<bool>    _pendingQuote{false};
std::atomic<int64_t> _lastQuoteTriggerMs{0};
```

`_thermalLastMm` (written from `distanceTask`, read from `appTask` in the
cooldown check) has the same problem on `int` — aligned 32-bit reads are
effectively atomic on Xtensa but remain UB per the C++ standard. Declare
it `std::atomic<int>`.

---

### 2c. `MoodLeds` API called concurrently from multiple tasks without locking

In Thermal mode:

- `distanceTask` calls `setWarn()`, `startOverload()`, and `clearImmediate()`.
- `appTask` calls `clearImmediate()` in the VENTING → COOLDOWN transition.
- `displayTask` calls `update()` at 50 Hz.

All three paths call `applyBrightness()`, which calls `led_strip_set_pixel()`
and `led_strip_refresh()`. The RMT-backed LED strip driver is not documented
as reentrant. Concurrent calls from two cores can corrupt the RMT descriptor
ring.

Fix — add a `portMUX_TYPE` spinlock to `MoodLeds` and take it in every
method that touches `_strip`:

```cpp
// mood_leds.h
private:
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
```

```cpp
// mood_leds.cpp — in setWarn(), clearImmediate(), setFull(), update(), etc.
portENTER_CRITICAL(&_mux);
// ... modify _brightness / _state / call applyBrightness() ...
portEXIT_CRITICAL(&_mux);
```

---

### 2d. `gpio_config()` return values are silently ignored

```cpp
// whisperer_app.cpp:101, 111
gpio_config(&io);
```

Called twice in `setupHardware()` — once for AP_TRIGGER_GPIO, once for
LED_GPIO — with the return value discarded. GPIO configuration failure is
rare on correct hardware but leaves the button and activity LED silently
dead with no log output. Wrap with `ESP_ERROR_CHECK` or at minimum log the
error.

---

### 2e. I2C bus shared by DS1307 and VL53L0X has a brief concurrent-access window

Both peripherals use `I2C_NUM_0`. `distanceTask` calls
`_tof.readRangeMm()` continuously. `appTask` calls `_rtc.writeTime()` once
in `loop()` when `!_rtcSynced && timeAvail`. There is a small window where
both are active on the same bus simultaneously. `i2c_master_write_to_device`
is not protected against concurrent callers on the same port.

The window is one-time and brief. The simplest fix is to call `writeTime()`
at the end of `setup()` after a `vTaskDelay` once NTP sync completes,
before `distanceTask` is created. Alternatively, pass a shared
`SemaphoreHandle_t` to both drivers.

---

## 3. Error Handling

Most error paths are handled correctly: all `nvs_*`, I2C, HTTP, and JSON
failures log through `LOGERR` and propagate return codes. The following
gaps remain.

---

### 3a. `esp_timer_create` unchecked

Covered in §2a. This is the only unhandled resource allocation failure that
leads directly to a hard abort.

---

### 3b. `WhispererPreferences::putPreferences()` is missing `nvs_commit`

`BasePreferences::putPreferences()` calls `nvs_commit` and then
`closeNvs()`. `WhispererPreferences::putPreferences()` calls the base
first, then reopens NVS, writes its own keys (`brightness`, `mood_src`,
`owm_key`, `owm_city`, `trigger_mode`, etc.), and closes without
committing:

```cpp
// whisperer_preferences.cpp:78-90
BasePreferences::putPreferences();   // commits base fields, closes NVS

if (!openNvs(true)) return;

writeBool  (KEY_SHOW_STARTUP, config.showStartupAnimation);
writeInt   (KEY_BRIGHTNESS,   config.displayBrightness);
writeString(KEY_MOOD_SRC,     config.moodSource);
writeInt   (KEY_FIXED_MOOD,   config.fixedMoodTimes100);
writeString(KEY_OWM_KEY,      config.owmApiKey);
writeString(KEY_OWM_CITY,     config.owmCity);
writeString(KEY_TRIGGER_MODE, config.triggerMode);

closeNvs();   // ← no nvs_commit
```

A power loss in the ~1.5 s window between `closeNvs()` and the reboot
timer firing leaves all app-specific config absent from NVS on next boot
(device resets to defaults and may re-enter AP mode).

Fix — expose a `commitNvs()` helper in `BasePreferences`:

```cpp
// base_preferences.h
protected:
    void commitNvs();
```

```cpp
// base_preferences.cpp
void BasePreferences::commitNvs() {
    if (!_nvs_open) return;
    nvs_commit(static_cast<nvs_handle_t>(_nvs_handle));
}
```

Call it at the end of `WhispererPreferences::putPreferences()` before
`closeNvs()`.

---

### 3c. `applyFormBody` always returns `true` — 400 path in `handleSave` unreachable

Covered in §1c. The immediate consequence for error handling is that a
malformed or empty POST body triggers a reboot instead of a 400 response,
leaving the user staring at a reconnect spinner.

---

### 3d. `gpio_config` returns unchecked

Covered in §2d.

---

## 4. Code Quality

---

### 4a. Stale comments

- `whisperer_access_point_manager.cpp:93–99`: references
  `WhispererApp::onConfigSaved` which does not exist anywhere in the
  codebase. The actual mechanism (buffer-to-int sync in `putPreferences`)
  works correctly but the comment describes a method that was never
  implemented or was removed.

- `whisperer_preferences.h:25`: `triggerMode` field comment reads
  `"classic" | "thermal"` — the third mode `"geiger"` is absent.

---

### 4b. `pi` hardcoded three times in `mood_leds.cpp`

```cpp
// mood_leds.cpp:79
_brightness = THROB_MIN + (THROB_MAX - THROB_MIN) *
              (0.5f + 0.5f * sinf(_throbTick * (2.0f * 3.14159f / 33.0f)));

// mood_leds.cpp:83
_geigerPhase += 2.0f * 3.14159f * _geigerFreqHz / UPDATE_RATE_HZ;
if (_geigerPhase > 2.0f * 3.14159f) _geigerPhase -= 2.0f * 3.14159f;

// mood_leds.cpp:99
_geigerPhase = 3.14159f * 1.5f;
```

Replace all occurrences with a single named constant or `static_cast<float>(M_PI)`:

```cpp
static constexpr float TWO_PI = 2.0f * 3.14159265f;
```

---

### 4c. `isInState(const std::string&)` constructs a `std::string` on every FSM tick

```cpp
// clock_fsm_manager.h
bool isInState(const std::string& stateName) const;
```

All callers pass string literals. `std::string` construction from a literal
allocates heap memory on every call. `isInState` is called from
`whisperer_app.cpp` multiple times per `loop()` iteration (10 Hz) and from
`base_ntp_clock_app.cpp`.

Change the signature to `const char*`:

```cpp
bool isInState(const char* name) const;
// impl: return std::strcmp(name, stateName(_state)) == 0;
```

All call sites already pass string literals and require no changes.

---

### 4d. `distanceTask` wastes CPU when sensor is unavailable

```cpp
void WhispererApp::pollDistance() {
    if (!_tofAvailable) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }
    ...
}
```

When `_tofAvailable` is false this task wakes at 10 Hz, yields immediately,
and never does anything useful. `_tofAvailable` is determined before
`distanceTask` is created. Skip creating the task entirely when the sensor
is absent:

```cpp
// whisperer_app.cpp in setup()
_tofAvailable = _tof.init();
if (_tofAvailable) {
    _tof.startContinuous();
    LOGINF("VL53L0X distance sensor started");
    xTaskCreatePinnedToCore(distanceTask, "DistanceTask",
                            4096, nullptr, 4, nullptr, 1);
} else {
    LOGINF("VL53L0X not available — distance triggering disabled");
}
```

---

## Summary Table

| # | Severity | File(s) | Issue |
|---|----------|---------|-------|
| 2a | **Crash** | `whisperer_app.cpp` | `esp_timer_create` unchecked → null handle → abort on first WiFi event |
| 3b | **Data loss** | `whisperer_preferences.cpp` | Missing `nvs_commit` in `putPreferences` — app config not persisted on power loss |
| 2b | **UB / data race** | `whisperer_app.h/.cpp` | `_pendingQuote` (bool) and `_lastQuoteTriggerMs` (int64_t) accessed across Core 0 / Core 1 without atomics |
| 2c | **UB / potential crash** | `mood_leds.cpp` | LED strip API called from `distanceTask` + `appTask` + `displayTask` without locking |
| 1c / 3c | **Logic bug** | `base_access_point_manager.cpp` | `applyFormBody` always returns `true`; 400 path in `handleSave` unreachable |
| 2d | **Silent failure** | `whisperer_app.cpp` | `gpio_config()` return values unchecked |
| 2e | **Race** | `vl53l0x_driver.cpp`, `ds1307_driver.cpp` | I2C bus shared without locking between `distanceTask` and `appTask` |
| 1d | **Dead code** | `clock_fsm_manager.cpp` | Geo-TZ detection result fetched, logged, discarded — never applied |
| 1b | **Design smell** | `whisperer_preferences.cpp`, `whisperer_access_point_manager.cpp` | `s_brightnessBuffer`/`s_moodBuffer` cross-file extern coupling |
| 1e | **Maintenance risk** | `enc_types.h`, `scene_manager.h` | `MAX_SCENE_TEXT_LEN` defined in two places |
| 1f | **Perf / UX** | `whisperer_app.cpp` | Synchronous `fetchWeather` blocks AppTask for up to 5 s |
| 4a | **Stale docs** | `whisperer_access_point_manager.cpp`, `whisperer_preferences.h` | `onConfigSaved` reference and incomplete `triggerMode` comment |
| 4b | **Polish** | `mood_leds.cpp` | `pi` hardcoded as `3.14159f` three times |
| 4c | **Minor perf** | `clock_fsm_manager.cpp` | `isInState(std::string)` heap-allocates on every FSM tick |
| 4d | **Minor waste** | `whisperer_app.cpp` | `distanceTask` spins at 10 Hz even when sensor is absent |
