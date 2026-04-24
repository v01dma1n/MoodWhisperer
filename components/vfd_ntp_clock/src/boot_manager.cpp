// boot_manager.cpp — NVS-backed double-reset detection.

#include "boot_manager.h"
#include "i_base_clock.h"
#include "logging.h"

#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr const char* NVS_BOOT_FLAG_NS  = "bootflags";
static constexpr const char* NVS_BOOT_FLAG_KEY = "recent";

BootManager::BootManager(IBaseClock& clock) : _clock(clock) {}

bool BootManager::checkForForceAPMode() {
    nvs_handle_t h = 0;
    if (nvs_open(NVS_BOOT_FLAG_NS, NVS_READWRITE, &h) != ESP_OK) return false;

    uint8_t flag = 0;
    nvs_get_u8(h, NVS_BOOT_FLAG_KEY, &flag);
    bool forceAp = (flag != 0);

    // Mark this boot as recent. The app calls markBootStable() later to
    // clear the flag once the user has stopped tapping reset.
    nvs_set_u8(h, NVS_BOOT_FLAG_KEY, 1);
    nvs_commit(h);
    nvs_close(h);

    if (forceAp) {
        LOGINF("BootManager: double reset detected -> AP mode");
    }
    return forceAp;
}

void BootManager::markBootStable() {
    nvs_handle_t h = 0;
    if (nvs_open(NVS_BOOT_FLAG_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_BOOT_FLAG_KEY, 0);
    nvs_commit(h);
    nvs_close(h);
}
