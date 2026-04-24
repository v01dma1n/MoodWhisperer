// anim_time.h — lightweight millis()-equivalent for IDF.
//
// The Arduino animations all use millis() for timing. This header gives
// us a drop-in replacement so the animation code reads the same.

#pragma once

#include "esp_timer.h"
#include <cstdint>

inline uint32_t app_millis() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
