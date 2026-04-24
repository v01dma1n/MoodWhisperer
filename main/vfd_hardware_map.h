// vfd_hardware_map.h — GPIO + host assignments for the Sony VFD wiring.
//
// Pin numbers are taken from the CN803 wiring table in the Sony VFD
// integration README (REMEMBER: Sony silkscreen labels on side A are
// MIRRORED — go by the electrical order).
//
// Change here if porting to a different ESP32 board or wiring choice.

#pragma once

#include "driver/spi_common.h"

// Sony HT-CT550W CN803 <-> ESP32-WROOM wiring.
constexpr int PT6315_GPIO_SCK  = 18;  // FL_CLK  — VSPI SCK
constexpr int PT6315_GPIO_MOSI = 23;  // FL_DATA — VSPI MOSI
constexpr int PT6315_GPIO_CS   =  5;  // FL_CS   — VSPI SS

// VSPI on the classic ESP32 = SPI3_HOST in IDF >= 4.4.
constexpr spi_host_device_t PT6315_SPI_HOST = SPI3_HOST;

// Optional touch-sensor pad that the user mentioned in the spec.
// Configured as a simple GPIO input here; swap for `touch_pad_t` if using
// the ESP32's dedicated touch peripheral. -1 disables the feature.
constexpr int AP_TRIGGER_GPIO = -1;
