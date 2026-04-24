// anim_static_text.h — simplest animation: render text once, hold until timeout.

#pragma once

#include "i_animation.h"

#include <string>
#include <cstdint>

class StaticTextAnimation : public IAnimation {
public:
    explicit StaticTextAnimation(std::string text);

    void setup(IDisplayDriver* display) override;
    void update() override;
    bool isDone() override;

private:
    std::string _text;
    uint32_t    _startTime;
    bool        _rendered;
};
