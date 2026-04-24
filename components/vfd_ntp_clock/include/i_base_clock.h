// i_base_clock.h — the contract an application must fulfil so the engine
// can drive it. The Arduino original uses RTC_DS1307 from RTClib here; in
// the IDF port we drop the external RTC (the ESP32's internal clock +
// NTP is sufficient for a VFD that doesn't care about sub-second drift
// during brown-outs).
//
// If an external RTC is later needed, add getRtc() back as an optional
// hook via a separate IRtcClock interface, the same way IWeatherClock
// is layered on top.

#pragma once

#include <ctime>

class DisplayManager;
class IDisplayDriver;

class IBaseClock {
public:
    virtual ~IBaseClock() = default;

    // --- Identity ---------------------------------------------------------
    virtual const char* getAppName() const = 0;

    // --- WiFi credentials (from preferences) ------------------------------
    virtual const char* getSsid() const = 0;
    virtual const char* getPassword() const = 0;
    virtual const char* getTimezone() const = 0;

    // --- Hardware accessors ----------------------------------------------
    virtual IDisplayDriver& getDisplay() = 0;
    virtual DisplayManager& getClock()   = 0;

    // --- State ------------------------------------------------------------
    // Scene playback is gated on this; normally returns true while the FSM
    // is in RUNNING_NORMAL. The FSM manager polls it.
    virtual bool isOkToRunScenes() const = 0;

    // Format `now` into `txt` using strftime() conventions. The base class
    // provides a default, but the app can override to honor a custom TZ.
    virtual void formatTime(char* txt, unsigned txt_size,
                            const char* format, time_t now) = 0;

    // Called by the boot manager / button handler to jump into AP mode.
    // Blocks (inside the app's implementation) until the user saves creds.
    virtual void activateAccessPoint() = 0;
};
