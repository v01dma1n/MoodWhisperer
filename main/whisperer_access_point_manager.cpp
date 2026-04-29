#include "whisperer_access_point_manager.h"

#include <cstdio>

static const PrefSelectOption s_triggerModeOptions[] = {
    { "Classic (movement change)", "classic" },
    { "Thermal Overload",          "thermal" },
    { "Geiger Proximity Pulse",    "geiger"  },
};
static const int s_numTriggerModeOptions =
    sizeof(s_triggerModeOptions) / sizeof(s_triggerModeOptions[0]);

static const PrefSelectOption s_moodSourceOptions[] = {
    { "Random",              "random" },
    { "Fixed (use slider)",  "fixed"  },
};
static const int s_numMoodSourceOptions =
    sizeof(s_moodSourceOptions) / sizeof(s_moodSourceOptions[0]);

static const PrefSelectOption s_brightnessOptions[] = {
    { "1 / 7 (dimmest)", "1" },
    { "2 / 7",           "2" },
    { "3 / 7",           "3" },
    { "4 / 7",           "4" },
    { "5 / 7",           "5" },
    { "6 / 7",           "6" },
    { "7 / 7 (max)",     "7" },
};
static const int s_numBrightnessOptions =
    sizeof(s_brightnessOptions) / sizeof(s_brightnessOptions[0]);

// Brightness round-trips through a small string buffer because our
// PREF_SELECT form type stores a string. applyFormBody() will atoi() it
// back into config.displayBrightness before save.
// External linkage so WhispererPreferences::putPreferences() can pick
// the buffer up on the way out.
char s_brightnessBuffer[4] = "7";

// Similarly for fixed mood (stored as int * 100 for NVS portability).
char s_moodBuffer[8] = "0";

void WhispererAccessPointManager::initializeFormFields() {
    BaseAccessPointManager::initializeFormFields();

    auto& cfg = static_cast<WhispererPreferences&>(_prefs).config;

    // Sync the string mirrors with the current config values so the form
    // renders the right selection pre-checked.
    snprintf(s_brightnessBuffer, sizeof(s_brightnessBuffer), "%d",
             (int)cfg.displayBrightness);
    snprintf(s_moodBuffer, sizeof(s_moodBuffer), "%d",
             (int)cfg.fixedMoodTimes100);

    _formFields.push_back(FormField{
        "brightness", "Display Brightness", false, VALIDATION_NONE,
        PREF_SELECT, { .str_pref = s_brightnessBuffer },
        s_brightnessOptions, s_numBrightnessOptions,
    });

    _formFields.push_back(FormField{
        "show_startup", "Show Startup Animation", false, VALIDATION_NONE,
        PREF_BOOL, { .bool_pref = &cfg.showStartupAnimation },
        nullptr, 0,
    });

    _formFields.push_back(FormField{
        "mood_src", "Mood Source", false, VALIDATION_NONE, PREF_SELECT,
        { .str_pref = cfg.moodSource },
        s_moodSourceOptions, s_numMoodSourceOptions,
    });

    _formFields.push_back(FormField{
        "fixed_mood", "Fixed Mood (-100..+100)", false, VALIDATION_INTEGER,
        PREF_STRING, { .str_pref = s_moodBuffer }, nullptr, 0,
    });

    _formFields.push_back(FormField{
        "trigger_mode", "Quote Trigger Mode", false, VALIDATION_NONE, PREF_SELECT,
        { .str_pref = cfg.triggerMode },
        s_triggerModeOptions, s_numTriggerModeOptions,
    });

    _formFields.push_back(FormField{
        "owm_key", "OpenWeatherMap API Key", false, VALIDATION_NONE,
        PREF_STRING, { .str_pref = cfg.owmApiKey }, nullptr, 0,
    });

    _formFields.push_back(FormField{
        "owm_city", "OWM City (e.g. Warsaw,PL)", false, VALIDATION_NONE,
        PREF_STRING, { .str_pref = cfg.owmCity }, nullptr, 0,
    });

    // NOTE: After applyFormBody() runs, the app reads the buffers back into
    // numeric fields in WhispererApp::onConfigSaved (called just before
    // esp_restart). Because the AP manager reboots immediately on save,
    // the cheapest path is to parse these buffers in
    // WhispererPreferences::putPreferences() before commit. We do that
    // indirectly by teaching the app to refresh the numeric fields from
    // the buffers right before persistence. See whisperer_app.cpp.
}
