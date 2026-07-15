#include "whisperer_app.h"
#include "version.h"
#include "vfd_hardware_map.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sntp_client.h"
#include "weather_manager.h"

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

static float whisperer_getTemperature() {
    WeatherData w = WhispererApp::getInstance().getWeather().getWeatherData();
    return w.valid ? w.tempF : UNSET_VALUE;
}

static float whisperer_getHumidity() {
    WeatherData w = WhispererApp::getInstance().getWeather().getWeatherData();
    return w.valid ? static_cast<float>(w.humidity) : UNSET_VALUE;
}

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
    { "Time",  " %H. %M. %S", SLOT_MACHINE, true,  true,  10000, 150, 40, &whisperer_timeDataStub },
    { "Date",  " %b %d",      MATRIX,       false, false, 10000, 300, 150, &whisperer_timeDataStub },
    { "Time",  " %H. %M. %S", SLOT_MACHINE, true,  true,  10000, 150,  40, &whisperer_timeDataStub },
    { "Temp",  "  %.1f F",    MATRIX,       true,  false,  7000, 300, 150, &whisperer_getTemperature },
    { "Time",  " %H. %M. %S", SLOT_MACHINE, true,  true,  10000, 150,  40, &whisperer_timeDataStub },
    { "Hum",   "  %.0f PCT",  MATRIX,       false, false,  7000, 300, 150, &whisperer_getHumidity },
    { "Time",  " %H. %M. %S", SLOT_MACHINE, true,  true,  10000, 150, 40, &whisperer_timeDataStub },
    { "Year",  "%m/%d/%Y",    STATIC_TEXT,  false, false, 10000,   0,  0, &whisperer_timeDataStub },
    { "Time",  " %H. %M. %S", SLOT_MACHINE, true,  true,  10000, 150, 40, &whisperer_timeDataStub },
    { "Year",  "%Y-%m-%d",    STATIC_TEXT,  false, false, 10000,   0,  0, &whisperer_timeDataStub },
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
      _weatherManager(*this),
      _tof(TOF_I2C_PORT, TOF_I2C_SDA, TOF_I2C_SCL),
      _rtc(RTC_I2C_PORT, RTC_I2C_SDA, RTC_I2C_SCL, RTC_I2C_ADDR) {
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
        if (gpio_config(&io) != ESP_OK)
            LOGERR("gpio_config failed for AP trigger GPIO %d", AP_TRIGGER_GPIO);
        LOGINF("AP trigger on GPIO %d (hold 3 s for AP mode)", AP_TRIGGER_GPIO);
    }

    if (LED_GPIO >= 0) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << LED_GPIO;
        io.mode         = GPIO_MODE_OUTPUT;
        io.pull_up_en   = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type    = GPIO_INTR_DISABLE;
        if (gpio_config(&io) != ESP_OK)
            LOGERR("gpio_config failed for LED GPIO %d", LED_GPIO);
        gpio_set_level(static_cast<gpio_num_t>(LED_GPIO), 0);

        esp_timer_create_args_t ta = {};
        ta.callback = ledOffCallback;
        ta.name     = "led_off";
        esp_timer_create(&ta, &s_ledOffTimer);

        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ledWifiEventHandler, nullptr);
        esp_event_handler_register(IP_EVENT,   ESP_EVENT_ANY_ID, ledWifiEventHandler, nullptr);
        LOGINF("WiFi activity LED on GPIO %d", LED_GPIO);
    }

    // DS1307 must init before VL53L0X — it sets the shared I2C bus to 100 kHz.
    _rtcPresent = _rtc.init();
    if (_rtcPresent && _rtc.isRunning()) {
        time_t t = _rtc.readTime();
        if (t != (time_t)-1) {
            struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            _rtcAvailable = true;
            LOGINF("System clock seeded from DS1307");
        }
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
        auto& cfg = _appPrefs.config;
        LOGINF("VL53L0X OK | xtalkRate=%d calib=%s",
               (int)cfg.xtalkRate, cfg.xtalkCalibPending ? "PENDING" : "no");
        if (cfg.xtalkCalibPending) {
            LOGINF("Glass crosstalk calibration requested — running 50 samples "
                   "(ensure no target within 500 mm)");
            uint16_t rate = _tof.performXtalkCalibration();
            cfg.xtalkRate         = (int32_t)rate;
            cfg.xtalkCalibPending = false;
            _appPrefs.putPreferences();
            LOGINF("Crosstalk calibration complete — rate %u saved to NVS", (unsigned)rate);
        }
        // Always write the compensation register — including zero to
        // explicitly disable it. The sensor survives ESP soft reboots
        // with registers intact, so skipping the write here left a
        // previous boot's compensation silently active (max-distance
        // readings with logs claiming compensation was off).
        _tof.applyXtalkCompensation((uint16_t)cfg.xtalkRate);
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

    // Weather polling — WeatherManager gates on isOkToRunScenes() and
    // its own 15-minute interval.
    _weatherManager.update();

    // Write NTP time back to DS1307 once on first sync.
    if (_rtcPresent && !_rtcSynced && timeAvail) {
        _rtc.writeTime(time(nullptr));
        _rtcSynced = true;
    }

    // ---- Classic mode: deferred animation start + LED-gated end ----------------

    // Start the quote animation once the LED reaches full brightness.
    if (_pendingQuote && _moodLeds.isFullyLit()) {
        _pendingQuote = false;
        _inQuoteMode  = true;
        _displayManager->setAnimation(
            std::make_unique<ScrollingTextAnimation>(s_quoteBuffer, 250, false));
    }

    // Scroll finished — begin LED fade-out; keep _inQuoteMode until dark.
    if (_inQuoteMode && !_fadingOut &&
        _thermalPhase == ThermalPhase::NONE &&
        std::strcmp(_appPrefs.config.triggerMode, "geiger") != 0 &&
        !_displayManager->isAnimationRunning()) {
        _fadingOut = true;
        _moodLeds.startFadeOut();
    }

    // Resume scene playback once the LED is completely dark.
    if (_fadingOut && _moodLeds.isIdle()) {
        _fadingOut   = false;
        _inQuoteMode = false;
        _lastQuoteTriggerMs = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        LOGINF("Quote done — back to clock (cooldown %lld s)", QUOTE_COOLDOWN_MS / 1000);
    }

    // ---- Geiger Proximity Pulse mode -------------------------------------------

    const bool isGeiger = (std::strcmp(_appPrefs.config.triggerMode, "geiger") == 0);

    // Start LED breathing once we first enter RUNNING_NORMAL in geiger mode.
    if (isGeiger && !_geigerRunning &&
        _fsmManager && _fsmManager->isInState("RUNNING_NORMAL")) {
        _geigerRunning = true;
        _moodLeds.startGeiger(0.2f);  // frequency updated on first distance reading
        LOGINF("Geiger: LED breathing started");
    }

    // Quote animation ended → resume breathing.
    if (isGeiger && _inQuoteMode && !_displayManager->isAnimationRunning()) {
        _inQuoteMode        = false;
        _lastQuoteTriggerMs = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        _moodLeds.startGeiger(0.2f);  // frequency snaps to current distance on next tick
        LOGINF("Geiger: quote done — breathing resumed (cooldown %lld s)",
               QUOTE_COOLDOWN_MS / 1000);
    }

    // ---- Thermal Overload mode phase transitions --------------------------------

    // Phase B → C: once the LED is fully throbbing, vent the quote.
    if (_thermalPhase == ThermalPhase::OVERLOAD && _moodLeds.isOverloading()) {
        _thermalPhase = ThermalPhase::VENTING;
        _inQuoteMode  = true;
        const char* q = _quotes->pickQuote();
        if (q && *q) {
            std::strncpy(s_quoteBuffer, q, sizeof(s_quoteBuffer) - 1);
            s_quoteBuffer[sizeof(s_quoteBuffer) - 1] = '\0';
        }
        _displayManager->setAnimation(
            std::make_unique<ScrollingTextAnimation>(s_quoteBuffer, 250, false));
        LOGINF("Thermal: venting — Phase C quote");
    }

    // Phase C end: scroll done → LEDs off, display off, 5 s cooldown.
    if (_thermalPhase == ThermalPhase::VENTING && !_displayManager->isAnimationRunning()) {
        _thermalPhase      = ThermalPhase::COOLDOWN;
        _thermalCooldownMs = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        _inQuoteMode       = false;
        _fadingOut         = false;
        _moodLeds.clearImmediate();
        _display.setBrightness(0);
        LOGINF("Thermal: cooldown started");
    }

    // Cooldown end: 5 s elapsed AND person has backed away.
    if (_thermalPhase == ThermalPhase::COOLDOWN) {
        int64_t nowMs = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (nowMs - _thermalCooldownMs >= 5000 && _thermalLastMm >= 1000) {
            _thermalPhase = ThermalPhase::NONE;
            _display.setBrightness(
                static_cast<uint8_t>(_appPrefs.config.displayBrightness));
            LOGINF("Thermal: cooldown ended — resuming");
        }
    }
}

// --- Distance sensor ---------------------------------------------------------

void WhispererApp::pollDistance() {
    if (!_tofAvailable) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }
    int mm = _tof.readRangeMm();

    // ~Every 3 s at DEBUG: raw reading + device status + return signal
    // rate, for diagnosing behind-glass behavior (glass reflection shows
    // up as a huge signal rate pinned at a short distance).
    static int s_diagCounter = 0;
    if (++s_diagCounter >= 30) {
        s_diagCounter = 0;
        LOGDBG("ToF diag: %d mm, status 0x%02X, signal %u Q9.7 (%.1f MCPS)",
               mm, _tof.lastRangeStatus(),
               (unsigned)_tof.lastSignalRateQ97(),
               _tof.lastSignalRateQ97() / 128.0f);
    }

    onDistanceReading(mm);
    // readRangeMm already blocked ~33 ms waiting for the sample; a small
    // extra delay keeps worst-case poll rate around 10 Hz.
    vTaskDelay(pdMS_TO_TICKS(67));
}

void WhispererApp::onDistanceReading(int mm) {
    // Ignore readings below 60 mm — spurious glass-reflection returns that
    // would otherwise anchor the EMA baseline to the cover-glass distance.
    if (mm >= 0 && mm < 60) return;

    // Out of range / no target (the 8190/8191 sentinel, -1 timeout, or
    // beyond 2 m). In geiger mode, ease the breathing back to calm —
    // otherwise walking out of range froze the tubes at the last (and
    // therefore most nervous) frequency.
    if (mm < 0 || mm > 2000) {
        if (std::strcmp(_appPrefs.config.triggerMode, "geiger") == 0) {
            onGeigerNoTarget();
        }
        return;
    }

    if (std::strcmp(_appPrefs.config.triggerMode, "thermal") == 0) {
        onDistanceReadingThermal(mm);
        return;
    }
    if (std::strcmp(_appPrefs.config.triggerMode, "geiger") == 0) {
        onDistanceReadingGeiger(mm);
        return;
    }

    if (_inQuoteMode || _pendingQuote) return;

    if (_lastStableDistanceMm < 0) {
        _lastStableDistanceMm = mm;
        return;
    }

    int64_t nowMs = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool inCooldown = (_lastQuoteTriggerMs > 0 &&
                       (nowMs - _lastQuoteTriggerMs) < QUOTE_COOLDOWN_MS);

    int delta = std::abs(mm - _lastStableDistanceMm);

    if (!inCooldown && delta >= DISTANCE_CHANGE_THRESHOLD_MM) {
        LOGDBG("ToF trigger: %d mm (baseline %d mm, delta %d mm)",
               mm, _lastStableDistanceMm, delta);
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
    _pendingQuote = true;
}

void WhispererApp::onDistanceReadingThermal(int mm) {
    _thermalLastMm = mm;

    // Ignore sensor during quote display and cooldown.
    if (_inQuoteMode ||
        _thermalPhase == ThermalPhase::VENTING ||
        _thermalPhase == ThermalPhase::COOLDOWN) return;

    int64_t nowMs  = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool    inZone = (mm < 1000);

    if (!inZone) {
        if (_thermalPhase != ThermalPhase::NONE) {
            LOGDBG("Thermal: left zone (%d mm) — reset", mm);
            _thermalPhase = ThermalPhase::NONE;
            _moodLeds.clearImmediate();
        }
        return;
    }

    switch (_thermalPhase) {
    case ThermalPhase::NONE:
        _thermalPhase      = ThermalPhase::WARN;
        _thermalPresenceMs = nowMs;
        _moodLeds.setWarn();
        LOGDBG("Thermal: entered zone (%d mm) — Phase A warning", mm);
        break;

    case ThermalPhase::WARN:
        if (nowMs - _thermalPresenceMs >= 3000) {
            _thermalPhase = ThermalPhase::OVERLOAD;
            _moodLeds.startOverload();
            LOGINF("Thermal: 3 s in zone (%d mm) — Phase B overload", mm);
        }
        break;

    case ThermalPhase::OVERLOAD:
        break;  // transition to VENTING is driven from loop() once LED is throbbing

    default:
        break;
    }
}

// Geiger breathing frequency range — shared by the valid-reading and
// no-target paths.
static constexpr float GEIGER_FREQ_FAR      = 0.2f;   // calm breathe at no movement
static constexpr float GEIGER_FREQ_CLOSE    = 5.0f;   // nervous Geiger at max deviation
static constexpr float GEIGER_MAX_DEVIATION = 400.0f; // mm of movement = full nervousness

void WhispererApp::onGeigerNoTarget() {
    if (!_geigerRunning) return;
    // Exponential ease toward calm: at ~10 Hz polling this settles in a
    // couple of seconds, reading as the tube relaxing rather than the
    // LEDs dying mid-motion.
    _geigerFreqHz += (GEIGER_FREQ_FAR - _geigerFreqHz) * 0.05f;
    _moodLeds.setGeigerFrequency(_geigerFreqHz);
}

void WhispererApp::onDistanceReadingGeiger(int mm) {
    // Frequency maps deviation from the adaptive ambient baseline (not absolute
    // distance), so the mode works correctly in confined spaces like under a desk
    // where the ceiling is always nearby. The baseline drifts slowly with the
    // environment via the same EMA used for trigger detection.
    if (_lastStableDistanceMm >= 0) {
        float deviation = (float)std::abs(mm - _lastStableDistanceMm);
        float t         = std::min(1.0f, deviation / GEIGER_MAX_DEVIATION);
        _geigerFreqHz = GEIGER_FREQ_FAR + (GEIGER_FREQ_CLOSE - GEIGER_FREQ_FAR) * t;
        _moodLeds.setGeigerFrequency(_geigerFreqHz);
    }

    if (_inQuoteMode) return;

    // Quote trigger — same threshold and cooldown as classic mode.
    if (_lastStableDistanceMm < 0) {
        _lastStableDistanceMm = mm;
        return;
    }
    int64_t nowMs = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool inCooldown = (_lastQuoteTriggerMs > 0 &&
                       (nowMs - _lastQuoteTriggerMs) < QUOTE_COOLDOWN_MS);
    int delta = std::abs(mm - _lastStableDistanceMm);

    if (!inCooldown && delta >= DISTANCE_CHANGE_THRESHOLD_MM) {
        LOGDBG("Geiger trigger: %d mm (baseline %d mm, delta %d mm)",
               mm, _lastStableDistanceMm, delta);
        _lastStableDistanceMm = mm;

        float mood = moodFromDistance(mm);
        _moodProvider = std::make_unique<FixedMoodProvider>(mood);
        _quotes       = std::make_unique<QuoteManager>(_moodProvider.get(), nullptr, 0, 10);
        const char* q = _quotes->pickQuote();
        if (!q || !*q) return;
        std::strncpy(s_quoteBuffer, q, sizeof(s_quoteBuffer) - 1);
        s_quoteBuffer[sizeof(s_quoteBuffer) - 1] = '\0';

        _inQuoteMode = true;
        _moodLeds.setFull();  // LEDs at 100% for the duration of the quote
        _displayManager->setAnimation(
            std::make_unique<ScrollingTextAnimation>(s_quoteBuffer, 250, false));
        LOGINF("Geiger trigger (%d mm, mood %.2f) → quote", mm, mood);
    } else {
        _lastStableDistanceMm = (int)(_lastStableDistanceMm * 0.98f + mm * 0.02f);
    }
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
    if (_thermalPhase == ThermalPhase::COOLDOWN) return false;
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

    _displayManager->setAnimation(
        std::make_unique<ScrollingTextAnimation>(waiting, 180, false));
    _apManagerConcrete.runBlockingLoop();
}
