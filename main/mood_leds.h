#pragma once
#include "led_strip.h"
#include <cstdint>

// Two WS2812 LEDs that simulate vacuum-tube warm glow.
// Fades in when a quote is triggered and fades out when it ends.
// Call update() at a steady rate (50 Hz from DisplayTask is fine).

class MoodLeds {
public:
    bool init(int gpio, int count);

    // Classic mode
    void triggerGlow();       // start fade-in
    void startFadeOut();      // start fade-out
    bool isIdle() const;      // true once fade-out reaches zero
    bool isFullyLit() const;  // true once fade-in completes

    // Thermal Overload mode
    void setWarn();           // immediate 30% warning glow (Phase A)
    void startOverload();     // begin ramp → throb sequence (Phase B)
    void clearImmediate();    // immediately off (cooldown entry)
    bool isOverloading() const;  // true once throb is running

    void update();            // advance by one step — call at 50 Hz

private:
    led_strip_handle_t _strip = nullptr;
    int      _count      = 0;
    float    _brightness = 0.0f;
    uint32_t _throbTick  = 0;

    enum class State {
        IDLE, FADE_IN, LIT, FADE_OUT,
        WARN, OVERLOAD_RAMP, OVERLOAD_THROB
    } _state = State::IDLE;

    static constexpr float FADE_IN_STEP      = 1.0f / 100.0f;  // 2 s at 50 Hz
    static constexpr float FADE_OUT_STEP     = 1.0f / 100.0f;  // 2 s at 50 Hz
    static constexpr float MAX_BRIGHTNESS    = 0.5f;
    static constexpr float WARN_BRIGHTNESS   = 0.3f;
    static constexpr float OVERLOAD_RAMP_STEP = 0.02f;         // ~1 s to THROB_MIN
    static constexpr float THROB_MIN         = 0.6f;
    static constexpr float THROB_MAX         = 1.0f;

    void applyBrightness();
};
