#include "display_manager.h"

#include <cstring>

DisplayManager::DisplayManager(IDisplayDriver& display) : _display(display) {}

void DisplayManager::begin() {
    _display.begin();
}

void DisplayManager::setAnimation(std::unique_ptr<IAnimation> animation) {
    _currentAnimation = std::move(animation);
    if (_currentAnimation) {
        _currentAnimation->setup(&_display);
    }
    // Force the first push after a new animation is installed by
    // invalidating the cached frame. unique_ptr swap alone isn't enough
    // because the animation's initial frame might happen to equal the
    // previous one, which would otherwise be suppressed below.
    _lastFrameValid = false;
}

void DisplayManager::update() {
    if (!_currentAnimation) return;
    _currentAnimation->update();

    const auto& frame = _currentAnimation->getFrame();

    // Only push to the hardware if the frame actually changed. Animations
    // are steady-state between ticks (e.g. static text, locked cells in
    // slot-machine) and burning 10 SPI bursts per 10 ms both wastes CPU
    // and starves cross-core IPC paths the SPI driver uses, which at one
    // point was crashing the idle task on the other core.
    bool changed = !_lastFrameValid ||
                   _lastFrame.size() != frame.size() ||
                   std::memcmp(_lastFrame.data(), frame.data(),
                               frame.size() * sizeof(unsigned long)) != 0;

    if (changed) {
        _display.setBuffer(frame);
        _display.writeDisplay();
        _lastFrame      = frame;
        _lastFrameValid = true;
    }
}

bool DisplayManager::isAnimationRunning() const {
    return _currentAnimation && !_currentAnimation->isDone();
}
