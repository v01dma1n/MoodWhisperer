// enc_types.h — shared enums and structs used by the engine and apps.
// Mirrors the Arduino-version file of the same name in ESP32NTPClock,
// but with Arduino dependencies (String) removed.
//
// Naming intentionally matches the upstream Arduino project so that the
// IDF port reads the same as the original. Scenes, FormFields, and the
// AppLogLevel enum remain binary-compatible in spirit.

#pragma once

#include <cstdint>
#include <cstddef>

// Marker value emitted by scene data getters when data is not (yet) valid.
// The scene manager substitutes a placeholder when it sees this.
static constexpr float UNSET_VALUE = -999.0f;

// The built-in animation types the engine ships with. Application code picks
// one per scene. Add new types by extending AnimationType *and* the switch
// in scene_manager.cpp.
enum AnimationType {
    STATIC_TEXT,
    SLOT_MACHINE,
    MATRIX,
    SCROLLING
};

// One entry in an application's scene playlist. The scene manager cycles
// through these and, for each one, calls getDataValue() to resolve %-tokens
// in format_string via strftime() (if it looks like a time format) or
// snprintf() (everything else).
struct DisplayScene {
    const char* scene_name;
    const char* format_string;
    AnimationType animation_type;
    bool dots_with_previous;   // "3.14" — dot attaches to previous glyph
    bool isLiveUpdate;         // re-render every tick (e.g. seconds)
    unsigned long duration_ms;
    unsigned long anim_param_1;
    unsigned long anim_param_2;
    float (*getDataValue)();   // may be nullptr for pure time-format scenes
};

// Option entry for dropdown-style preference fields in the captive portal.
struct PrefSelectOption {
    const char* name;   // user-facing label
    const char* value;  // value persisted to NVS
};

// Application-level log verbosity. The engine's LOG* macros consult a
// global g_appLogLevel and suppress lower-severity messages.
enum AppLogLevel {
    APP_LOG_ERROR,
    APP_LOG_INFO,
    APP_LOG_DEBUG
};

// Which kind of form control to render for a given preference.
enum PrefType {
    PREF_NONE,
    PREF_STRING,
    PREF_BOOL,
    PREF_INT,
    PREF_ENUM,
    PREF_SELECT   // dropdown backed by PrefSelectOption[]
};

enum FieldValidation {
    VALIDATION_NONE,
    VALIDATION_IP_ADDRESS,
    VALIDATION_INTEGER,
};

// Description of one preference the captive portal should render.
// The AP manager walks an array of these, renders HTML, and writes back
// through the `pref` union.
struct FormField {
    const char* id;          // HTML name="" — must match NVS key slug
    const char* name;        // human-readable label
    bool isMasked;           // render as type="password"
    FieldValidation validation;
    PrefType prefType;
    union {
        char*    str_pref;   // PREF_STRING / PREF_SELECT
        bool*    bool_pref;  // PREF_BOOL
        int32_t* int_pref;   // PREF_INT
    } pref;
    const PrefSelectOption* select_options;  // only for PREF_SELECT
    int num_select_options;
};
