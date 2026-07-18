#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Persisted WiFi credentials and connection state, owned by wifi_manager.
// Exposed here so setup() can call prefs.begin() before wifiManagerInit().
extern Preferences prefs;

// True while WiFi.begin() is in progress and we are waiting for a result.
extern bool         wifiConnecting;
extern unsigned long wifiConnectStart;

// Loads saved WiFi credentials from flash and registers the WiFi event handler.
// Call once from setup(), before espNowInit().
void wifiManagerInit();

// Drives the async WiFi state machine. Call every loop() iteration.
// Handles: connection timeout, scan result collection, exponential-backoff reconnect.
void wifiProcess();

// Returns the saved SSID (persists after disconnect, empty if never connected).
String getWifiSsid();

// ── Command handlers — called by command_handler when CrowPanel sends WiFi commands ──

// Starts a non-blocking WiFi scan. Results are delivered via WIFI_SCAN_RESULT.
void handleWifiScan();

// Connects to the given AP asynchronously. Persists credentials for auto-reconnect.
void handleWifiConnect(const String &ssid, const String &pass);

// Disconnects from WiFi and clears saved credentials.
void handleWifiDisconnect();
