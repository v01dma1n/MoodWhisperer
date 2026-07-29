// lgl_vfd_studio.h — low-level driver for the LGL Studio VFD display module.
//
// 5x7 dot-matrix alphanumeric VFD glass with an on-chip ASCII font, driven
// over a 3-wire bit-banged serial link (CS, CLK, SDI — no MISO, write-only).
// Protocol reverse-engineered from the HarutoHiroki/VFDDisplay Arduino
// library, which targets this exact display family:
// https://github.com/HarutoHiroki/VFDDisplay
//
// Command bytes (CS held low for the duration of each command):
//   0xE0, <n-1>        set active digit count (n = 1..16)
//   0xE4, <0..255>     set brightness
//   0x20+pos, <bytes>  write ASCII bytes into display RAM starting at pos
//   0xE8                latch display RAM to the glass (auto-sent after
//                       every text write; the chip's font ROM renders each
//                       ASCII byte as a 5x7 glyph)
// All bytes are clocked LSB-first, data set up while CLK is low and
// latched by CLK going high (equivalent to SPI Mode 0).
//
// This is a standalone component with no dependency on IDisplayDriver / the
// esp32_ntp_clock engine — it exists for bring-up on a bare ESP32 board
// before this glass is wired into a bigger app.

#pragma once

#include <cstdint>
#include <cstddef>

namespace LglVfdConst {
    constexpr uint8_t CMD_SET_DIGIT_COUNT = 0xE0;
    constexpr uint8_t CMD_SET_BRIGHTNESS  = 0xE4;
    constexpr uint8_t CMD_LATCH           = 0xE8;
    constexpr uint8_t CMD_WRITE_BASE      = 0x20;  // + 0-indexed position

    constexpr int MAX_DIGITS = 16;
}

class LglVfdStudio {
public:
    // cs/clk/sdi are GPIO numbers. numDigits is the character count of the
    // glass (8 or 16 for the known variants of this module).
    LglVfdStudio(int cs_gpio, int clk_gpio, int sdi_gpio, int numDigits);

    // Configures the GPIOs, sends the digit-count command, and sets
    // brightness to max. Safe to call once at startup.
    void begin();

    // Blank every character position.
    void clear();

    // Write a single ASCII character at a 0-indexed position.
    void writeChar(uint8_t position, char c);

    // Write a NUL-terminated ASCII string starting at a 0-indexed position.
    // Does not pad or wrap — the caller supplies exactly the characters it
    // wants shown.
    void writeString(uint8_t position, const char* str);

    // 0 (dim) - 255 (max). The chip's native brightness scale.
    void setBrightness(uint8_t brightness);

    int getDigitCount() const { return _numDigits; }

private:
    void bitBangByte(uint8_t data);     // LSB-first; caller holds CS low
    void beginCommand();                // CS low
    void endCommand();                  // CS high + settle delay
    uint8_t digitCountByte() const;

    int _cs;
    int _clk;
    int _sdi;
    int _numDigits;
};
