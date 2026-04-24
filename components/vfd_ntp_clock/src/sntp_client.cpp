// sntp_client.cpp — thin wrapper over esp_sntp.

#include "sntp_client.h"
#include "logging.h"

#include "esp_sntp.h"
#include "esp_timer.h"

#include <cstdlib>
#include <ctime>

volatile bool timeAvail = false;

static bool s_sntpConfigured = false;
static int64_t s_startedAtUs = 0;

static void timeSyncNotify(struct timeval* /*tv*/) {
    timeAvail = true;
    LOGINF("NTP: time received");
}

void setupSntp(const char* tz) {
    if (tz && *tz) {
        setenv("TZ", tz, 1);
        tzset();
    }

    if (!s_sntpConfigured) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        esp_sntp_setservername(2, "time.nist.gov");
        sntp_set_time_sync_notification_cb(&timeSyncNotify);
        esp_sntp_init();
        s_sntpConfigured = true;
    }

    s_startedAtUs = esp_timer_get_time();
    timeAvail = false;
}

int loopSntpGetTime(unsigned intervalMillis) {
    if (timeAvail) return 1;

    int64_t elapsedMs = (esp_timer_get_time() - s_startedAtUs) / 1000;
    if (elapsedMs > static_cast<int64_t>(intervalMillis)) {
        LOGERR("NTP sync timed out after %u ms", intervalMillis);
        return -1;
    }
    return 0;
}
