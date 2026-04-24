// anim_scrolling_text.h — classic right-to-left marquee scroll.

#pragma once

#include "i_animation.h"

#include <string>
#include <vector>
#include <cstdint>

class ScrollingTextAnimation : public IAnimation {
public:
    ScrollingTextAnimation(std::string text,
                           uint32_t scrollDelay = 250,
                           bool dotsWithPreviousChar = false);

    void setup(IDisplayDriver* display) override;
    void update() override;
    bool isDone() override;

private:
    std::string _text;
    uint32_t    _scrollDelay;
    uint32_t    _lastScrollTime;
    int         _currentPosition;
    bool        _dotsWithPreviousChar;
    bool        _done;

    std::string              _parsedText;
    std::vector<uint8_t>     _dotStates;
};
