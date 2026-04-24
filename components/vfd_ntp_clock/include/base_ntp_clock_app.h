// base_ntp_clock_app.h — the generic "clock engine" app subclasses inherit from.
//
// Subclasses override the hardware-specific pieces (setupHardware,
// activateAccessPoint, formatTime, the display/clock accessors) and
// leave everything else to the base class.
//
// Lifecycle:
//   BaseNtpClockApp::setup()
//     -> _prefs->setup() + getPreferences()
//     -> setupHardware()                       (app provides)
//     -> _bootManager->checkForForceAPMode()   (double reset?)
//     -> _fsmManager->setup()                  (STARTUP_ANIM -> WIFI -> NTP -> RUN)
//
//   BaseNtpClockApp::loop()
//     -> _fsmManager->update()
//     -> _sceneManager->update()

#pragma once

#include "i_base_clock.h"

#include <memory>

class BasePreferences;
class BaseAccessPointManager;
class ClockFsmManager;
class SceneManager;
class BootManager;

class BaseNtpClockApp : public virtual IBaseClock {
public:
    virtual void setup();
    virtual void loop();

protected:
    BaseNtpClockApp();
    virtual ~BaseNtpClockApp();

    // App must implement: bring up SPI, I2C, the display driver, etc.
    // Called after preferences are loaded so the app can honor saved
    // brightness/theme values.
    virtual void setupHardware() = 0;

    // Pointers to app-owned objects. The subclass wires these up in its
    // constructor; the base class only uses them through these pointers.
    BasePreferences*        _prefs;
    BaseAccessPointManager* _apManager;

    // Engine-owned objects.
    std::unique_ptr<ClockFsmManager> _fsmManager;
    std::unique_ptr<SceneManager>    _sceneManager;
    std::unique_ptr<BootManager>     _bootManager;
};
