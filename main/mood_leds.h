#pragma once
#include "led_strip.h"
#include <cstdint>

// Two WS2812 LEDs that simulate vacuum-tube warm glow.
// Fades in when a quote is triggered and fades out when it ends.
// Call update() at a steady rate (50 Hz from DisplayTask is fine).

class MoodLeds {
public:
    bool init(int gpio, int count);

    void triggerGlow();    // start fade-in (idempotent if already lit)
    void startFadeOut();   // start fade-out (called when quote ends)
    void update();         // advance fade by one step — call at 50 Hz
    bool isIdle() const;   // true once fade-out reaches zero

private:
    led_strip_handle_t _strip = nullptr;
    int   _count = 0;
    float _brightness = 0.0f;

    enum class State { IDLE, FADE_IN, LIT, FADE_OUT } _state = State::IDLE;

    static constexpr float FADE_IN_STEP  = 1.0f / 100.0f;  // 2 s at 50 Hz
    static constexpr float FADE_OUT_STEP = 1.0f / 100.0f;  // 2 s at 50 Hz
    static constexpr float MAX_BRIGHTNESS = 0.5f;


    void applyBrightness();
};
