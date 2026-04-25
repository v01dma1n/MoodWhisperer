// main.cpp — ESP-IDF entry point for MoodWhisperer.
//
// Task layout:
//   Core 0 "AppTask"      — setup() + loop(): WiFi, NTP, portal, scene FSM.
//   Core 1 "DisplayTask"  — dm.update() + leds.update() at 50 Hz.
//   Core 1 "DistanceTask" — VL53L0X poll loop (~10 Hz), triggers quotes.
//   Core 1 "RefreshTask"  — optional, only for drivers needing sw mux (not PT6315).

#include "whisperer_app.h"
#include "logging.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr int APP_TASK_STACK = 12288;
static constexpr int APP_TASK_PRIO  = 5;
static constexpr int APP_TASK_CORE  = 0;

static void logTaskWatermarks() {
    static char buf[800];
    vTaskList(buf);
    LOGINF("--- task watermarks (name / state / prio / free_stack_words / num) ---");
    char* line = buf;
    for (char* p = buf; ; ++p) {
        if (*p == '\n' || *p == '\0') {
            bool done = (*p == '\0');
            *p = '\0';
            if (*line) LOGINF("  %s", line);
            if (done) break;
            line = p + 1;
        }
    }
    LOGINF("--- end watermarks ---");
}

// Drives display animation + mood LED fade at 50 Hz on core 1 so AppTask
// blocking in WiFi/NTP doesn't stall the glass or the LEDs.
static void displayTask(void* /*pvParameters*/) {
    WhispererApp& app  = WhispererApp::getInstance();
    DisplayManager& dm = app.getClock();
    MoodLeds& leds     = app.getMoodLeds();
    for (;;) {
        dm.update();
        leds.update();
        vTaskDelay(pdMS_TO_TICKS(20));  // 50 Hz
    }
}

// Polls the VL53L0X at ~10 Hz and forwards readings to the app.
static void distanceTask(void* /*pvParameters*/) {
    for (;;) {
        WhispererApp::getInstance().pollDistance();
    }
}

static void refreshTask(void* /*pvParameters*/) {
    IDisplayDriver& display = WhispererApp::getInstance().getDisplay();
    for (;;) {
        display.writeNextDigit();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void appTask(void* /*pvParameters*/) {
    auto& app = WhispererApp::getInstance();
    app.setup();

    xTaskCreatePinnedToCore(
        displayTask, "DisplayTask",
        4096, nullptr, 6, nullptr, /*core=*/1);

    xTaskCreatePinnedToCore(
        distanceTask, "DistanceTask",
        4096, nullptr, 4, nullptr, /*core=*/1);

    if (app.getDisplay().needsContinuousUpdate()) {
        xTaskCreatePinnedToCore(
            refreshTask, "RefreshTask",
            4096, nullptr, 10, nullptr, /*core=*/1);
    }

    logTaskWatermarks();

    static TickType_t s_lastDump = 0;
    for (;;) {
        app.loop();

        TickType_t now = xTaskGetTickCount();
        if (now - s_lastDump >= pdMS_TO_TICKS(30000)) {
            s_lastDump = now;
            logTaskWatermarks();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" void app_main(void) {
    LOGINF(">>> MoodWhisperer booting");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xTaskCreatePinnedToCore(
        appTask, "AppTask",
        APP_TASK_STACK, nullptr, APP_TASK_PRIO, nullptr, APP_TASK_CORE);

    LOGINF(">>> tasks running; app_main returning to idle");
}
