// whisperer_preferences.h — application-specific preferences.
//
// Extends BaseConfig with:
//   * displayBrightness: 0..7
//   * showStartupAnimation: whether to play the splash on boot
//   * fixedMood: user-selected mood override (-1.0 .. +1.0); used when
//     the mood source is "fixed" (the default until a real sensor exists)
//   * moodSource: "fixed" or "random" for now; future sources get added
//     as new enum strings without breaking persisted data

#pragma once

#include "ESP32NTPClock.h"

// Extra preferences the base class doesn't know about. New fields land
// here, not in BaseConfig; they're serialized by the subclass's
// getPreferences()/putPreferences().
struct WhispererConfig : public BaseConfig {
    bool    showStartupAnimation;
    int32_t displayBrightness;               // 0..7
    char    moodSource[MAX_PREF_STRING_LEN]; // "fixed" | "random"
    int32_t fixedMoodTimes100;               // -100..+100
    char    owmApiKey[MAX_PREF_STRING_LEN];   // OpenWeatherMap API key (leave empty to disable)
    char    owmCity[MAX_PREF_STRING_LEN];     // e.g. "Warsaw,PL"
    char    triggerMode[MAX_PREF_STRING_LEN]; // "classic" | "thermal"
};

class WhispererPreferences : public BasePreferences {
public:
    WhispererPreferences();

    void getPreferences() override;
    void putPreferences() override;
    void dumpPreferences() override;

    WhispererConfig config;
};
