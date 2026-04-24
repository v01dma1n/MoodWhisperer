#include "base_ntp_clock_app.h"

#include "base_preferences.h"
#include "base_access_point_manager.h"
#include "clock_fsm_manager.h"
#include "scene_manager.h"
#include "boot_manager.h"

#include "logging.h"

BaseNtpClockApp::BaseNtpClockApp()
    : _prefs(nullptr), _apManager(nullptr) {}

BaseNtpClockApp::~BaseNtpClockApp() = default;

void BaseNtpClockApp::setup() {
    // Preferences first, so setupHardware() sees the saved brightness /
    // log-level / etc.
    if (_prefs) {
        _prefs->setup();
        _prefs->getPreferences();
        _prefs->dumpPreferences();

        // Propagate loaded log level to the global.
        extern AppLogLevel g_appLogLevel;
        g_appLogLevel = _prefs->getConfig().logLevel;
    }

    // App-specific hardware init (SPI, display, I2C...).
    setupHardware();

    // Engine managers. The FSM and scene manager need a reference back
    // to this->IBaseClock, which is available because we are already
    // inside a concrete subclass at this point in the chain.
    _bootManager  = std::make_unique<BootManager>(*this);
    _fsmManager   = std::make_unique<ClockFsmManager>(*this);
    _sceneManager = std::make_unique<SceneManager>(*this);

    // If the user double-tapped reset, skip the happy path and jump to
    // the captive portal before even trying to connect to WiFi.
    if (_bootManager->checkForForceAPMode()) {
        _fsmManager->requestApMode();
    }

    _fsmManager->setup();

    LOGINF("Base clock engine setup complete");
}

void BaseNtpClockApp::loop() {
    if (_fsmManager)   _fsmManager->update();
    if (_sceneManager) _sceneManager->update();

    // After a few seconds of stable RUNNING_NORMAL, clear the "recent
    // boot" flag so the next isolated reset won't be seen as the 2nd of
    // a pair. Using a static to keep boot_manager.h dependency-free.
    static int64_t s_markedAtMs = 0;
    if (_bootManager && _fsmManager &&
        _fsmManager->isInState("RUNNING_NORMAL") &&
        s_markedAtMs == 0) {
        s_markedAtMs = 1;  // latch
        _bootManager->markBootStable();
    }
}
