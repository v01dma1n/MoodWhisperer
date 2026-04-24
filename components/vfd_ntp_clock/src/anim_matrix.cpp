#include "anim_matrix.h"
#include "anim_utils.h"
#include "anim_time.h"

#include "esp_random.h"

MatrixAnimation::MatrixAnimation(std::string targetText,
                                 uint32_t revealDelay,
                                 uint32_t rainDelay,
                                 bool dotsWithPreviousChar)
    : _targetText(std::move(targetText)),
      _dotsWithPreviousChar(dotsWithPreviousChar),
      _revealDelay(revealDelay),
      _rainDelay(rainDelay),
      _lastRainTime(0),
      _lastRevealTime(0),
      _revealedCount(0),
      _done(false),
      _rngState(esp_random() | 1u) {}

void MatrixAnimation::setup(IDisplayDriver* display) {
    IAnimation::setup(display);
    parseTextAndDots(_targetText, _dotsWithPreviousChar,
                     _parsedTargetText, _dotState);

    int cells = _display->getDisplaySize();
    if (static_cast<int>(_parsedTargetText.size()) < cells) {
        int pad = cells - _parsedTargetText.size();
        _parsedTargetText.append(pad, ' ');
        _dotState.insert(_dotState.end(), pad, 0);
    } else if (static_cast<int>(_parsedTargetText.size()) > cells) {
        _parsedTargetText.resize(cells);
        _dotState.resize(cells);
    }

    _revealedCount  = 0;
    uint32_t now    = app_millis();
    _lastRainTime   = now;
    _lastRevealTime = now;
    _done = false;
}

char MatrixAnimation::randomChar() {
    uint32_t x = _rngState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    _rngState = x;
    return static_cast<char>(0x30 + (x % ('Z' - '0' + 1)));
}

void MatrixAnimation::update() {
    if (_done) return;

    uint32_t now = app_millis();

    // Reveal one more cell periodically (left-to-right).
    if (now - _lastRevealTime >= _revealDelay) {
        if (_revealedCount < static_cast<int>(_parsedTargetText.size())) {
            ++_revealedCount;
        }
        _lastRevealTime = now;
    }

    // Update every cell every rainDelay: revealed cells show target,
    // unrevealed cells show fresh noise.
    if (now - _lastRainTime >= _rainDelay) {
        for (int i = 0; i < static_cast<int>(_parsedTargetText.size()); ++i) {
            if (i < _revealedCount) {
                setChar(i, _parsedTargetText[i], _dotState[i] != 0);
            } else {
                setChar(i, randomChar(), false);
            }
        }
        _lastRainTime = now;
    }

    if (_revealedCount >= static_cast<int>(_parsedTargetText.size())) {
        // Lock the final image.
        for (int i = 0; i < static_cast<int>(_parsedTargetText.size()); ++i) {
            setChar(i, _parsedTargetText[i], _dotState[i] != 0);
        }
        _done = true;
    }
}

bool MatrixAnimation::isDone() { return _done; }
