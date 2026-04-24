#include "anim_slot_machine.h"
#include "anim_utils.h"
#include "anim_time.h"

#include "esp_random.h"

SlotMachineAnimation::SlotMachineAnimation(std::string targetText,
                                           uint32_t lockDelay,
                                           uint32_t spinDelay,
                                           bool dotsWithPreviousChar)
    : _targetText(std::move(targetText)),
      _lockDelay(lockDelay),
      _spinDelay(spinDelay),
      _dotsWithPreviousChar(dotsWithPreviousChar),
      _lastLockTime(0),
      _lastSpinTime(0),
      _done(false),
      _rngState(esp_random() | 1u) {}

void SlotMachineAnimation::setup(IDisplayDriver* display) {
    IAnimation::setup(display);
    parseTextAndDots(_targetText, _dotsWithPreviousChar,
                     _parsedText, _dotStates);

    // Pad/truncate to the display width so each cell maps to one target.
    int cells = _display->getDisplaySize();
    if (static_cast<int>(_parsedText.size()) < cells) {
        int pad = cells - _parsedText.size();
        _parsedText.append(pad, ' ');
        _dotStates.insert(_dotStates.end(), pad, 0);
    } else if (static_cast<int>(_parsedText.size()) > cells) {
        _parsedText.resize(cells);
        _dotStates.resize(cells);
    }

    _spinningIndices.clear();
    for (int i = 0; i < cells; ++i) _spinningIndices.push_back(i);

    uint32_t now = app_millis();
    _lastLockTime = now;
    _lastSpinTime = now;
    _done = false;
}

char SlotMachineAnimation::randomChar() {
    // xorshift32 — plenty random for this.
    uint32_t x = _rngState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    _rngState = x;

    // Printable ASCII between 0x30 ('0') and 0x5A ('Z') gives digits
    // + uppercase letters, exactly the glyphs a VFD font reliably has.
    return static_cast<char>(0x30 + (x % ('Z' - '0' + 1)));
}

void SlotMachineAnimation::update() {
    if (_done) return;

    uint32_t now = app_millis();

    // Periodically lock the next spinning cell to its target character.
    if (now - _lastLockTime >= _lockDelay && !_spinningIndices.empty()) {
        int idx = _spinningIndices.front();
        _spinningIndices.pop_front();

        setChar(idx,
                _parsedText[idx],
                _dotStates[idx] != 0);

        _lastLockTime = now;
    }

    // All other (still spinning) cells pick fresh random characters.
    if (now - _lastSpinTime >= _spinDelay) {
        for (int idx : _spinningIndices) {
            setChar(idx, randomChar(), false);
        }
        _lastSpinTime = now;
    }

    if (_spinningIndices.empty()) {
        // Final frame: make sure the target (with dots) is fully drawn.
        for (size_t i = 0; i < _parsedText.size(); ++i) {
            setChar(static_cast<int>(i), _parsedText[i], _dotStates[i] != 0);
        }
        _done = true;
    }
}

bool SlotMachineAnimation::isDone() { return _done; }
