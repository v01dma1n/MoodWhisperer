// display_manager.h — owns the current animation and pushes frames to the driver.
//
// This is the layer the app talks to when it wants to change what's on
// the glass. Hand it an IAnimation subclass via setAnimation(); the
// manager advances the animation each call to update(), copies its
// private buffer into the driver buffer, and flushes.
//
// Mirrors ESP32NTPClock::DisplayManager.

#pragma once

#include "i_display_driver.h"
#include "i_animation.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <memory>
#include <vector>

class DisplayManager {
public:
    explicit DisplayManager(IDisplayDriver& display);

    // One-time hardware init (forwarded to the driver).
    void begin();

    // Take ownership of a new animation. Any previous animation is dropped.
    void setAnimation(std::unique_ptr<IAnimation> animation);

    // Advance the current animation by one step and flush to the driver.
    void update();

    // True while an animation is installed and hasn't reported done.
    bool isAnimationRunning() const;

    // Raw accessor — scene manager peeks for isDone()/type checks.
    IAnimation* getCurrentAnimation() const { return _currentAnimation.get(); }

    // Direct access for emergency "blank the screen" calls.
    IDisplayDriver& getDriver() { return _display; }

private:
    IDisplayDriver&              _display;
    std::unique_ptr<IAnimation>  _currentAnimation;

    // Protects _currentAnimation and _lastFrame* against concurrent access
    // between the DisplayTask (which calls update()) and the AppTask (which
    // calls setAnimation()). Declared mutable so isAnimationRunning() const
    // can also lock it.
    mutable SemaphoreHandle_t    _mutex = nullptr;

    // Cache of the last frame we actually pushed to the driver. `update()`
    // compares against this and suppresses redundant SPI bursts, which
    // matters a lot for animations that hold a steady-state frame between
    // ticks (StaticText, the locked tail of SlotMachine, etc.).
    std::vector<unsigned long>   _lastFrame;
    bool                         _lastFrameValid = false;
};
