// anim_matrix.h — "Matrix" rain: columns of noise resolve to the target.

#pragma once

#include "i_animation.h"

#include <string>
#include <vector>
#include <cstdint>

class MatrixAnimation : public IAnimation {
public:
    MatrixAnimation(std::string targetText,
                    uint32_t revealDelay = 200,
                    uint32_t rainDelay   = 50,
                    bool dotsWithPreviousChar = false);

    void setup(IDisplayDriver* display) override;
    void update() override;
    bool isDone() override;

private:
    std::string _targetText;
    bool        _dotsWithPreviousChar;

    std::string          _parsedTargetText;
    std::vector<uint8_t> _dotState;

    uint32_t _revealDelay;
    uint32_t _rainDelay;

    uint32_t _lastRainTime;
    uint32_t _lastRevealTime;
    int      _revealedCount;
    bool     _done;

    uint32_t _rngState;
    char     randomChar();
};
