// base_access_point_manager.cpp — IDF-native captive portal.

#include "base_access_point_manager.h"
#include "base_preferences.h"
#include "display_manager.h"
#include "anim_scrolling_text.h"
#include "tz_data.h"
#include "logging.h"
#include "wifi_connector.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>

// --- DNS hijack -------------------------------------------------------------
//
// Every DNS query received on port 53 is answered with the AP's own IP.
// That tricks iOS/Android/macOS/Windows into opening the captive portal
// sheet automatically.

struct DnsHeader {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
};

static constexpr uint16_t DNS_FLAGS_RESPONSE = 0x8180;  // std. query response, no error

// --- Log level options for the built-in form --------------------------------
// Referenced by initializeFormFields() below.
static const PrefSelectOption s_logLevelOptions[] = {
    { "Error", "0" },
    { "Info",  "1" },
    { "Debug", "2" },
};
static const int s_numLogLevelOptions =
    sizeof(s_logLevelOptions) / sizeof(s_logLevelOptions[0]);

// Storage for the string view of logLevel that the form round-trips through.
// FormField pref.str_pref points here; applyFormBody() parses back to enum.
// Must be MAX_PREF_STRING_LEN bytes — applyFormBody() calls strncpy with
// MAX_PREF_STRING_LEN-1 and assumes all str_pref buffers are that size.
static char s_logLevelBuffer[MAX_PREF_STRING_LEN] = "1";

// --- Lifecycle --------------------------------------------------------------

BaseAccessPointManager::BaseAccessPointManager(BasePreferences& prefs)
    : _prefs(prefs) {}

BaseAccessPointManager::~BaseAccessPointManager() {
    shutdown();
}

void BaseAccessPointManager::setup(const char* hostName) {
    _pageTitle = hostName ? hostName : "vfd-whisperer";

    // Make sure the logLevel string mirror reflects the current int enum
    // before rendering.
    std::snprintf(s_logLevelBuffer, sizeof(s_logLevelBuffer), "%d",
                  static_cast<int>(_prefs.getConfig().logLevel));

    _formFields.clear();
    initializeFormFields();

    startSoftAp(hostName);
    startHttpd();
    startDnsResponder();

    LOGINF("AP started as '%s' at 192.168.4.1", hostName);
}

void BaseAccessPointManager::shutdown() {
    stopDnsResponder();
    stopHttpd();

    if (_apNetif) {
        esp_wifi_stop();
        esp_netif_destroy_default_wifi(_apNetif);
        _apNetif = nullptr;
    }
    _isClientConnected = false;
}

// --- Soft AP setup ----------------------------------------------------------

void BaseAccessPointManager::startSoftAp(const char* hostName) {
    // Ensure WiFi is initialized. WiFiInit() is idempotent and safe to
    // call even if the app never attempted a STA connect (first boot
    // with empty credentials goes straight here).
    WiFiInit();

    // If WiFi is already running in any mode (e.g. a failed STA attempt
    // left it in STA mode), stop it cleanly before reconfiguring. Ignore
    // the "not started" error since that's the happy path on first boot.
    esp_err_t stopErr = esp_wifi_stop();
    if (stopErr != ESP_OK && stopErr != ESP_ERR_WIFI_NOT_INIT &&
        stopErr != ESP_ERR_WIFI_NOT_STARTED) {
        LOGERR("esp_wifi_stop returned %d before AP mode; continuing", stopErr);
    }

    if (_apNetif == nullptr) {
        _apNetif = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t apConfig = {};
    std::strncpy(reinterpret_cast<char*>(apConfig.ap.ssid), hostName,
                 sizeof(apConfig.ap.ssid));
    apConfig.ap.ssid_len       = std::strlen(hostName);
    apConfig.ap.channel        = 1;
    apConfig.ap.max_connection = 4;
    apConfig.ap.authmode       = WIFI_AUTH_OPEN;  // no password for setup

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apConfig));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// --- HTTP server ------------------------------------------------------------

void BaseAccessPointManager::startHttpd() {
    if (_httpd) return;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size     = 8192;
    cfg.max_uri_handlers = 8;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    // Pin httpd's accept loop to core 0 alongside the DNS task. Leaving
    // it unpinned causes it to float onto core 1 under load, which
    // interferes with lwIP's TCP task and can corrupt ISR state during
    // WiFi activity.
    cfg.core_id          = 0;

    if (httpd_start(&_httpd, &cfg) != ESP_OK) {
        LOGERR("httpd_start failed");
        _httpd = nullptr;
        return;
    }

    // Root page — the form.
    httpd_uri_t rootUri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = &BaseAccessPointManager::handleRoot,
        .user_ctx = this,
    };
    httpd_register_uri_handler(_httpd, &rootUri);

    // Save POST.
    httpd_uri_t saveUri = {
        .uri      = "/save",
        .method   = HTTP_POST,
        .handler  = &BaseAccessPointManager::handleSave,
        .user_ctx = this,
    };
    httpd_register_uri_handler(_httpd, &saveUri);

    // Captive-portal probe endpoints. Apple uses /hotspot-detect.html,
    // Android /generate_204, Windows /connecttest.txt — we catch all of
    // them with the 404 fallback below, which redirects to /.
    httpd_register_err_handler(_httpd, HTTPD_404_NOT_FOUND,
                               &BaseAccessPointManager::handleNotFound);
}

void BaseAccessPointManager::stopHttpd() {
    if (!_httpd) return;
    httpd_stop(_httpd);
    _httpd = nullptr;
}

esp_err_t BaseAccessPointManager::handleRoot(httpd_req_t* req) {
    auto* self = static_cast<BaseAccessPointManager*>(req->user_ctx);
    self->_isClientConnected = true;

    std::string html;
    self->generateForm(html);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html.c_str(), html.size());
}

esp_err_t BaseAccessPointManager::handleNotFound(httpd_req_t* req,
                                                 httpd_err_code_t /*err*/) {
    // Favicon probes: answer with a 204 and no body. Redirecting them to
    // "/" makes the browser parse our HTML form as an icon, and clutters
    // the logs. Android/Samsung browsers request this aggressively.
    if (std::strstr(req->uri, "favicon.ico") != nullptr) {
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
        return httpd_resp_send(req, nullptr, 0);
    }

    // Everything else: redirect to the root so captive-portal probe hits
    // trigger the portal sheet on the client device.
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, nullptr, 0);
}

// Fires from the esp_timer task ~1 second after a successful save, long
// after handleSave() has returned and the httpd worker has closed the
// socket. This avoids blocking the httpd thread for the reboot delay,
// which previously caused concurrent probe requests (iOS, Android,
// /favicon.ico) to panic on torn-down sockets.
static void rebootTimerCallback(void* /*arg*/) {
    esp_restart();
}

static void scheduleReboot(uint32_t delay_ms) {
    // One-shot; esp_timer owns the lifetime after we arm it.
    esp_timer_handle_t t = nullptr;
    esp_timer_create_args_t args = {};
    args.callback = &rebootTimerCallback;
    args.name     = "reboot";
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_once(t, static_cast<uint64_t>(delay_ms) * 1000ULL);
    } else {
        // Fallback: at least abort so we don't wedge.
        esp_restart();
    }
}

esp_err_t BaseAccessPointManager::handleSave(httpd_req_t* req) {
    auto* self = static_cast<BaseAccessPointManager*>(req->user_ctx);

    int total = req->content_len;
    if (total <= 0 || total > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }

    std::string body;
    body.resize(total);
    int recvd = 0;
    while (recvd < total) {
        int n = httpd_req_recv(req, body.data() + recvd, total - recvd);
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_OK;
        }
        recvd += n;
    }

    bool ok = self->applyFormBody(body.c_str(), body.size());
    if (!ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parse");
        return ESP_OK;
    }

    // Clear the double-reset flag before we reboot. Without this, the
    // BootManager on the next boot still sees a "recent" flag left over
    // from whatever sequence got us into AP mode in the first place, and
    // would force us right back into AP mode ignoring the credentials
    // the user just saved — the classic "stuck in AP mode" loop.
    {
        nvs_handle_t h = 0;
        if (nvs_open("bootflags", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_u8(h, "recent", 0);
            nvs_commit(h);
            nvs_close(h);
        }
    }

    // Tell httpd we want to close the connection after this response, so
    // the client (and any proxies/retries) know not to keep-alive into a
    // server that's about to vanish.
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/html");

    static const char msg[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>Saved</title>"
        "<style>body{font:16px system-ui;margin:2em;color:#eee;background:#111}</style>"
        "<h1>saved. rebooting...</h1>";
    httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);

    // Schedule the reboot on a timer so this handler returns immediately.
    // The httpd worker cleans up the socket as it unwinds; concurrent
    // probes (favicon.ico, hotspot-detect.html, etc.) continue to be
    // serviced until the timer fires.
    scheduleReboot(1500);
    return ESP_OK;
}

// --- DNS responder task -----------------------------------------------------

void BaseAccessPointManager::startDnsResponder() {
    if (_dnsRun) return;
    _dnsRun = true;

    // IMPORTANT: pin the DNS task to core 0 and give it 6 KB of stack.
    // The default 3 KB is not enough once lwIP's recvfrom path recurses
    // with WiFi + softirq activity, and leaving the task unpinned lets
    // it float onto core 1 where it races with lwIP's internal tasks —
    // we saw Core 1 idle-task panics (InstrFetchProhibited, PC=0) when
    // stack overflow corrupted an ISR return address.
    TaskHandle_t h = nullptr;
    xTaskCreatePinnedToCore(&BaseAccessPointManager::dnsTaskTrampoline,
                            "dns_hijack",
                            /*stack=*/6144,
                            this,
                            /*prio=*/3,
                            &h,
                            /*core=*/0);
    _dnsTaskHandle = h;
}

void BaseAccessPointManager::stopDnsResponder() {
    _dnsRun = false;
    if (_dnsSocket >= 0) {
        ::close(_dnsSocket);
        _dnsSocket = -1;
    }
    // Let the task exit naturally from the recv error.
    if (_dnsTaskHandle) {
        // Task deletes itself on loop exit; just forget the handle.
        _dnsTaskHandle = nullptr;
    }
}

void BaseAccessPointManager::dnsTaskTrampoline(void* arg) {
    static_cast<BaseAccessPointManager*>(arg)->dnsTaskLoop();
    vTaskDelete(nullptr);
}

void BaseAccessPointManager::dnsTaskLoop() {
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        LOGERR("dns socket() failed");
        return;
    }
    _dnsSocket = s;

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        LOGERR("dns bind() failed");
        ::close(s);
        _dnsSocket = -1;
        return;
    }

    uint8_t buf[512];
    while (_dnsRun) {
        struct sockaddr_in client = {};
        socklen_t clen = sizeof(client);

        int n = recvfrom(s, buf, sizeof(buf), 0,
                        reinterpret_cast<struct sockaddr*>(&client), &clen);
        if (n < static_cast<int>(sizeof(DnsHeader))) continue;

        // Flip response bit; keep qd section; append a single A-record
        // pointing at 192.168.4.1 using DNS message compression (0xC00C).
        DnsHeader* hdr = reinterpret_cast<DnsHeader*>(buf);
        hdr->flags   = htons(DNS_FLAGS_RESPONSE);
        hdr->ancount = htons(1);

        // Walk the question's QNAME to the terminating zero byte, but
        // bail if a length-prefix runs us past the packet — malformed
        // queries must not be allowed to read/write past `buf`.
        int qend = sizeof(DnsHeader);
        bool malformed = false;
        while (qend < n && buf[qend] != 0) {
            uint8_t label_len = buf[qend];
            // DNS label lengths max out at 63; anything else is a
            // compression pointer or garbage, and we don't handle either.
            if (label_len > 63) { malformed = true; break; }
            qend += label_len + 1;
            if (qend >= n)       { malformed = true; break; }
        }
        if (malformed) continue;
        qend += 1;     // terminating 0
        qend += 4;     // qtype + qclass
        if (qend > n)  continue;  // truncated packet

        if (qend + 16 > static_cast<int>(sizeof(buf))) continue;
        uint8_t* ans = buf + qend;
        ans[0]  = 0xC0; ans[1]  = 0x0C;     // pointer to QNAME
        ans[2]  = 0x00; ans[3]  = 0x01;     // type A
        ans[4]  = 0x00; ans[5]  = 0x01;     // class IN
        ans[6]  = 0x00; ans[7]  = 0x00;     // TTL
        ans[8]  = 0x00; ans[9]  = 0x3C;     //   = 60
        ans[10] = 0x00; ans[11] = 0x04;     // RDLENGTH
        ans[12] = 192;  ans[13] = 168;
        ans[14] = 4;    ans[15] = 1;

        sendto(s, buf, qend + 16, 0,
               reinterpret_cast<struct sockaddr*>(&client), clen);
    }

    ::close(s);
    _dnsSocket = -1;
}

// --- Form rendering ---------------------------------------------------------

void BaseAccessPointManager::initializeFormFields() {
    auto& cfg = _prefs.getConfig();

    _formFields.push_back(FormField{
        "ssid", "WiFi SSID", false, VALIDATION_NONE, PREF_STRING,
        { .str_pref = cfg.ssid }, nullptr, 0,
    });
    _formFields.push_back(FormField{
        "password", "WiFi Password", true, VALIDATION_NONE, PREF_STRING,
        { .str_pref = cfg.password }, nullptr, 0,
    });

    // Timezone as a dropdown backed by the shared tz_data table.
    _formFields.push_back(FormField{
        "timezone", "Time Zone", false, VALIDATION_NONE, PREF_SELECT,
        { .str_pref = cfg.time_zone }, timezones, num_timezones,
    });

    _formFields.push_back(FormField{
        "loglevel", "Log Level", false, VALIDATION_NONE, PREF_SELECT,
        { .str_pref = s_logLevelBuffer }, s_logLevelOptions,
        s_numLogLevelOptions,
    });
}

// Lightweight HTML escaping for values embedded in the form.
static void htmlEscape(std::string& out, const char* in) {
    if (!in) return;
    for (const char* p = in; *p; ++p) {
        switch (*p) {
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '&':  out += "&amp;";  break;
            default:   out += *p;       break;
        }
    }
}

void BaseAccessPointManager::generateForm(std::string& out) {
    out.clear();
    out.reserve(4096);

    // Inline CSS keeps it under one HTTP response, no external resources
    // (the client has no internet on this AP). Dark theme, monospace —
    // fits the VFD vibe, legible on all phone sizes.
    out +=
        "<!doctype html><html lang=en><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>";
    htmlEscape(out, _pageTitle.c_str());
    out +=
        " setup</title>"
        "<style>"
        ":root{--bg:#0a0e14;--fg:#c8d1d9;--accent:#3bd4ae;--dim:#55606d;--panel:#131921}"
        "*{box-sizing:border-box}"
        "body{margin:0;padding:1.5em 1em;background:var(--bg);color:var(--fg);"
        "font:15px/1.5 ui-monospace,'SF Mono',Menlo,monospace}"
        "main{max-width:28em;margin:0 auto}"
        "h1{color:var(--accent);font-weight:500;margin:0 0 .2em;letter-spacing:.08em}"
        "p.sub{color:var(--dim);margin:0 0 2em;font-size:.9em}"
        "label{display:block;margin:1em 0 .3em;color:var(--dim);"
        "text-transform:uppercase;letter-spacing:.12em;font-size:.75em}"
        "input,select{width:100%;padding:.6em .7em;background:var(--panel);"
        "border:1px solid var(--dim);color:var(--fg);font:inherit;border-radius:2px}"
        "input:focus,select:focus{outline:none;border-color:var(--accent)}"
        "button{margin-top:2em;width:100%;padding:.8em;background:var(--accent);"
        "color:#000;border:0;font:inherit;font-weight:600;letter-spacing:.1em;"
        "text-transform:uppercase;cursor:pointer;border-radius:2px}"
        ".row-bool{display:flex;align-items:center;gap:.6em;margin-top:1em}"
        ".row-bool input{width:auto}"
        ".row-bool label{margin:0;text-transform:none;letter-spacing:0;"
        "font-size:1em;color:var(--fg)}"
        "</style></head><body><main>"
        "<h1>";
    htmlEscape(out, _pageTitle.c_str());
    out += "</h1><p class=sub>setup mode &middot; fill in and save</p>"
           "<form method=POST action='/save'>";

    for (const FormField& f : _formFields) {
        if (f.prefType == PREF_BOOL) {
            out += "<div class=row-bool><input type=checkbox name='";
            out += f.id; out += "' id='"; out += f.id; out += "'";
            if (f.pref.bool_pref && *f.pref.bool_pref) out += " checked";
            out += "><label for='"; out += f.id; out += "'>";
            htmlEscape(out, f.name);
            out += "</label></div>";
            continue;
        }

        out += "<label for='"; out += f.id; out += "'>";
        htmlEscape(out, f.name);
        out += "</label>";

        if (f.prefType == PREF_SELECT && f.select_options) {
            out += "<select name='"; out += f.id; out += "' id='";
            out += f.id; out += "'>";
            const char* current = f.pref.str_pref ? f.pref.str_pref : "";
            for (int i = 0; i < f.num_select_options; ++i) {
                const PrefSelectOption& opt = f.select_options[i];
                out += "<option value=\"";
                htmlEscape(out, opt.value);
                out += "\"";
                if (std::strcmp(opt.value, current) == 0) out += " selected";
                out += ">";
                htmlEscape(out, opt.name);
                out += "</option>";
            }
            out += "</select>";
            continue;
        }

        out += "<input type='";
        out += f.isMasked ? "password" : "text";
        out += "' name='"; out += f.id; out += "' id='"; out += f.id;
        out += "' value='";
        if (f.isMasked && f.pref.str_pref && *f.pref.str_pref) {
            out += PASSWORD_MASKED;
        } else if (f.prefType == PREF_INT && f.pref.int_pref) {
            char tmp[16];
            std::snprintf(tmp, sizeof(tmp), "%ld",
                          static_cast<long>(*f.pref.int_pref));
            htmlEscape(out, tmp);
        } else if (f.pref.str_pref) {
            htmlEscape(out, f.pref.str_pref);
        }
        out += "' autocomplete=off>";
    }

    out += "<button>Save &amp; Restart</button></form></main></body></html>";
}

// --- Form submission --------------------------------------------------------

void BaseAccessPointManager::urlDecode(std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return 10 + h - 'a';
                if (h >= 'A' && h <= 'F') return 10 + h - 'A';
                return 0;
            };
            out += static_cast<char>((hex(s[i + 1]) << 4) | hex(s[i + 2]));
            i += 2;
        } else {
            out += c;
        }
    }
    s = std::move(out);
}

bool BaseAccessPointManager::applyFormBody(const char* body, size_t len) {
    // Walk key=value pairs separated by '&'.
    size_t i = 0;
    while (i < len) {
        size_t amp = i;
        while (amp < len && body[amp] != '&') ++amp;
        size_t eq = i;
        while (eq < amp && body[eq] != '=') ++eq;
        if (eq == amp) { i = amp + 1; continue; }

        std::string key(body + i, eq - i);
        std::string val(body + eq + 1, amp - (eq + 1));
        urlDecode(key);
        urlDecode(val);
        i = amp + 1;

        // Find matching FormField.
        for (const FormField& f : _formFields) {
            if (key != f.id) continue;

            if (f.prefType == PREF_BOOL && f.pref.bool_pref) {
                *f.pref.bool_pref = !val.empty() &&
                                    (val == "on" || val == "1" || val == "true");
            } else if (f.prefType == PREF_INT && f.pref.int_pref) {
                *f.pref.int_pref = std::atoi(val.c_str());
            } else if ((f.prefType == PREF_STRING ||
                        f.prefType == PREF_SELECT) && f.pref.str_pref) {
                // Keep the old password if the submitted value is the mask.
                if (f.isMasked && val == PASSWORD_MASKED) continue;
                std::strncpy(f.pref.str_pref, val.c_str(),
                             MAX_PREF_STRING_LEN - 1);
                f.pref.str_pref[MAX_PREF_STRING_LEN - 1] = '\0';
            }
            break;
        }
    }

    // Also handle unchecked checkboxes: if the form didn't include the
    // key, the bool is false. We re-walk just the bool fields for this.
    for (const FormField& f : _formFields) {
        if (f.prefType != PREF_BOOL || !f.pref.bool_pref) continue;
        std::string needle; needle += f.id; needle += "=";
        if (std::string(body, len).find(needle) == std::string::npos) {
            *f.pref.bool_pref = false;
        }
    }

    // Propagate the log-level string back to the enum field.
    _prefs.getConfig().logLevel =
        static_cast<AppLogLevel>(std::atoi(s_logLevelBuffer));

    _prefs.putPreferences();
    return true;
}

// --- Blocking loop for the app ---------------------------------------------

void BaseAccessPointManager::runBlockingLoop(DisplayManager& display,
                                             const char* waitingMsg,
                                             const char* connectedMsg) {
    LOGINF("AP waiting banner: %s", waitingMsg ? waitingMsg : "");

    bool announced = false;
    TickType_t lastDump = xTaskGetTickCount();
    while (true) {
        if (!announced && _isClientConnected) {
            LOGINF("AP connected banner: %s", connectedMsg ? connectedMsg : "");
            announced = true;
            if (connectedMsg) {
                display.setAnimation(std::make_unique<ScrollingTextAnimation>(
                    connectedMsg, 180, false));
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (now - lastDump >= pdMS_TO_TICKS(30000)) {
            lastDump = now;
            static char wm_buf[800];
            vTaskList(wm_buf);
            LOGINF("--- task watermarks ---");
            char* line = wm_buf;
            for (char* p = wm_buf; ; ++p) {
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

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
