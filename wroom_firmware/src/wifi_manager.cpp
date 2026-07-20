#include "wifi_manager.h"
#include "comms.h"
#include "time_manager.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

Preferences  prefs;
bool         wifiConnecting    = false;
unsigned long wifiConnectStart = 0;

static String        s_savedSsid              = "";
static String        s_savedPass              = "";
static bool          s_wifiDropped            = false;
static unsigned long s_wifiDropTime           = 0;
static uint32_t      s_wifiBackoffMs          = 5000;
static const uint32_t WIFI_MAX_BACKOFF_MS     = 60000UL;
static unsigned long  s_intentionalDiscUntilMs = 0;  // suppress reconnect arm until this time
static bool           s_wifiScanPending        = false;
static unsigned long  s_wifiScanStartMs        = 0;

String getWifiSsid() { return s_savedSsid; }

// ── WiFi event callback ───────────────────────────────────────────────────────
// Registered once in wifiManagerInit(). Fires on the WiFi driver task.

static void onWiFiEvent(WiFiEvent_t event, arduino_event_info_t info) {
  switch (event) {

    case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
      // Read the AP channel from the event struct — WiFi.channel() can return 0
      // briefly right after connect, making the event struct the authoritative source.
      uint8_t apChannel = info.wifi_sta_connected.channel;
      Serial.printf("[WIFI] Connected to AP on channel %d\n", apChannel);
      s_wifiDropped    = false;
      s_wifiBackoffMs  = 5000;
      lastKnownChannel = apChannel;
      resyncEspNow();

      StaticJsonDocument<128> wstat;
      wstat["type"]      = "WIFI_STATUS";
      wstat["connected"] = true;
      wstat["ssid"]      = s_savedSsid;
      sendDoc(wstat);
      break;
    }

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WIFI] STA Disconnected");

      // Restore the radio to the fixed fallback channel so ESP-NOW keeps working
      // regardless of which AP channel we were on when the disconnect happened.
      esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
      lastKnownChannel = ESPNOW_CHANNEL;

      esp_now_del_peer(CROWPANEL_MAC);
      {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, CROWPANEL_MAC, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
      }

      send("{\"type\":\"WIFI_STATUS\",\"connected\":false}");

      // Only arm auto-reconnect for genuine unexpected drops.
      // A 3 s suppression window is opened by any intentional disconnect call
      // (handleWifiConnect, handleWifiDisconnect, scan) to absorb all the
      // STA_DISCONNECTED events that fire during a single connect/scan sequence.
      if (millis() >= s_intentionalDiscUntilMs && s_savedSsid.length() > 0) {
        s_wifiDropped  = true;
        s_wifiDropTime = millis();
        Serial.printf("[WIFI] Unexpected drop — auto-reconnect in %lu ms\n", s_wifiBackoffMs);
      }
      break;

    default:
      break;
  }
}

// ── Init ──────────────────────────────────────────────────────────────────────

void wifiManagerInit() {
  prefs.begin("wifi", false);
  s_savedSsid = prefs.getString("ssid", "");
  s_savedPass = prefs.getString("pass", "");
  if (s_savedSsid.length() > 0) {
    Serial.printf("[WIFI] Loaded saved credentials for '%s'\n", s_savedSsid.c_str());
  }
  WiFi.onEvent(onWiFiEvent);
}

// ── Process (call every loop iteration) ──────────────────────────────────────

void wifiProcess() {

  // ── Async connection timeout monitor ───────────────────────────────────────
  if (wifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting  = false;
      s_wifiDropped   = false;
      s_wifiBackoffMs = 5000;
      Serial.println("[WIFI] Connected! IP: " + WiFi.localIP().toString());

      StaticJsonDocument<128> resp;
      resp["type"]      = "WIFI_STATUS";
      resp["connected"] = true;
      resp["ip"]        = WiFi.localIP().toString();
      sendDoc(resp);
      syncNTP();

    } else if (millis() - wifiConnectStart > 10000) {
      // Timed out. Restore ESP-NOW state only — do NOT call handleWifiDisconnect()
      // because that clears s_wifiDropped, which would stop auto-reconnect retries.
      wifiConnecting           = false;
      s_intentionalDiscUntilMs = millis() + 3000;
      Serial.println("[WIFI] Connection timed out.");
      WiFi.disconnect(false);
      delay(50);
      esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
      lastKnownChannel = ESPNOW_CHANNEL;
      esp_now_del_peer(CROWPANEL_MAC);
      {
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, CROWPANEL_MAC, 6);
        p.channel = 0;
        p.encrypt = false;
        esp_now_add_peer(&p);
      }
      // STA_DISCONNECTED is suppressed above, so we send the status update here.
      send("{\"type\":\"WIFI_STATUS\",\"connected\":false}");
    }
  }

  // ── Async scan result collection ───────────────────────────────────────────
  if (s_wifiScanPending) {
    int  found      = WiFi.scanComplete();
    bool scanDone    = (found >= 0 || found == WIFI_SCAN_FAILED);
    bool scanTimeout = (!scanDone && millis() - s_wifiScanStartMs > 10000);

    if (scanDone || scanTimeout) {
      s_wifiScanPending = false;
      String ssidList = "";
      if (found > 0) {
        int limit = min(found, 5);
        for (int i = 0; i < limit; i++) {
          if (i > 0) ssidList += ",";
          ssidList += WiFi.SSID(i);
        }
        Serial.printf("[WIFI] Scan complete: %d networks found\n", found);
      } else {
        Serial.printf("[WIFI] Scan complete: no networks (code %d)\n", found);
      }
      WiFi.scanDelete();

      // The async scan internally hops channels — resync our peer registration.
      resyncEspNow(true);

      StaticJsonDocument<1024> resp;
      resp["type"]  = "WIFI_SCAN_RESULT";
      resp["ssids"] = ssidList;
      sendDoc(resp);
    }
  }

  // ── Exponential-backoff auto-reconnect ─────────────────────────────────────
  // Only runs when: an unexpected AP drop occurred, we are not mid-connect or scan,
  // and saved credentials are available to reconnect with.
  if (s_wifiDropped && !wifiConnecting && !s_wifiScanPending &&
      WiFi.status() != WL_CONNECTED && s_savedSsid.length() > 0) {
    if (millis() - s_wifiDropTime >= s_wifiBackoffMs) {
      Serial.printf("[WIFI] Auto-reconnect to '%s' (backoff=%lu ms)\n",
                    s_savedSsid.c_str(), s_wifiBackoffMs);
      s_wifiBackoffMs = min((uint32_t)(s_wifiBackoffMs * 2), WIFI_MAX_BACKOFF_MS);
      s_wifiDropTime  = millis();
      handleWifiConnect(s_savedSsid, s_savedPass);
    }
  }
}

// ── Command handlers ──────────────────────────────────────────────────────────

void handleWifiScan() {
  // Non-blocking async scan. A blocking scan stalls loop() for 2–5 s,
  // causing CrowPanel ESP-NOW PINGs to go unanswered and triggering
  // its channel-recovery scanner unnecessarily.
  Serial.println("[WIFI] Starting async WiFi scan...");
  WiFi.disconnect(false);
  WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
  s_wifiScanPending = true;
  s_wifiScanStartMs = millis();
}

void handleWifiConnect(const String &ssidStr, const String &passStr) {
  Serial.printf("[WIFI] Connecting to: '%s'\n", ssidStr.c_str());

  s_savedSsid = ssidStr;
  s_savedPass = passStr;
  prefs.putString("ssid", s_savedSsid);
  prefs.putString("pass", s_savedPass);

  // Open a 3 s suppression window so all STA_DISCONNECTED events fired during
  // disconnect + scan + begin are treated as intentional (not unexpected drops).
  s_intentionalDiscUntilMs = millis() + 3000;

  WiFi.disconnect(false);
  delay(100);

  // Scan for the target AP's channel so we can pre-hop the CrowPanel to that
  // channel before WiFi.begin() locks the radio there.
  int     n        = WiFi.scanNetworks(false, true, false, 300, 0, ssidStr.c_str());
  uint8_t targetCh = (n > 0) ? WiFi.channel(0) : 0;
  WiFi.scanDelete();

  resyncEspNow(true);
  delay(50);

  if (targetCh != 0 && targetCh != lastKnownChannel) {
    Serial.printf("[ESP-NOW] Target AP on channel %d. Pre-hopping CrowPanel.\n", targetCh);
    StaticJsonDocument<64> hop;
    hop["type"] = "CHANNEL_HOP";
    hop["ch"]   = targetCh;
    String hopOut; serializeJson(hop, hopOut);
    send(hopOut);
    delay(100);

    lastKnownChannel = targetCh;
    esp_now_del_peer(CROWPANEL_MAC);
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, CROWPANEL_MAC, 6);
    peerInfo.channel = targetCh;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }

  if (targetCh != 0) {
    WiFi.begin(ssidStr.c_str(), passStr.c_str(), targetCh);
  } else {
    WiFi.begin(ssidStr.c_str(), passStr.c_str());
  }
  esp_wifi_set_ps(WIFI_PS_NONE); // Force disable power saving again after begin()

  wifiConnecting   = true;
  wifiConnectStart = millis();
  Serial.println("[WIFI] Connection initiated asynchronously.");
}

void handleWifiDisconnect() {
  Serial.println("[WIFI] Disconnecting...");

  s_intentionalDiscUntilMs = millis() + 3000;
  s_wifiDropped            = false;

  // Notify CrowPanel to hop back to the default channel BEFORE we turn off
  // the radio — while ESP-NOW can still reach it.
  if (lastKnownChannel != ESPNOW_CHANNEL) {
    Serial.printf("[ESP-NOW] Pre-hopping CrowPanel back to default channel (%d).\n", ESPNOW_CHANNEL);
    StaticJsonDocument<64> hop;
    hop["type"] = "CHANNEL_HOP";
    hop["ch"]   = ESPNOW_CHANNEL;
    String hopOut; serializeJson(hop, hopOut);
    send(hopOut);
    delay(100);
  }

  // wifioff=true stops the radio and clears the NVS connection entry.
  WiFi.disconnect(true, true);
  delay(100);

  s_savedSsid = "";
  s_savedPass = "";
  prefs.putString("ssid", "");
  prefs.putString("pass", "");

  // WiFi.disconnect(true) calls esp_wifi_stop(), which destroys ESP-NOW state.
  // Re-initialize from scratch.
  espNowInit();

  // esp_wifi_stop() makes WiFi.channel() return 0, so we set the channel directly
  // rather than relying on resyncEspNow() which would compute the wrong delta.
  lastKnownChannel = ESPNOW_CHANNEL;
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  StaticJsonDocument<128> resp;
  resp["type"]      = "WIFI_STATUS";
  resp["connected"] = false;
  sendDoc(resp);
}
