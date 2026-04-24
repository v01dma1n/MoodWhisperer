// i_animation.h — contract for one animation. Animations compose frames in
// their own private buffer and hand them to the DisplayManager, which
// merges them into the driver's buffer. This keeps animations testable
// independent of hardware.
//
// Mirrors ESP32NTPClock::IAnimation.

#pragma once

#include "i_display_driver.h"

#include <vector>
#include <cstddef>

class IAnimation {
public:
    virtual ~IAnimation() = default;

    // Default setup: record the target driver and size our private buffer
    // to match. Subclasses that need additional state override and call
    // IAnimation::setup() first.
    virtual void setup(IDisplayDriver* display) {
        _display = display;
        _buffer.assign(static_cast<size_t>(_display->getDisplaySize()), 0UL);
    }

    // Advance by one step. Called repeatedly from the main loop until
    // isDone() returns true.
    virtual void update() = 0;

    // True when the animation has nothing more to do. For SCROLLING this
    // can be "never" (returns false forever) if the scroll loops.
    virtual bool isDone() = 0;

    // Latest composed frame, ready for the DisplayManager to flush.
    const std::vector<unsigned long>& getFrame() const { return _buffer; }

protected:
    IDisplayDriver* _display = nullptr;
    std::vector<unsigned long> _buffer;

    // Helper for subclasses: write into the private buffer, not the driver.
    void setChar(int position, char character, bool dot = false) {
        if (position < 0 || static_cast<size_t>(position) >= _buffer.size()) return;
        _buffer[position] = _display->mapAsciiToSegment(character, dot);
    }
};
