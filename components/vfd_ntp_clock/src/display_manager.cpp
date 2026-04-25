#include "display_manager.h"

#include <cstring>

DisplayManager::DisplayManager(IDisplayDriver& display)
    : _display(display) {
    _mutex = xSemaphoreCreateMutex();
}

void DisplayManager::begin() {
    _display.begin();
}

void DisplayManager::setAnimation(std::unique_ptr<IAnimation> animation) {
    // setup() before locking — it calls into the driver but doesn't touch
    // the shared fields, and keeping the lock duration short matters.
    if (animation) animation->setup(&_display);

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _currentAnimation = std::move(animation);
    _lastFrameValid   = false;
    xSemaphoreGive(_mutex);
}

void DisplayManager::update() {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (!_currentAnimation) {
        xSemaphoreGive(_mutex);
        return;
    }
    _currentAnimation->update();

    const auto& frame = _currentAnimation->getFrame();

    // Only push to the hardware if the frame actually changed.
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

    xSemaphoreGive(_mutex);
}

bool DisplayManager::isAnimationRunning() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool running = _currentAnimation && !_currentAnimation->isDone();
    xSemaphoreGive(_mutex);
    return running;
}
