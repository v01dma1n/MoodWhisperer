// clock_fsm_manager.cpp — concrete state machine.

#include "clock_fsm_manager.h"
#include "i_base_clock.h"
#include "display_manager.h"
#include "wifi_connector.h"
#include "sntp_client.h"
#include "geo_tz_client.h"
#include "logging.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

static constexpr int64_t WIFI_TIMEOUT_MS = 60 * 1000;
static constexpr int64_t NTP_TIMEOUT_MS  = 30 * 1000;

ClockFsmManager::ClockFsmManager(IBaseClock& clock)
    : _clock(clock),
      _state(FSM_STARTUP_ANIM),
      _stateEnteredUs(0),
      _forceAp(false),
      _ntpConfigured(false) {}

void ClockFsmManager::setup() {
    enter(FSM_STARTUP_ANIM);
}

const char* ClockFsmManager::stateName(ClockState s) const {
    switch (s) {
        case FSM_STARTUP_ANIM:   return "STARTUP_ANIM";
        case FSM_WIFI_CONNECT:   return "WIFI_CONNECT";
        case FSM_NTP_SYNC:       return "NTP_SYNC";
        case FSM_RUNNING_NORMAL: return "RUNNING_NORMAL";
        case FSM_AP_MODE:        return "AP_MODE";
    }
    return "UNKNOWN";
}

bool ClockFsmManager::isInState(const std::string& name) const {
    return std::strcmp(name.c_str(), stateName(_state)) == 0;
}

void ClockFsmManager::enter(ClockState s) {
    LOGINF("FSM: %s -> %s", stateName(_state), stateName(s));
    _state = s;
    _stateEnteredUs = esp_timer_get_time();
}

void ClockFsmManager::update() {
    // A pending AP request from the boot manager or touch sensor wins
    // over everything except RUNNING_NORMAL handoff already in progress.
    if (_forceAp && _state != FSM_AP_MODE) {
        _forceAp = false;
        enter(FSM_AP_MODE);
    }

    int64_t elapsedMs = (esp_timer_get_time() - _stateEnteredUs) / 1000;

    switch (_state) {
        case FSM_STARTUP_ANIM: {
            // Run the splash animation for ~2s, then move on. The app
            // queued the actual StaticTextAnimation in its setup().
            if (elapsedMs > 2000 || !_clock.getClock().isAnimationRunning()) {
                enter(FSM_WIFI_CONNECT);
            }
            break;
        }
        case FSM_WIFI_CONNECT: {
            const char* ssid = _clock.getSsid();
            const char* pass = _clock.getPassword();
            if (!ssid || !*ssid) {
                LOGINF("No WiFi SSID stored; dropping into AP mode");
                enter(FSM_AP_MODE);
                break;
            }
            bool ok = WiFiConnect(_clock.getAppName(), ssid, pass, /*attempts=*/3);
            if (ok) {
                // If the app stored an empty timezone, try to detect one.
                if (!_clock.getTimezone() || !*_clock.getTimezone()) {
                    char detected[64] = {0};
                    if (geoFetchPosixTz(detected, sizeof(detected))) {
                        // The app itself owns the config struct, so we
                        // can't write there without going through it.
                        // Log the suggestion; persisting is the app's
                        // responsibility (e.g., a setTimezone() hook).
                        LOGINF("Suggested timezone from geo: %s", detected);
                    }
                }
                enter(FSM_NTP_SYNC);
            } else if (elapsedMs > WIFI_TIMEOUT_MS) {
                enter(FSM_AP_MODE);
            }
            break;
        }
        case FSM_NTP_SYNC: {
            if (!_ntpConfigured) {
                setupSntp(_clock.getTimezone());
                _ntpConfigured = true;
            }
            int r = loopSntpGetTime(NTP_TIMEOUT_MS);
            if (r == 1) {
                enter(FSM_RUNNING_NORMAL);
            } else if (r == -1) {
                // Without an external RTC we can't fake a clock, so fall
                // back to the setup portal and let the user retry.
                enter(FSM_AP_MODE);
            }
            break;
        }
        case FSM_RUNNING_NORMAL: {
            // Nothing to do here — the app's scene manager drives itself.
            break;
        }
        case FSM_AP_MODE: {
            // activateAccessPoint() blocks inside the app; we'll never
            // leave this state except by reboot (the "save" handler
            // triggers esp_restart()).
            _clock.activateAccessPoint();
            // If activateAccessPoint ever returned, park here.
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }
    }
}
