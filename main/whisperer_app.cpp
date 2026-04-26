#include "whisperer_app.h"
#include "version.h"
#include "vfd_hardware_map.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

// --- WiFi activity LED -------------------------------------------------------

static esp_timer_handle_t s_ledOffTimer = nullptr;

static void ledOffCallback(void*) {
    gpio_set_level(static_cast<gpio_num_t>(LED_GPIO), 0);
}

static void ledWifiEventHandler(void*, esp_event_base_t, int32_t, void*) {
    if (LED_GPIO < 0 || !s_ledOffTimer) return;
    gpio_set_level(static_cast<gpio_num_t>(LED_GPIO), 1);
    esp_timer_stop(s_ledOffTimer);
    esp_timer_start_once(s_ledOffTimer, 80 * 1000ULL);
}

// --- Scene playlist ----------------------------------------------------------

static char s_quoteBuffer[48] = "HELLO";

static float whisperer_refreshQuote() {
    const char* q = WhispererApp::getInstance().getQuoteManager().pickQuote();
    if (q && *q) {
        std::strncpy(s_quoteBuffer, q, sizeof(s_quoteBuffer) - 1);
        s_quoteBuffer[sizeof(s_quoteBuffer) - 1] = '\0';
    }
    return UNSET_VALUE;
}

static float whisperer_timeDataStub() { return UNSET_VALUE; }

static const DisplayScene s_scenePlaylist[] = {
    { "Time",  " %H.%M.%S",  SLOT_MACHINE, true,  true,  8000, 150, 40, &whisperer_timeDataStub },
    { "Date",  " %b %d",     MATRIX,       false, false, 4000, 250, 40, &whisperer_timeDataStub },
    { "Time",  " %H-%M-%S",  SLOT_MACHINE, false, true,  8000, 150, 40, &whisperer_timeDataStub },
    { "Year",  "%m/%d/%Y",   STATIC_TEXT,  false, false, 3000,   0,  0, &whisperer_timeDataStub },
    { "Time",  " %H-%M-%S",  SLOT_MACHINE, false, true,  8000, 150, 40, &whisperer_timeDataStub },
    { "Year",  "%Y-%m-%d",  STATIC_TEXT,  false, false, 3000,   0,  0, &whisperer_timeDataStub },
    // { "Quote", s_quoteBuffer, SCROLLING,   false, false, 7000, 250,  0, &whisperer_refreshQuote },
};
static const int s_numScenes = sizeof(s_scenePlaylist) / sizeof(DisplayScene);

// --- Singleton ---------------------------------------------------------------

WhispererApp& WhispererApp::getInstance() {
    static WhispererApp instance;
    return instance;
}

WhispererApp::~WhispererApp() = default;

WhispererApp::WhispererApp()
    : _display(PT6315_GPIO_SCK, PT6315_GPIO_CS, PT6315_GPIO_MOSI, PT6315_SPI_HOST),
      _appPrefs(),
      _apManagerConcrete(_appPrefs),
      _tof(TOF_I2C_PORT, TOF_I2C_SDA, TOF_I2C_SCL) {
    _displayManager = std::make_unique<DisplayManager>(_display);
    _prefs     = &_appPrefs;
    _apManager = &_apManagerConcrete;
}

// --- Hardware & lifecycle ----------------------------------------------------

void WhispererApp::setupHardware() {
    if (AP_TRIGGER_GPIO >= 0) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << AP_TRIGGER_GPIO;
        io.mode         = GPIO_MODE_INPUT;
        io.pull_up_en   = GPIO_PULLUP_ENABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&io);
        LOGINF("AP trigger on GPIO %d (hold 3 s for AP mode)", AP_TRIGGER_GPIO);
    }

    if (LED_GPIO >= 0) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << LED_GPIO;
        io.mode         = GPIO_MODE_OUTPUT;
        io.pull_up_en   = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&io);
        gpio_set_level(static_cast<gpio_num_t>(LED_GPIO), 0);

        esp_timer_create_args_t ta = {};
        ta.callback = ledOffCallback;
        ta.name     = "led_off";
        esp_timer_create(&ta, &s_ledOffTimer);

        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ledWifiEventHandler, nullptr);
        esp_event_handler_register(IP_EVENT,   ESP_EVENT_ANY_ID, ledWifiEventHandler, nullptr);
        LOGINF("WiFi activity LED on GPIO %d", LED_GPIO);
    }

    _moodLeds.init(MOOD_LED_GPIO, MOOD_LED_COUNT);

    _displayManager->begin();
    _display.setBrightness(static_cast<uint8_t>(_appPrefs.config.displayBrightness));

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
    whisperer_refreshQuote();

    if (_sceneManager) _sceneManager->setup(s_scenePlaylist, s_numScenes);

    _tofAvailable = _tof.init();
    if (_tofAvailable) {
        _tof.startContinuous();
        LOGINF("VL53L0X distance sensor started");
    } else {
        LOGINF("VL53L0X not available — distance triggering disabled");
    }

    LOGINF("MoodWhisperer ready");
}

void WhispererApp::loop() {
    BaseNtpClockApp::loop();

    // AP trigger (BOOT button, hold 3 s).
    if (AP_TRIGGER_GPIO >= 0 && _fsmManager && !_fsmManager->isInState("AP_MODE")) {
        if (gpio_get_level((gpio_num_t)AP_TRIGGER_GPIO) == 0) {
            if (_apTriggerHeldSinceUs == 0)
                _apTriggerHeldSinceUs = esp_timer_get_time();
            else if (esp_timer_get_time() - _apTriggerHeldSinceUs >= 3'000'000) {
                LOGINF("AP trigger held 3 s — requesting AP mode");
                _apTriggerHeldSinceUs = 0;
                _fsmManager->requestApMode();
            }
        } else {
            _apTriggerHeldSinceUs = 0;
        }
    }

    // Detect end of distance-triggered quote and return to clock.
    if (_inQuoteMode && !_displayManager->isAnimationRunning()) {
        _inQuoteMode = false;
        _moodLeds.startFadeOut();
        _lastQuoteTriggerMs = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        LOGINF("Quote done — back to clock (cooldown %lld s)", QUOTE_COOLDOWN_MS / 1000);
    }
}

// --- Distance sensor ---------------------------------------------------------

void WhispererApp::pollDistance() {
    if (!_tofAvailable) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }
    int mm = _tof.readRangeMm();
    onDistanceReading(mm);
    // readRangeMm already blocked ~33 ms waiting for the sample; a small
    // extra delay keeps worst-case poll rate around 10 Hz.
    vTaskDelay(pdMS_TO_TICKS(67));
}

void WhispererApp::onDistanceReading(int mm) {
    if (mm <= 0 || mm > 2000) return;

    if (_inQuoteMode) return;

    if (_lastStableDistanceMm < 0) {
        _lastStableDistanceMm = mm;
        return;
    }

    int64_t nowMs = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool inCooldown = (_lastQuoteTriggerMs > 0 &&
                       (nowMs - _lastQuoteTriggerMs) < QUOTE_COOLDOWN_MS);

    int delta = std::abs(mm - _lastStableDistanceMm);

    if (!inCooldown && delta >= DISTANCE_CHANGE_THRESHOLD_MM) {
        triggerDistanceQuote(mm);
        _lastStableDistanceMm = mm;
    } else {
        // Slow exponential drift so baseline tracks the environment.
        _lastStableDistanceMm = (int)(_lastStableDistanceMm * 0.98f + mm * 0.02f);
    }
}

void WhispererApp::triggerDistanceQuote(int distanceMm) {
    float mood = moodFromDistance(distanceMm);
    LOGINF("ToF %d mm → mood %.2f → quote", distanceMm, mood);

    _moodProvider = std::make_unique<FixedMoodProvider>(mood);
    _quotes       = std::make_unique<QuoteManager>(_moodProvider.get(), nullptr, 0, 10);

    const char* q = _quotes->pickQuote();
    if (!q || !*q) return;
    std::strncpy(s_quoteBuffer, q, sizeof(s_quoteBuffer) - 1);
    s_quoteBuffer[sizeof(s_quoteBuffer) - 1] = '\0';

    _moodLeds.triggerGlow();
    _inQuoteMode = true;
    _displayManager->setAnimation(
        std::make_unique<ScrollingTextAnimation>(s_quoteBuffer, 250, false));
}

// Maps distance in mm to mood in [-1.0, +1.0].
// <  500 mm → -1.0 (very close = gloomy)
// 500–1000 mm → linear -1.0 → 0.0
// 1000–1500 mm → linear 0.0 → +1.0
// > 1500 mm → +1.0 (far away = upbeat)
float WhispererApp::moodFromDistance(int mm) {
    if (mm <= 500)  return -1.0f;
    if (mm >= 1500) return  1.0f;
    if (mm < 1000)  return -1.0f + (mm - 500) / 500.0f;
    return (mm - 1000) / 500.0f;
}

// --- MoodProvider selection --------------------------------------------------

void WhispererApp::refreshMoodProvider() {
    if (std::strcmp(_appPrefs.config.moodSource, "fixed") == 0) {
        float v = static_cast<float>(_appPrefs.config.fixedMoodTimes100) / 100.0f;
        _moodProvider = std::make_unique<FixedMoodProvider>(v);
    } else {
        _moodProvider = std::make_unique<RandomMoodProvider>();
    }
    _quotes = std::make_unique<QuoteManager>(_moodProvider.get(), nullptr, 0, 10);
}

// --- IBaseClock implementations ----------------------------------------------

bool WhispererApp::isOkToRunScenes() const {
    if (_inQuoteMode) return false;
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

    _displayManager->setAnimation(
        std::make_unique<ScrollingTextAnimation>(waiting, 180, false));
    _apManagerConcrete.runBlockingLoop(*_displayManager, waiting, connected);
}
