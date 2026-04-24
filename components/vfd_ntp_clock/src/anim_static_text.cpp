#include "anim_static_text.h"
#include "anim_time.h"

StaticTextAnimation::StaticTextAnimation(std::string text)
    : _text(std::move(text)), _startTime(0), _rendered(false) {}

void StaticTextAnimation::setup(IDisplayDriver* display) {
    IAnimation::setup(display);
    _startTime = app_millis();
    _rendered = false;
}

void StaticTextAnimation::update() {
    if (_rendered) return;

    // Render once into our private buffer and leave it there; isDone()
    // never returns true, so the scene manager yanks us when its timer
    // expires.
    const int cells = _display->getDisplaySize();
    for (int i = 0; i < cells; ++i) {
        char c = (i < static_cast<int>(_text.size())) ? _text[i] : ' ';
        setChar(i, c, /*dot=*/false);
    }
    _rendered = true;
}

bool StaticTextAnimation::isDone() {
    // The scene manager controls how long we stay; we are always "ready
    // to be replaced", never "finished on our own".
    return false;
}
