#include "mood_leds.h"
#include "logging.h"

#include <algorithm>
#include <cmath>

// Warm amber/orange that mimics incandescent vacuum tube glow.
// At full brightness: R=255, G=80, B=5.
static constexpr uint8_t GLOW_R = 255;
static constexpr uint8_t GLOW_G =  80;
static constexpr uint8_t GLOW_B =   5;

bool MoodLeds::init(int gpio, int count) {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        LOGERR("MoodLeds: mutex create failed");
        return false;
    }

    _count = count;

    led_strip_config_t cfg = {};
    cfg.strip_gpio_num   = gpio;
    cfg.max_leds         = count;
    cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB;
    cfg.led_model        = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt = {};
    rmt.resolution_hz   = 10 * 1000 * 1000;  // 10 MHz

    esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt, &_strip);
    if (err != ESP_OK) {
        LOGERR("MoodLeds init failed: %d", err);
        return false;
    }
    led_strip_clear(_strip);
    led_strip_refresh(_strip);
    LOGINF("MoodLeds: %d LED(s) on GPIO %d", count, gpio);
    return true;
}

void MoodLeds::triggerGlow() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_state == State::IDLE || _state == State::FADE_OUT) {
        _state = State::FADE_IN;
    }
    xSemaphoreGive(_mutex);
}

void MoodLeds::startFadeOut() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_state == State::LIT || _state == State::FADE_IN) {
        _state = State::FADE_OUT;
    }
    xSemaphoreGive(_mutex);
}

void MoodLeds::update() {
    if (!_strip) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool apply = false;
    switch (_state) {
    case State::IDLE:
    case State::LIT:
    case State::WARN:
        break;

    case State::FADE_IN:
        _brightness = std::min(MAX_BRIGHTNESS, _brightness + FADE_IN_STEP);
        if (_brightness >= MAX_BRIGHTNESS) _state = State::LIT;
        apply = true;
        break;

    case State::FADE_OUT:
        _brightness = std::max(0.0f, _brightness - FADE_OUT_STEP);
        if (_brightness <= 0.0f) _state = State::IDLE;
        apply = true;
        break;

    case State::OVERLOAD_RAMP:
        _brightness = std::min(THROB_MIN, _brightness + OVERLOAD_RAMP_STEP);
        if (_brightness >= THROB_MIN) {
            _state     = State::OVERLOAD_THROB;
            _throbTick = 0;
        }
        apply = true;
        break;

    case State::OVERLOAD_THROB:
        ++_throbTick;
        // ~1.5 Hz sine at 50 Hz update rate (period ≈ 33 ticks)
        _brightness = THROB_MIN + (THROB_MAX - THROB_MIN) *
                      (0.5f + 0.5f * sinf(_throbTick * (2.0f * 3.14159f / 33.0f)));
        apply = true;
        break;

    case State::GEIGER_BREATHE:
        _geigerPhase += 2.0f * 3.14159f * _geigerFreqHz / UPDATE_RATE_HZ;
        if (_geigerPhase > 2.0f * 3.14159f) _geigerPhase -= 2.0f * 3.14159f;
        _brightness = MAX_BRIGHTNESS * (0.5f + 0.5f * sinf(_geigerPhase));
        apply = true;
        break;
    }
    if (apply) applyBrightness();
    xSemaphoreGive(_mutex);
}

bool MoodLeds::isIdle() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool v = (_state == State::IDLE);
    xSemaphoreGive(_mutex);
    return v;
}

bool MoodLeds::isFullyLit() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool v = (_state == State::LIT);
    xSemaphoreGive(_mutex);
    return v;
}

bool MoodLeds::isOverloading() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool v = (_state == State::OVERLOAD_THROB);
    xSemaphoreGive(_mutex);
    return v;
}

bool MoodLeds::isGeigerBreathing() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool v = (_state == State::GEIGER_BREATHE);
    xSemaphoreGive(_mutex);
    return v;
}

void MoodLeds::startGeiger(float freqHz) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _geigerFreqHz = freqHz;
    _geigerPhase  = 3.14159f * 1.5f;  // start at sine trough → breathe up from dark
    _state        = State::GEIGER_BREATHE;
    xSemaphoreGive(_mutex);
}

void MoodLeds::setGeigerFrequency(float freqHz) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _geigerFreqHz = freqHz;  // phase kept for smooth mid-breathe update
    xSemaphoreGive(_mutex);
}

void MoodLeds::setFull() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _brightness = 1.0f;
    _state      = State::LIT;
    applyBrightness();
    xSemaphoreGive(_mutex);
}

void MoodLeds::setWarn() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _brightness = WARN_BRIGHTNESS;
    _state      = State::WARN;
    applyBrightness();
    xSemaphoreGive(_mutex);
}

void MoodLeds::startOverload() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _state = State::OVERLOAD_RAMP;
    xSemaphoreGive(_mutex);
}

void MoodLeds::clearImmediate() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _brightness = 0.0f;
    _state      = State::IDLE;
    applyBrightness();
    xSemaphoreGive(_mutex);
}

void MoodLeds::applyBrightness() {
    if (!_strip) return;
    auto r = (uint8_t)(GLOW_R * _brightness);
    auto g = (uint8_t)(GLOW_G * _brightness);
    auto b = (uint8_t)(GLOW_B * _brightness);
    for (int i = 0; i < _count; ++i) {
        led_strip_set_pixel(_strip, i, r, g, b);
    }
    led_strip_refresh(_strip);
}
