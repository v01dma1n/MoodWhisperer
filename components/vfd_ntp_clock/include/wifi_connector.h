// wifi_connector.h — one-shot WiFi STA connect helper.
//
// Wraps the IDF event dance into a blocking call with a bounded retry
// count. Returns true on success, false if credentials are empty or all
// attempts time out.

#pragma once

// Initialize netif, default event loop, and WiFi once per boot. Idempotent.
void WiFiInit();

// Attempt to connect in STA mode. `host` is the hostname advertised via
// DHCP option 12. Each attempt waits up to ~10s for an IP. Returns true
// on success, false otherwise (caller drops into AP mode).
bool WiFiConnect(const char* host, const char* ssid, const char* pass,
                 int attempts);

// Disconnect and stop WiFi (e.g. before switching to AP mode).
void WiFiStop();
