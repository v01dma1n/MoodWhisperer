// base_access_point_manager.h — soft-AP + captive portal + config form.
//
// Arduino version uses ESPAsyncWebServer. IDF port uses esp_http_server
// (httpd) together with a lightweight DNS responder that redirects every
// query to the AP's own IP, so most phones pop the captive-portal sheet
// automatically.
//
// Extension points for apps:
//   1) Override initializeFormFields() to add rows (OpenWeatherMap keys,
//      mood-source selection, brightness, whatever) on top of the base
//      WiFi/TZ/logLevel fields.
//   2) Pass a subclass of BasePreferences to the constructor so the
//      form reads/writes against the app's own config.

#pragma once

#include "enc_types.h"

#include "esp_http_server.h"
#include "esp_netif.h"

#include <vector>
#include <string>

class DisplayManager;
class BasePreferences;

// Displayed in place of the real password in the served form.
static constexpr const char* PASSWORD_MASKED = "********";

class BaseAccessPointManager {
public:
    explicit BaseAccessPointManager(BasePreferences& prefs);
    virtual ~BaseAccessPointManager();

    // Start the soft-AP with the given hostname as the SSID (no password).
    virtual void setup(const char* hostName);

    // Tear everything down. Called automatically by the destructor and
    // whenever the FSM leaves AP mode.
    void shutdown();

    // Show a banner until a client connects, then a different banner
    // once the user loads the form. Blocks until the user posts "/save"
    // and the device reboots, OR a very long timeout lapses (caller's
    // responsibility — this helper just loops forever).
    //
    // Because IDF's httpd is already asynchronous, "blocking" just means
    // we spin here and update the banner.
    void runBlockingLoop(DisplayManager& display,
                         const char* waitingMsg,
                         const char* connectedMsg);

    // True between the first STA-associated event and the user leaving.
    bool isClientConnected() const { return _isClientConnected; }

protected:
    // Override to add application-specific rows to _formFields. Call the
    // base implementation first so the built-in rows come before the
    // app-specific ones.
    virtual void initializeFormFields();

    BasePreferences&        _prefs;
    std::vector<FormField>  _formFields;

private:
    // HTTP handlers — declared static because httpd wants C-style function
    // pointers, but they dispatch to `this` via the user_ctx pointer.
    static esp_err_t handleRoot(httpd_req_t* req);
    static esp_err_t handleSave(httpd_req_t* req);
    static esp_err_t handleCaptivePortalProbe(httpd_req_t* req);
    static esp_err_t handleNotFound(httpd_req_t* req, httpd_err_code_t err);

    // WiFi and httpd setup.
    void startSoftAp(const char* hostName);
    void startHttpd();
    void stopHttpd();
    void startDnsResponder();
    void stopDnsResponder();

    // Renders the complete HTML page into `out`.
    void generateForm(std::string& out);

    // Parses an application/x-www-form-urlencoded body and writes back
    // into _prefs via _formFields. Returns true on success.
    bool applyFormBody(const char* body, size_t len);

    // URL-decode a single form field value in place.
    static void urlDecode(std::string& s);

    // DNS task (captive-portal hijack). Runs as a low-priority FreeRTOS
    // task; stops when _dnsRun is cleared.
    static void dnsTaskTrampoline(void* arg);
    void dnsTaskLoop();

    esp_netif_t*     _apNetif       = nullptr;
    httpd_handle_t   _httpd         = nullptr;
    volatile bool    _dnsRun        = false;
    void*            _dnsTaskHandle = nullptr;
    int              _dnsSocket     = -1;

    bool _isClientConnected = false;
    std::string _pageTitle;
};
