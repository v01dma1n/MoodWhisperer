#include "anim_scrolling_text.h"
#include "anim_utils.h"
#include "anim_time.h"

ScrollingTextAnimation::ScrollingTextAnimation(std::string text,
                                               uint32_t scrollDelay,
                                               bool dotsWithPreviousChar)
    : _text(std::move(text)),
      _scrollDelay(scrollDelay),
      _lastScrollTime(0),
      _currentPosition(0),
      _dotsWithPreviousChar(dotsWithPreviousChar),
      _done(false) {}

void ScrollingTextAnimation::setup(IDisplayDriver* display) {
    IAnimation::setup(display);
    parseTextAndDots(_text, _dotsWithPreviousChar, _parsedText, _dotStates);

    // Pad with leading spaces so the text scrolls in from the right
    // edge, and trailing spaces so it scrolls out cleanly on the left.
    int cells = _display->getDisplaySize();
    std::string padded(cells, ' ');
    padded += _parsedText;
    padded.append(cells, ' ');
    std::vector<uint8_t> paddedDots(cells, 0);
    paddedDots.insert(paddedDots.end(), _dotStates.begin(), _dotStates.end());
    paddedDots.insert(paddedDots.end(), cells, 0);
    _parsedText = std::move(padded);
    _dotStates  = std::move(paddedDots);

    _currentPosition = 0;
    _lastScrollTime  = app_millis();
    _done = false;
}

void ScrollingTextAnimation::update() {
    if (_done) return;

    uint32_t now = app_millis();
    if (now - _lastScrollTime < _scrollDelay) return;
    _lastScrollTime = now;

    const int cells = _display->getDisplaySize();
    for (int i = 0; i < cells; ++i) {
        int src = _currentPosition + i;
        char c  = (src < static_cast<int>(_parsedText.size())) ? _parsedText[src] : ' ';
        bool d  = (src < static_cast<int>(_dotStates.size()))  ? _dotStates[src]  : 0;
        setChar(i, c, d);
    }

    ++_currentPosition;
    // Done when the last character has passed the left edge (strictly >
    // so the final step renders a blank frame before stopping).
    if (_currentPosition + cells > static_cast<int>(_parsedText.size())) {
        _done = true;
    }
}

bool ScrollingTextAnimation::isDone() {
    return _done;
}
