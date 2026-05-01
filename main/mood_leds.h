#pragma once
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
    void clearImmediate();    // immediately off (cooldown / Geiger pop)
    bool isOverloading() const;  // true once throb is running

    // Geiger Proximity Pulse mode
    void startGeiger(float freqHz = 0.5f); // begin breathing at given frequency
    void setGeigerFrequency(float freqHz); // update frequency mid-breathe
    void setFull();                        // instant 100% (far-range quote)
    bool isGeigerBreathing() const;

    void update();            // advance by one step — call at 50 Hz

private:
    led_strip_handle_t    _strip = nullptr;
    mutable SemaphoreHandle_t _mutex = nullptr;
    int      _count        = 0;
    float    _brightness   = 0.0f;
    uint32_t _throbTick    = 0;
    float    _geigerFreqHz = 0.5f;
    float    _geigerPhase  = 0.0f;  // radians

    enum class State {
        IDLE, FADE_IN, LIT, FADE_OUT,
        WARN, OVERLOAD_RAMP, OVERLOAD_THROB,
        GEIGER_BREATHE
    } _state = State::IDLE;

    static constexpr float FADE_IN_STEP      = 1.0f / 100.0f;  // 2 s at 50 Hz
    static constexpr float FADE_OUT_STEP     = 1.0f / 100.0f;  // 2 s at 50 Hz
    static constexpr float MAX_BRIGHTNESS    = 0.5f;
    static constexpr float WARN_BRIGHTNESS   = 0.3f;
    static constexpr float OVERLOAD_RAMP_STEP = 0.02f;         // ~1 s to THROB_MIN
    static constexpr float THROB_MIN         = 0.6f;
    static constexpr float THROB_MAX         = 1.0f;
    static constexpr float UPDATE_RATE_HZ    = 50.0f;

    void applyBrightness();
};
