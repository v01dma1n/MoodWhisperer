// app_main.cpp — bring-up test for the LGL Studio VFD on a bare ESP32
// board.
//
// Wiring (this board only exposes the header side with 3V3, GPIO25,
// GPIO26, GPIO27 — GND is available on the same side):
//   VFD GND -> ESP32 GND
//   VFD VCC -> ESP32 3V3
//   VFD CS  -> ESP32 GPIO25
//   VFD CLK -> ESP32 GPIO26
//   VFD SDI -> ESP32 GPIO27
//
// This is intentionally standalone — no WiFi, no NVS, no NTP engine, no
// mood LEDs / ToF sensor. Just proves the glass lights up and takes text.

#include "lgl_vfd_studio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "lgl_bringup";

constexpr int VFD_CS_GPIO  = 25;
constexpr int VFD_CLK_GPIO = 26;
constexpr int VFD_SDI_GPIO = 27;
constexpr int VFD_NUM_DIGITS = 16;

// Custom characters (CGRAM) were investigated and dropped: three plausible
// command-byte candidates plus a no-command baseline all rendered character
// codes 1-7 as blank, which doesn't distinguish "command ignored" from
// "command worked but codes 1-7 aren't the reference convention" — a two
// -unknown search with no way to converge by eye. Not needed for
// MoodWhisperer's plain-text quotes anyway.

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "LGL VFD bring-up starting (CS=%d CLK=%d SDI=%d, %d digits)",
             VFD_CS_GPIO, VFD_CLK_GPIO, VFD_SDI_GPIO, VFD_NUM_DIGITS);

    LglVfdStudio vfd(VFD_CS_GPIO, VFD_CLK_GPIO, VFD_SDI_GPIO, VFD_NUM_DIGITS);
    vfd.begin();
    vfd.clear();
    vfd.writeString(0, "Mood Whisperer");
    ESP_LOGI(TAG, "Wrote greeting — check the glass");
    vTaskDelay(pdMS_TO_TICKS(3000));  // hold before the demo loop clears it

    for (;;) {
        vfd.clear();
        vfd.writeChar(0, 'A');
        vTaskDelay(pdMS_TO_TICKS(2000));

        vfd.writeString(0, "Hello World!    ");
        vTaskDelay(pdMS_TO_TICKS(2000));

        vfd.writeString(0, "1234567890ABCDEF");
        vTaskDelay(pdMS_TO_TICKS(2000));

        for (int b = 255; b >= 50; b -= 50) {
            vfd.setBrightness(static_cast<uint8_t>(b));
            vfd.writeString(0, "Bright Test     ");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        vfd.setBrightness(0xFF);
    }
}
