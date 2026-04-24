// main.cpp — ESP-IDF entry point for VFDWhisperer.
//
// The structure mirrors GustavClock's two-task layout:
//   * Core 0, "AppTask" — runs BaseNtpClockApp::setup() once and then
//     BaseNtpClockApp::loop() forever. Handles WiFi, NTP, the captive
//     portal, scene transitions, and animation advancement.
//   * Core 1, "RefreshTask" — only used for drivers that need external
//     multiplexing. The PT6315 multiplexes internally so this task is
//     currently just a watchdog-friendly idler, but it is kept as a
//     hook for a future driver swap.
//
// app_main() just spawns the two tasks and suspends itself.

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

// Print every task's remaining stack (words) to find the overflow culprit.
// vTaskList() needs CONFIG_FREERTOS_USE_TRACE_FACILITY + USE_STATS_FORMATTING.
static void logTaskWatermarks() {
    // ~40 chars per task; 20 tasks is a safe upper bound for this firmware.
    static char buf[800];
    vTaskList(buf);
    // vTaskList writes multi-line text; ESP_LOGI truncates at ~250 chars so
    // we log one line at a time.
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

static void refreshTask(void* /*pvParameters*/) {
    // Entered only after appTask has already built the singleton and
    // confirmed the driver wants continuous updates. No race with
    // app.setup() — we're guaranteed to have a fully-constructed driver.
    IDisplayDriver& display = WhispererApp::getInstance().getDisplay();
    for (;;) {
        display.writeNextDigit();
        vTaskDelay(pdMS_TO_TICKS(1));  // ~1 kHz refresh for up to 10 cells
    }
}

static void appTask(void* /*pvParameters*/) {
    auto& app = WhispererApp::getInstance();
    app.setup();

    // Now that the app (and its display driver) are fully constructed,
    // spawn the per-digit refresh task on core 1 *only* if the driver
    // actually needs externally-driven multiplexing. The PT6315 does not,
    // so under the current wiring this branch is never taken; we keep
    // the code path so swapping in a MAX6921-style driver is a one-line
    // change in vfd_hardware_map.h + the driver REQUIRES.
    if (app.getDisplay().needsContinuousUpdate()) {
        xTaskCreatePinnedToCore(
            refreshTask, "RefreshTask",
            4096, nullptr, 10, nullptr,
            /*core=*/1);
    }

    // Dump before any FSM state can block (WiFi connect, AP mode).
    logTaskWatermarks();

    // In RUNNING_NORMAL the loop returns quickly; dump every 30 s.
    // In AP/WiFi-connect states app.loop() blocks, so the dump inside
    // runBlockingLoop() covers those paths instead.
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
    LOGINF(">>> VFDWhisperer booting");

    // Initialize NVS up front — BasePreferences::setup() will do the
    // same, but pulling it forward means early log statements can be
    // written if we ever add NVS-backed logging later.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    // NETIF + event loop have to be ready before either WiFi or the
    // captive portal manager starts.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xTaskCreatePinnedToCore(
        appTask, "AppTask",
        APP_TASK_STACK, nullptr, APP_TASK_PRIO, nullptr, APP_TASK_CORE);

    // RefreshTask is spawned later, from inside appTask, once the app
    // singleton is fully constructed AND only if the selected display
    // driver actually wants continuous external multiplexing.

    LOGINF(">>> tasks running; app_main returning to idle");
}
