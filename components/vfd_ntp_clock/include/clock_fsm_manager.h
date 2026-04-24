// clock_fsm_manager.h — tiny state machine driving the clock's lifecycle.
//
// Replaces the Arduino SimpleFSM dependency. States match the original:
//
//   STARTUP_ANIM ──► WIFI_CONNECT ──► NTP_SYNC ──► RUNNING_NORMAL
//                         │                │
//                         ▼                ▼
//                       AP_MODE          AP_MODE (fallback)
//
// Transitions are triggered by guard predicates the app-side events
// update indirectly (e.g. WiFi connected, NTP timed out, button press
// during boot).

#pragma once

#include <string>

class IBaseClock;

enum ClockState {
    FSM_STARTUP_ANIM,
    FSM_WIFI_CONNECT,
    FSM_NTP_SYNC,
    FSM_RUNNING_NORMAL,
    FSM_AP_MODE,
};

class ClockFsmManager {
public:
    explicit ClockFsmManager(IBaseClock& clock);

    // One-time startup. Kicks off the boot animation and transitions
    // into WIFI_CONNECT when it finishes.
    void setup();

    // Drive the machine. Must be called from the main loop.
    void update();

    // Used by IBaseClock::isOkToRunScenes(): returns true if we're in
    // the requested state. The name is a string for API parity with the
    // original SimpleFSM-backed code, which used state names as IDs.
    bool isInState(const std::string& stateName) const;

    // The app can force-enter AP mode (boot button, "re-config" request).
    void requestApMode() { _forceAp = true; }

private:
    void enter(ClockState s);
    const char* stateName(ClockState s) const;

    IBaseClock& _clock;
    ClockState  _state;
    int64_t     _stateEnteredUs;
    bool        _forceAp;
    bool        _ntpConfigured;
};
