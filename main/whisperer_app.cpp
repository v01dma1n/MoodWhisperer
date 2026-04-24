#include "whisperer_app.h"
#include "version.h"
#include "vfd_hardware_map.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"

#include <cstdio>
#include <cstring>
#include <ctime>

// --- Data getters for scenes that aren't pure time formats ---------------
//
// The DisplayScene callback returns a float. For the quote scene we need
// a *string*, not a float, so the real text comes from a separate static
// buffer refreshed by the data getter and embedded into the scene's
// format string with %s. The getter returns UNSET_VALUE for scenes that
// only care about the side effect.

static char s_quoteBuffer[48] = "HELLO";

static float whisperer_refreshQuote() {
    // Called once when a Quote scene starts; picks a fresh quote and
    // caches it in the static buffer read by the scene's format string.
    const char* q = WhispererApp::getInstance().getQuoteManager().pickQuote();
    if (q && *q) {
        std::strncpy(s_quoteBuffer, q, sizeof(s_quoteBuffer) - 1);
        s_quoteBuffer[sizeof(s_quoteBuffer) - 1] = '\0';
    }
    return UNSET_VALUE;  // signal "no numeric value" to the scene manager
}

static float whisperer_timeDataStub() { return UNSET_VALUE; }

// --- Scene playlist ------------------------------------------------------
//
// The display is 10 cells wide. Time-of-day scenes fit ("HH-MM-SS" = 8
// chars). Dates and quotes use scrolling / matrix animations so longer
// strings don't get truncated.

// We can't use %s in renderCurrentSceneText's strftime path (it triggers
// the time-format heuristic). Instead, we use a plain format for quotes
// and let snprintf handle it. The trick: since snprintf wants an
// argument that matches the format spec, and our data getter returns
// float, we use %s AND prime the buffer — but renderCurrentSceneText
// calls snprintf(fmt, v). That means the quote scene must bake the
// quote *into* the format_string dynamically.
//
// Simpler approach: for the quote scene, `format_string` is just "%s"
// pointed at a mutable buffer. We'll update the "scene" dynamically
// between playlist cycles — but the engine signature is const.
//
// Cleanest workaround: pre-render the quote into s_quoteBuffer and have
// the scene format string be the raw buffer pointer. The scene manager
// will snprintf(s_quoteBuffer, v) — snprintf with no % specifiers
// copies the string verbatim. That's exactly what we want.
//
// The refresh getter returns UNSET_VALUE so the "dashes placeholder"
// path in scene_manager only activates if the buffer is empty.
//
// We accept this small hack because it avoids touching the engine's
// scene signature (that would ripple through every other app).

static const DisplayScene s_scenePlaylist[] = {
    // Time — live-updated, slot-machine on entry, seconds-updated static frames after.
    { "Time",    " %H-%M-%S",   SLOT_MACHINE, false, true,  8000, 150, 40, &whisperer_timeDataStub },

    // Date — matrix reveal.
    { "Date",    " %b %d",       MATRIX,       false, false, 4000, 250, 40, &whisperer_timeDataStub },

    // Quote — scrolls in. format_string points at the pre-filled buffer,
    // whose contents refreshQuote() updated when the scene started.
    // scene manager's snprintf(fmt, v) with no %-spec just copies the buffer.
    { "Quote",   s_quoteBuffer,  SCROLLING,    false, false, 7000, 180,  0, &whisperer_refreshQuote },

    // Year — bright static text.
    { "Year",    "     %Y",     STATIC_TEXT,  false, false, 3000,   0,  0, &whisperer_timeDataStub },
};

static const int s_numScenes = sizeof(s_scenePlaylist) / sizeof(DisplayScene);

// --- Singleton boilerplate ---------------------------------------------------

WhispererApp& WhispererApp::getInstance() {
    static WhispererApp instance;
    return instance;
}

WhispererApp::~WhispererApp() = default;

WhispererApp::WhispererApp()
    : _display(PT6315_GPIO_SCK, PT6315_GPIO_CS, PT6315_GPIO_MOSI,
               PT6315_SPI_HOST),
      _appPrefs(),
      _apManagerConcrete(_appPrefs) {
    _displayManager = std::make_unique<DisplayManager>(_display);

    _prefs     = &_appPrefs;
    _apManager = &_apManagerConcrete;
}

// --- Hardware & lifecycle ---------------------------------------------------

void WhispererApp::setupHardware() {
    _displayManager->begin();
    _display.setBrightness(static_cast<uint8_t>(_appPrefs.config.displayBrightness));

    // Give the user immediate visual feedback that the board is alive.
    if (_appPrefs.config.showStartupAnimation) {
        _displayManager->setAnimation(
            std::make_unique<ScrollingTextAnimation>(APP_GREETING, 150, false));
    } else {
        _displayManager->setAnimation(
            std::make_unique<StaticTextAnimation>(APP_GREETING));
    }
}

void WhispererApp::setup() {
    BaseNtpClockApp::setup();
    refreshMoodProvider();

    // Prime the quote buffer so the very first render isn't blank while
    // the scrolling animation starts.
    whisperer_refreshQuote();

    if (_sceneManager) _sceneManager->setup(s_scenePlaylist, s_numScenes);

    LOGINF("VFDWhisperer ready");
}

void WhispererApp::loop() {
    BaseNtpClockApp::loop();
    // Nothing VFDWhisperer-specific per tick; the engine handles the
    // FSM + scene manager and the quote refresh happens at scene start.
}

// --- MoodProvider selection -------------------------------------------------

void WhispererApp::refreshMoodProvider() {
    if (std::strcmp(_appPrefs.config.moodSource, "fixed") == 0) {
        float v = static_cast<float>(_appPrefs.config.fixedMoodTimes100) / 100.0f;
        _moodProvider = std::make_unique<FixedMoodProvider>(v);
    } else {
        _moodProvider = std::make_unique<RandomMoodProvider>();
    }
    _quotes = std::make_unique<QuoteManager>(_moodProvider.get());
}

// --- IBaseClock implementations --------------------------------------------

bool WhispererApp::isOkToRunScenes() const {
    return _fsmManager && _fsmManager->isInState("RUNNING_NORMAL");
}

void WhispererApp::formatTime(char* txt, unsigned txt_size,
                              const char* format, time_t now) {
    struct tm ti;
    localtime_r(&now, &ti);
    strftime(txt, txt_size, format, &ti);
}

void WhispererApp::activateAccessPoint() {
    _apManagerConcrete.setup(APP_HOST_NAME);

    char waiting[64];
    snprintf(waiting, sizeof(waiting), "SETUP MODE - JOIN %s", APP_HOST_NAME);

    char connected[] = "GOTO 192.168.4.1";

    // Swap to a scrolling "setup mode" banner for the entire AP session.
    _displayManager->setAnimation(
        std::make_unique<ScrollingTextAnimation>(waiting, 180, false));

    _apManagerConcrete.runBlockingLoop(*_displayManager, waiting, connected);
}
