// whisperer_app.h — the MoodWhisperer application.
//
// Mirrors GustavClock's GustavClockApp in structure: one class that
// inherits BaseNtpClockApp, owns a DispDriverPT6315 and a DisplayManager,
// and supplies the IBaseClock implementations the engine wants.
//
// Additions over GustavClock:
//   * QuoteManager + MoodProvider — picks a mood-appropriate quote for
//     the "Quote" scene in the playlist.
//   * No weather manager. The quote stream is our "extra content".

#pragma once

#include "ESP32NTPClock.h"

#include "disp_driver_pt6315.h"
#include "whisperer_preferences.h"
#include "whisperer_access_point_manager.h"

#include <memory>

class WhispererApp : public BaseNtpClockApp {
public:
    // Singleton: the scene-playlist data getter callbacks need a way to
    // reach the instance without receiving 'this' explicitly.
    static WhispererApp& getInstance();

    ~WhispererApp() override;

    void setup() override;
    void loop()  override;

    void setupHardware() override;

    // Preferences accessor (for code that needs to read specific fields).
    WhispererPreferences& getPrefs() { return _appPrefs; }

    // Quote manager accessor for the scene playlist's text providers.
    QuoteManager& getQuoteManager() { return *_quotes; }

    // --- IBaseClock --------------------------------------------------------
    const char* getAppName()  const override { return APP_HOST_NAME; }
    const char* getSsid()     const override { return _appPrefs.config.ssid; }
    const char* getPassword() const override { return _appPrefs.config.password; }
    const char* getTimezone() const override { return _appPrefs.config.time_zone; }

    IDisplayDriver& getDisplay() override { return _display; }
    DisplayManager& getClock()   override { return *_displayManager; }

    bool isOkToRunScenes() const override;
    void activateAccessPoint() override;
    void formatTime(char* txt, unsigned txt_size,
                    const char* format, time_t now) override;

private:
    WhispererApp();

    static constexpr const char* APP_HOST_NAME = "mood-whisperer";

    // Hardware + managers (owned here).
    DispDriverPT6315                     _display;
    std::unique_ptr<DisplayManager>      _displayManager;

    WhispererPreferences                 _appPrefs;
    WhispererAccessPointManager          _apManagerConcrete;

    // Mood + quotes.
    std::unique_ptr<MoodProvider>        _moodProvider;
    std::unique_ptr<QuoteManager>        _quotes;

    // Rebuild the mood provider based on preferences (called after
    // preferences are loaded and whenever the portal save changes them).
    void refreshMoodProvider();

    // AP trigger hold detection. Non-zero once the button goes low;
    // reset to 0 on release or after AP mode is requested.
    int64_t _apTriggerHeldSinceUs = 0;
};
