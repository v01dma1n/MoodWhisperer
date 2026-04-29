// whisperer_app.h — the MoodWhisperer application.
//
// Mirrors GustavClock's GustavClockApp in structure: one class that
// inherits BaseNtpClockApp, owns a DispDriverPT6315 and a DisplayManager,
// and supplies the IBaseClock implementations the engine wants.
//
// Additions over GustavClock:
//   * QuoteManager + MoodProvider — picks a mood-appropriate quote.
//   * VL53L0X distance sensor — triggers quotes on movement; maps
//     distance to mood (<1 m gloomy, >1 m upbeat).
//   * MoodLeds — two WS2812 LEDs that glow amber when a quote plays.

#pragma once

#include "ESP32NTPClock.h"

#include "disp_driver_pt6315.h"
#include "whisperer_preferences.h"
#include "whisperer_access_point_manager.h"
#include "vl53l0x_driver.h"
#include "ds1307_driver.h"
#include "mood_leds.h"

#include <atomic>
#include <memory>

class WhispererApp : public BaseNtpClockApp {
public:
    static WhispererApp& getInstance();

    ~WhispererApp() override;

    void setup() override;
    void loop()  override;
    void setupHardware() override;

    WhispererPreferences& getPrefs()      { return _appPrefs; }
    QuoteManager&         getQuoteManager() { return *_quotes; }
    MoodLeds&             getMoodLeds()   { return _moodLeds; }

    // --- IBaseClock --------------------------------------------------------
    const char* getAppName()  const override { return APP_HOST_NAME; }
    const char* getSsid()     const override { return _appPrefs.config.ssid; }
    const char* getPassword() const override { return _appPrefs.config.password; }
    const char* getTimezone() const override { return _appPrefs.config.time_zone; }

    IDisplayDriver& getDisplay() override { return _display; }
    DisplayManager& getClock()   override { return *_displayManager; }

    bool isOkToRunScenes() const override;
    bool hasRtcTime()     const override { return _rtcAvailable; }
    void activateAccessPoint() override;
    void formatTime(char* txt, unsigned txt_size,
                    const char* format, time_t now) override;

    // Called from the distance-sensor task: reads sensor + processes result.
    void pollDistance();

private:
    WhispererApp();

    static constexpr const char* APP_HOST_NAME = "mood-whisperer";

    DispDriverPT6315                 _display;
    std::unique_ptr<DisplayManager>  _displayManager;

    WhispererPreferences             _appPrefs;
    WhispererAccessPointManager      _apManagerConcrete;

    std::unique_ptr<MoodProvider>    _moodProvider;
    std::unique_ptr<QuoteManager>    _quotes;

    Vl53l0xDriver                    _tof;
    Ds1307Driver                     _rtc;
    MoodLeds                         _moodLeds;
    bool                             _tofAvailable = false;
    bool                             _rtcPresent   = false;  // chip responded on I2C
    bool                             _rtcAvailable = false;  // chip had valid time at boot
    bool                             _rtcSynced    = false;  // NTP time written back to RTC

    // Classic quote trigger state.
    std::atomic<bool> _inQuoteMode{false};
    bool    _pendingQuote         = false;
    bool    _fadingOut            = false;
    int     _lastStableDistanceMm = -1;
    int64_t _lastQuoteTriggerMs   = 0;

    // Thermal Overload state.
    enum class ThermalPhase { NONE, WARN, OVERLOAD, VENTING, COOLDOWN };
    ThermalPhase _thermalPhase      = ThermalPhase::NONE;
    int64_t      _thermalPresenceMs = 0;
    int64_t      _thermalCooldownMs = 0;
    int          _thermalLastMm     = 2000;

    // Geiger Proximity Pulse state.
    bool  _geigerRunning    = false;  // true once breathing has started
    bool  _geigerFarTrigger = false;  // true if quote came from > 1 m

    void refreshMoodProvider();
    void onDistanceReading(int mm);
    void onDistanceReadingThermal(int mm);
    void onDistanceReadingGeiger(int mm);
    void triggerDistanceQuote(int distanceMm);
    static float moodFromDistance(int mm);

    int64_t _apTriggerHeldSinceUs = 0;
};
