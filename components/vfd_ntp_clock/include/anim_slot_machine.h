// anim_slot_machine.h — each cell spins through random chars, then locks.

#pragma once

#include "i_animation.h"

#include <list>
#include <string>
#include <vector>
#include <cstdint>

class SlotMachineAnimation : public IAnimation {
public:
    SlotMachineAnimation(std::string targetText,
                         uint32_t lockDelay = 200,
                         uint32_t spinDelay = 50,
                         bool dotsWithPreviousChar = false);

    void setup(IDisplayDriver* display) override;
    void update() override;
    bool isDone() override;

private:
    std::string _targetText;
    std::string _parsedText;
    std::vector<uint8_t> _dotStates;

    // Indices of cells still spinning, ordered left-to-right so we lock
    // from left to right. std::list because we pop_front() often and
    // the original used a list too.
    std::list<int> _spinningIndices;

    uint32_t _lockDelay;
    uint32_t _spinDelay;
    bool     _dotsWithPreviousChar;

    uint32_t _lastLockTime;
    uint32_t _lastSpinTime;
    bool     _done;

    uint32_t _rngState;   // fast xorshift32 RNG

    char randomChar();
};
