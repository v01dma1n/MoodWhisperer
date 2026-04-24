// wifi_connector.cpp — blocking STA connect on top of esp_wifi.

#include "wifi_connector.h"
#include "logging.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include <cstring>

static EventGroupHandle_t s_wifiEvents = nullptr;
static constexpr int BIT_CONNECTED = BIT0;
static constexpr int BIT_FAILED    = BIT1;

static bool s_initDone = false;
static esp_netif_t* s_staNetif = nullptr;

static void onWifiEvent(void* /*arg*/, esp_event_base_t base,
                        int32_t id, void* /*data*/) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifiEvents, BIT_FAILED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifiEvents, BIT_CONNECTED);
    }
}

void WiFiInit() {
    if (s_initDone) return;

    // netif_init and the default event loop may already have been
    // created by app_main; treat ALREADY_INITIALIZED / INVALID_STATE
    // as "fine, keep going".
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(e);

    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(e);

    s_staNetif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifiEvents = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &onWifiEvent, nullptr, nullptr));

    s_initDone = true;
}

bool WiFiConnect(const char* host, const char* ssid, const char* pass,
                 int attempts) {
    WiFiInit();

    if (!ssid || !*ssid) {
        LOGINF("No SSID configured; skipping STA connect");
        return false;
    }

    wifi_config_t sta = {};
    std::strncpy(reinterpret_cast<char*>(sta.sta.ssid), ssid, sizeof(sta.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(sta.sta.password), pass ? pass : "",
                 sizeof(sta.sta.password));
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;  // accept anything

    if (host && *host) {
        esp_netif_set_hostname(s_staNetif, host);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());

    for (int attempt = 0; attempt < attempts; ++attempt) {
        xEventGroupClearBits(s_wifiEvents, BIT_CONNECTED | BIT_FAILED);

        LOGINF("WiFi connect attempt %d/%d ssid=%s", attempt + 1, attempts, ssid);
        esp_wifi_connect();

        EventBits_t bits = xEventGroupWaitBits(
            s_wifiEvents, BIT_CONNECTED | BIT_FAILED,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

        if (bits & BIT_CONNECTED) {
            LOGINF("WiFi connected");
            return true;
        }
        LOGINF("WiFi attempt failed, retrying");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
}

void WiFiStop() {
    esp_wifi_disconnect();
    esp_wifi_stop();
}
