// i_display_driver.h — the hardware abstraction the engine talks to.
//
// Matches ESP32NTPClock::IDisplayDriver one-for-one, minus Arduino types.
// `unsigned long` is kept over `uint32_t` to preserve the original
// signatures exactly; on ESP32 they are the same width.
//
// Each concrete driver (PT6315, MAX6921, HT16K33, HCS12SS59T, ...) decides
// how to turn a frame buffer of unsigned longs into actual glyphs. A frame
// buffer is one unsigned long per character position. Drivers that need
// multiplexing (MAX6921, PT6315) repeatedly call getFrameData() from a
// high-priority FreeRTOS task, while static drivers (HT16K33) push once
// and forget.

#pragma once

#include <cstdint>
#include <vector>

class IDisplayDriver {
public:
    virtual ~IDisplayDriver() = default;

    // One-time hardware init. Safe to call twice.
    virtual void begin() = 0;

    // Number of character cells the driver exposes to the app. The engine
    // never writes beyond this bound.
    virtual int  getDisplaySize() = 0;

    // 0 = dimmest, 7 = brightest. Drivers map onto their native scale.
    virtual void setBrightness(uint8_t level) = 0;

    // Clear all character cells (does not clear per-digit annunciators).
    virtual void clear() = 0;

    // Render one character at a given 0-indexed cell. `dot` controls the
    // trailing decimal point if the glass has one.
    virtual void setChar(int position, char character, bool dot = false) = 0;

    // Directly set the raw 16-bit segment mask on a cell (engine uses this
    // for low-level animations).
    virtual void setSegments(int position, uint16_t mask) = 0;

    // Toggle just the decimal point at a cell without redrawing the glyph.
    virtual void setDot(int position, bool on) = 0;

    // Convert a single char into the driver's native frame buffer word
    // (with or without dot). Used by animation classes to compose frames
    // off-screen before calling setBuffer().
    virtual unsigned long mapAsciiToSegment(char ascii_char, bool dot) = 0;

    // Replace the whole frame buffer in one shot.
    virtual void setBuffer(const std::vector<unsigned long>& newBuffer) = 0;

    // Convenience: print a C-string starting at cell 0.
    // `dotsWithPreviousChar` routes '.' in the input to the previous cell's
    // decimal point instead of rendering it as a glyph.
    void print(const char* text, bool dotsWithPreviousChar = false);

    // Flush any cached RAM to the hardware. For multiplexed drivers this
    // is a NO-OP (the display task drives the glass).
    virtual void writeDisplay() = 0;

    // Called from the display task at refresh rate for multiplexed drivers.
    // Static drivers can leave the default implementation alone.
    virtual void writeNextDigit() { /* default: do nothing */ }

    // True if this driver needs a background task to continuously refresh.
    virtual bool needsContinuousUpdate() const { return false; }

    // Copy the current frame buffer into `buffer` (caller-sized to
    // getDisplaySize()). Used by the display task to fetch the latest
    // frame from the logic task.
    virtual void getFrameData(unsigned long* buffer) = 0;
};
