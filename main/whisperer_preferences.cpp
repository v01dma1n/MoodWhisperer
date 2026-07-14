#include "whisperer_preferences.h"

#include <cstdlib>
#include <cstring>

static constexpr const char* KEY_SHOW_STARTUP = "show_startup";
static constexpr const char* KEY_BRIGHTNESS   = "brightness";
static constexpr const char* KEY_MOOD_SRC     = "mood_src";
static constexpr const char* KEY_FIXED_MOOD   = "fixed_mood";
static constexpr const char* KEY_OWM_KEY      = "owm_key";
static constexpr const char* KEY_OWM_CITY     = "owm_city";
static constexpr const char* KEY_TRIGGER_MODE  = "trigger_mode";
static constexpr const char* KEY_XTALK_RATE    = "xtalk_rate";
static constexpr const char* KEY_XTALK_CALIB   = "xtalk_calib";

WhispererPreferences::WhispererPreferences()
    : BasePreferences(config) {
    std::memset(&config, 0, sizeof(config));
    config.showStartupAnimation = true;
    config.displayBrightness    = 7;
    std::strncpy(config.moodSource, "random",
                 sizeof(config.moodSource) - 1);
    config.fixedMoodTimes100    = 0;
    std::strncpy(config.triggerMode, "classic", sizeof(config.triggerMode) - 1);
    config.xtalkRate        = 0;
    config.xtalkCalibPending = false;
}

void WhispererPreferences::getPreferences() {
    // Base fields first (ssid/password/timezone/loglevel).
    BasePreferences::getPreferences();

    if (!openNvs(false)) return;

    config.showStartupAnimation = readBool(KEY_SHOW_STARTUP, true);
    config.displayBrightness    = readInt (KEY_BRIGHTNESS,   7);
    if (config.displayBrightness < 0) config.displayBrightness = 0;
    if (config.displayBrightness > 7) config.displayBrightness = 7;

    readString(KEY_MOOD_SRC, config.moodSource, sizeof(config.moodSource));
    if (config.moodSource[0] == '\0') {
        std::strncpy(config.moodSource, "random", sizeof(config.moodSource) - 1);
    }

    config.fixedMoodTimes100 = readInt(KEY_FIXED_MOOD, 0);
    if (config.fixedMoodTimes100 < -100) config.fixedMoodTimes100 = -100;
    if (config.fixedMoodTimes100 >  100) config.fixedMoodTimes100 =  100;

    readString(KEY_OWM_KEY,  config.owmApiKey, sizeof(config.owmApiKey));
    readString(KEY_OWM_CITY, config.owmCity,   sizeof(config.owmCity));

    readString(KEY_TRIGGER_MODE, config.triggerMode, sizeof(config.triggerMode));
    if (config.triggerMode[0] == '\0') {
        std::strncpy(config.triggerMode, "classic", sizeof(config.triggerMode) - 1);
    }

    config.xtalkRate         = readInt (KEY_XTALK_RATE,  0);
    config.xtalkCalibPending = readBool(KEY_XTALK_CALIB, false);

    closeNvs();
}

// Declared in whisperer_access_point_manager.cpp; used to shuttle
// numeric fields through the string-typed form plumbing.
extern char s_brightnessBuffer[];
extern char s_moodBuffer[];
extern char s_xtalkRateBuffer[];

void WhispererPreferences::putPreferences() {
    // Re-sync buffers -> numeric fields if the AP form touched them.
    // Safe no-ops when invoked outside the portal because the buffers
    // stay at their last-loaded values.
    if (s_brightnessBuffer[0] != '\0') {
        int b = atoi(s_brightnessBuffer);
        if (b < 0) b = 0;
        if (b > 7) b = 7;
        config.displayBrightness = b;
    }
    if (s_moodBuffer[0] != '\0') {
        int m = atoi(s_moodBuffer);
        if (m < -100) m = -100;
        if (m >  100) m =  100;
        config.fixedMoodTimes100 = m;
    }
    if (s_xtalkRateBuffer[0] != '\0') {
        int r = atoi(s_xtalkRateBuffer);
        if (r < 0)     r = 0;
        if (r > 65535) r = 65535;
        config.xtalkRate = (int32_t)r;
    }

    BasePreferences::putPreferences();

    if (!openNvs(true)) return;

    writeBool  (KEY_SHOW_STARTUP, config.showStartupAnimation);
    writeInt   (KEY_BRIGHTNESS,   config.displayBrightness);
    writeString(KEY_MOOD_SRC,     config.moodSource);
    writeInt   (KEY_FIXED_MOOD,   config.fixedMoodTimes100);
    writeString(KEY_OWM_KEY,      config.owmApiKey);
    writeString(KEY_OWM_CITY,     config.owmCity);
    writeString(KEY_TRIGGER_MODE, config.triggerMode);
    writeInt   (KEY_XTALK_RATE,   config.xtalkRate);
    writeBool  (KEY_XTALK_CALIB,  config.xtalkCalibPending);

    closeNvs();
}

void WhispererPreferences::dumpPreferences() {
    BasePreferences::dumpPreferences();
    LOGDBG("Pref=%s: %s", KEY_SHOW_STARTUP,
           config.showStartupAnimation ? "yes" : "no");
    LOGDBG("Pref=%s: %d", KEY_BRIGHTNESS, (int)config.displayBrightness);
    LOGDBG("Pref=%s: %s", KEY_MOOD_SRC,   config.moodSource);
    LOGDBG("Pref=%s: %d (mood=%.2f)", KEY_FIXED_MOOD,
           (int)config.fixedMoodTimes100,
           static_cast<float>(config.fixedMoodTimes100) / 100.0f);
    LOGDBG("Pref=%s: %s", KEY_OWM_KEY,      config.owmApiKey[0] ? "(set)" : "(empty)");
    LOGDBG("Pref=%s: %s", KEY_OWM_CITY,     config.owmCity);
    LOGDBG("Pref=%s: %s", KEY_TRIGGER_MODE, config.triggerMode);
    LOGDBG("Pref=%s: %d (Q9.7 MCPS)", KEY_XTALK_RATE,  (int)config.xtalkRate);
    LOGDBG("Pref=%s: %s", KEY_XTALK_CALIB, config.xtalkCalibPending ? "pending" : "no");
}
