/**
 * wroom_firmware.ino
 * Biometrics Employee Time-In/Time-Out System — Controller Node
 *
 * Board    : ESP32-WROOM-32
 * AS608    : UART1  RX=GPIO16, TX=GPIO17, TOUCH=GPIO34
 * DS3231   : I2C    SDA=GPIO21, SCL=GPIO22
 * CrowPanel: ESP-NOW wireless link (no UART wire needed)
 *
 * Libraries (install via Arduino Library Manager):
 *   - Adafruit Fingerprint Sensor Library  (Adafruit)
 *   - RTClib                               (Adafruit)
 *   - ArduinoJson                          (Benoit Blanchon)
 *
 * Serial commands (USB monitor only):
 *   ENROLL:<slot>   enroll a finger into slot 1-127
 *   DELETE:<slot>   erase stored template from sensor
 */

#include <Adafruit_Fingerprint.h>
#include <Wire.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <time.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <Preferences.h>

// ============================================================
// Pin definitions
// ============================================================
#define PIN_FP_RX    27   // AS608 TX  -> WROOM (UART1 RX)
#define PIN_FP_TX    26   // AS608 RX  <- WROOM (UART1 TX)
#define PIN_FP_TOUCH 34   // AS608 T-OUT  HIGH when finger present
#define PIN_FACTORY_RESET 14 // Factory Reset hardware button (active low)
#define MAX_SLOTS    127

// ============================================================
// Device Registration / Activation
// ============================================================
// Hardcoded device ID — used as the payload when querying the
// backend API to verify if this unit has been activated.
#define DEVICE_ID    "P001-2607-6AEC-Z2GD"

// Base URL of your backend server.
// Change to your server PC's LAN IP (e.g. http://192.168.1.50:8000) for local testing.
#define API_BASE_URL "https://demo.manpromanagement.com"

// ============================================================
// ESP-NOW configuration  (Option A — fixed channel)
// ============================================================
// Set your router to a fixed channel (e.g. 1) and update ESPNOW_CHANNEL to match.
// Both boards must use the same value.
#define ESPNOW_CHANNEL 1

// The CrowPanel's actual station MAC address.
// PLACEHOLDER: Flash the CrowPanel first and read its "[BOOT] CP MAC:" line,
// then paste those 6 hex bytes here before flashing the WROOM.
uint8_t CROWPANEL_MAC[6] = {0x30, 0xED, 0xA0, 0x31, 0x70, 0xEC}; // 30:ed:a0:31:70:ec

// ESP-NOW payload size limit
#define ESPNOW_PAYLOAD_MAX 251

// ============================================================
// UART instances
// ============================================================
HardwareSerial fpSerial(1);   // UART1 -> AS608
// cpSerial removed — CrowPanel link is now ESP-NOW wireless

// ============================================================
// Peripheral objects
// ============================================================
Adafruit_Fingerprint finger(&fpSerial);
RTC_DS3231 rtc;
Preferences prefs;

// ============================================================
// Mock employee database (JSON)
// AS608 slot ID maps directly to employee "id".
// Edit this JSON and re-flash to add/change employees.
// ============================================================
const char EMPLOYEES_JSON[] = R"([
  {"id":1,"name":"Admin","dept":"Admin","job_title":"System Admin","branch":"Main","fp_enrolled":false},
  {"id":2,"name":"Claire Jem Dedicatoria","dept":"HR","job_title":"Intern Tech Lead","branch":"Nasya","fp_enrolled":false},
  {"id":3,"name":"Alice Santos","dept":"HR","job_title":"HR Manager","branch":"Main","fp_enrolled":false},
  {"id":4,"name":"Bob Cruz","dept":"IT","job_title":"Developer","branch":"Main","fp_enrolled":false},
  {"id":5,"name":"Carol Reyes","dept":"Finance","job_title":"Accountant","branch":"Main","fp_enrolled":false},
  {"id":6,"name":"Dave Lim","dept":"Security","job_title":"Guard","branch":"Main","fp_enrolled":false},
  {"id":7,"name":"Eve Tan","dept":"Admin","job_title":"Clerk","branch":"Main","fp_enrolled":false}
])";

struct Employee { int id; String name; String dept; String job_title; String branch; bool fp_enrolled; };
const int MAX_EMP = 10;
Employee  empDB[MAX_EMP];
int       empCount = 0;

// ============================================================
// Time-In/Out state per slot
//   lastIn[slot] == true  -> last action was IN  -> next = OUT
//   lastIn[slot] == false -> last action was OUT -> next = IN
// ============================================================
bool lastIn[MAX_SLOTS + 1] = {};

// ============================================================
// System state
// ============================================================
bool enrolling  = false;
bool activated  = false;   // Set by CrowPanel via DEVICE_ACTIVATED command
bool idle_screen_active = false;
bool rtcValid   = false;
uint32_t rtcFallbackOffset = 0;

bool wifiConnecting = false;
unsigned long wifiConnectStart = 0;
uint8_t lastKnownChannel = ESPNOW_CHANNEL;

// Saved AP credentials — persisted for auto-reconnect on unexpected AP drop
static String s_savedSsid = "";
static String s_savedPass = "";

// Auto-reconnect exponential-backoff state
static bool           s_wifiDropped       = false;  // true when AP dropped unexpectedly
// BUG-FIX: replaced bool s_intentionalDisc with a timestamp window.
// A bool is consumed by the FIRST STA_DISCONNECTED event; subsequent events
// fired during the same connect sequence (WiFi.disconnect inside handleWifiConnect,
// events during scanNetworks, etc.) all see it as false and log spurious
// "Unexpected drop" messages. A time window naturally covers all of them.
static unsigned long  s_intentionalDiscUntilMs = 0; // ignore STA_DISCONNECTED until this time
static unsigned long  s_wifiDropTime      = 0;
static uint32_t       s_wifiBackoffMs     = 5000;
static const uint32_t WIFI_MAX_BACKOFF_MS = 60000UL;

// Async WiFi scan state (non-blocking handleWifiScan)
static bool          s_wifiScanPending = false;
static unsigned long s_wifiScanStartMs = 0;

// NTP sync state (non-blocking)
bool ntpSyncPending = false;
unsigned long ntpSyncStart = 0;

// ============================================================
// Helpers
// ============================================================

bool lookupEmployee(int slot, String &name, String &dept) {
  int emp_id = ((slot - 1) / 10) + 1;
  for (int i = 0; i < empCount; i++) {
    if (empDB[i].id == emp_id) {
      name = empDB[i].name;
      dept = empDB[i].dept;
      return true;
    }
  }
  return false;
}

String getTimestamp() {
  struct tm t = {};
  // If NTP has synced successfully, the internal time will have a valid year
  if (getLocalTime(&t, 0) && (t.tm_year + 1900) >= 2020) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
  }

  DateTime now;
  if (rtcValid) {
    now = rtc.now();
  } else {
    // Software fallback using millis() if RTC is disconnected
    now = DateTime(F(__DATE__), F(__TIME__)) + TimeSpan(millis() / 1000);
  }
  
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  return String(buf);
}

// Initiate NTP sync — NON-BLOCKING. Fires configTime(); result polled in loop().
void syncNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NTP] Skipped — WiFi not connected.");
    // Tell CrowPanel immediately so the UI doesn't spin
    send("{\"type\":\"NTP_STATUS\",\"ok\":false,\"err\":\"WiFi not connected\"}");
    return;
  }
  Serial.println("[NTP] Sync initiated (non-blocking)...");
  // UTC+8 (Philippine Standard Time). Both primary & fallback servers used.
  configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
  ntpSyncPending = true;
  ntpSyncStart   = millis();
}

// Full send: ESP-NOW + Serial log (for important events)
void send(const String &json) {
  if (json.length() >= ESPNOW_PAYLOAD_MAX) {
    Serial.printf("[ESP-NOW] TX SKIP: payload too large (%d bytes)\n", json.length());
    return;
  }
  esp_now_send(CROWPANEL_MAC, (const uint8_t*)json.c_str(), json.length());
  Serial.println("[->CP] " + json);
}

// Quiet send: ESP-NOW only, no Serial spam (used for high-frequency TIME broadcasts)
void sendQuiet(const String &json) {
  if (json.length() >= ESPNOW_PAYLOAD_MAX) return;
  esp_now_send(CROWPANEL_MAC, (const uint8_t*)json.c_str(), json.length());
}

void sendDoc(JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  send(out);
}

// ============================================================
// ESP-NOW Resync (Channel Hop)
// ============================================================
void resyncEspNow(bool force = false) {
  uint8_t curCh = WiFi.channel();
  if (curCh == 0) curCh = ESPNOW_CHANNEL; // fallback

  // BUG-FIX: capture oldChannel BEFORE any assignment — the original code
  // assigned lastKnownChannel = curCh then checked (force && curCh == lastKnownChannel)
  // which was always true, sending spurious CHANNEL_HOP packets on every forced resync.
  uint8_t oldChannel = lastKnownChannel;

  if (curCh != oldChannel || force) {
    if (curCh != oldChannel) {
      Serial.printf("[ESP-NOW] Radio channel changed from %d to %d. Pre-hopping CrowPanel.\n", oldChannel, curCh);

      // Temporarily switch back to CrowPanel's known channel so it can hear us
      esp_wifi_set_channel(oldChannel, WIFI_SECOND_CHAN_NONE);

      StaticJsonDocument<64> hop;
      hop["type"] = "CHANNEL_HOP";
      hop["ch"]   = curCh;
      String hopOut; serializeJson(hop, hopOut);

      // Re-add peer on old channel to dispatch the hop notification
      esp_now_del_peer(CROWPANEL_MAC);
      esp_now_peer_info_t peerOld = {};
      memcpy(peerOld.peer_addr, CROWPANEL_MAC, 6);
      peerOld.channel = oldChannel;
      peerOld.encrypt = false;
      esp_now_add_peer(&peerOld);

      send(hopOut);
      delay(100); // Give ESP-NOW time to physically transmit
    } else {
      Serial.printf("[ESP-NOW] Forcing ESP-NOW resync on channel %d (no channel change)\n", curCh);
    }

    // Switch radio to the actual current channel and re-register peer
    lastKnownChannel = curCh;
    esp_wifi_set_channel(curCh, WIFI_SECOND_CHAN_NONE);
    esp_now_del_peer(CROWPANEL_MAC);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, CROWPANEL_MAC, 6);
    peerInfo.channel = curCh;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    // Only broadcast a second CHANNEL_HOP when the channel actually changed.
    // (force=true with same channel just re-registers the peer — no hop needed.)
    if (curCh != oldChannel) {
      StaticJsonDocument<64> hop;
      hop["type"] = "CHANNEL_HOP";
      hop["ch"]   = curCh;
      String hopOut; serializeJson(hop, hopOut);
      send(hopOut);
    }
  }
}

// ============================================================
// WiFi Event Handler
// ============================================================
void onWiFiEvent(WiFiEvent_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
      // Read the real AP channel from the event info struct — WiFi.channel() can
      // return 0 briefly right after connect, so this is the authoritative source.
      uint8_t apChannel = info.wifi_sta_connected.channel;
      Serial.printf("[WIFI] Connected to AP on channel %d\n", apChannel);
      s_wifiDropped   = false;   // clear drop flag — connection succeeded
      s_wifiBackoffMs = 5000;    // reset backoff for next drop
      // Pre-load lastKnownChannel so resyncEspNow() computes the delta correctly
      lastKnownChannel = apChannel;
      resyncEspNow();
      // Push WIFI_STATUS immediately so the CrowPanel indicator updates without delay
      StaticJsonDocument<128> wstat;
      wstat["type"]      = "WIFI_STATUS";
      wstat["connected"] = true;
      wstat["ssid"]      = s_savedSsid;
      sendDoc(wstat);
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WIFI] STA Disconnected");
      // Restore radio to the fixed fallback channel so ESP-NOW keeps working
      esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
      lastKnownChannel = ESPNOW_CHANNEL;
      // Re-register CrowPanel peer on the fallback channel
      esp_now_del_peer(CROWPANEL_MAC);
      {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, CROWPANEL_MAC, 6);
        peer.channel = 0; // 0 = use current radio channel
        peer.encrypt = false;
        esp_now_add_peer(&peer);
      }
      // Push WIFI_STATUS: false so the CrowPanel indicator updates immediately
      send("{\"type\":\"WIFI_STATUS\",\"connected\":false}");
      // Schedule auto-reconnect — only for genuine unexpected drops.
      // Suppressed for 3 s after any intentional disconnect call to cover all
      // STA_DISCONNECTED events fired during a single connect/scan sequence.
      if (millis() >= s_intentionalDiscUntilMs && s_savedSsid.length() > 0) {
        s_wifiDropped  = true;
        s_wifiDropTime = millis();
        Serial.printf("[WIFI] Unexpected drop — auto-reconnect in %lu ms\n", s_wifiBackoffMs);
      }
      // (no explicit reset — s_intentionalDiscUntilMs expires naturally)
      break;
    default:
      break;
  }
}

// ============================================================
// WiFi Command Handlers
// ============================================================
void handleWifiScan() {
  // FIX: converted to async (non-blocking) scan. The old blocking scanNetworks(false)
  // stalled loop() for 2-5 s, during which the CrowPanel's ESP-NOW PINGs went
  // unanswered and triggered its 10 s channel-recovery scanner unnecessarily.
  Serial.println("[WIFI] Starting async WiFi scan...");

  // Soft-disconnect keeps the radio alive but drops any AP association,
  // which is required before scanning.
  WiFi.disconnect(false);

  // async=true, show_hidden=true — returns immediately; result polled in loop()
  WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
  s_wifiScanPending = true;
  s_wifiScanStartMs = millis();
}

void handleWifiConnect(const String& ssidStr, const String& passStr) {
  Serial.printf("[WIFI] Connecting to: '%s'\n", ssidStr.c_str());

  // Persist credentials for auto-reconnect on unexpected future AP drop
  s_savedSsid = ssidStr;
  s_savedPass = passStr;
  
  prefs.putString("ssid", s_savedSsid);
  prefs.putString("pass", s_savedPass);

  // Open a 3 s suppression window: all STA_DISCONNECTED events fired during
  // WiFi.disconnect() + scanNetworks() + WiFi.begin() are treated as intentional.
  s_intentionalDiscUntilMs = millis() + 3000;

  WiFi.disconnect(false);
  delay(100);

  // 1. Find the target AP's channel first so we can pre-hop the CrowPanel
  int n = WiFi.scanNetworks(false, true, false, 300, 0, ssidStr.c_str());
  uint8_t targetCh = 0;
  if (n > 0) {
    targetCh = WiFi.channel(0);
  }
  WiFi.scanDelete();

  // 2. Restore our radio state because scanNetworks changes it
  resyncEspNow(true);
  delay(50); // let ESP-NOW stabilize

  // 3. Pre-hop the CrowPanel if the channel is going to change
  if (targetCh != 0 && targetCh != lastKnownChannel) {
    Serial.printf("[ESP-NOW] Target AP is on channel %d. Pre-hopping CrowPanel.\n", targetCh);
    
    // We are currently on lastKnownChannel. We send hop command to CrowPanel
    StaticJsonDocument<64> hop;
    hop["type"] = "CHANNEL_HOP";
    hop["ch"] = targetCh;
    String hopOut; serializeJson(hop, hopOut);
    send(hopOut);
    delay(100); // Give ESP-NOW time to physically transmit
    
    // Now update our local tracking and peer to the NEW channel
    lastKnownChannel = targetCh;
    
    esp_now_del_peer(CROWPANEL_MAC);
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, CROWPANEL_MAC, 6);
    peerInfo.channel = targetCh;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }

  // 4. Connect explicitly to the target channel (prevents sweeping)
  if (targetCh != 0) {
    WiFi.begin(ssidStr.c_str(), passStr.c_str(), targetCh);
  } else {
    WiFi.begin(ssidStr.c_str(), passStr.c_str());
  }

  wifiConnecting = true;
  wifiConnectStart = millis();
  Serial.println("[WIFI] Connection initiated asynchronously.");
}

void handleWifiDisconnect() {
  Serial.println("[WIFI] Disconnecting...");

  // Open a 3 s suppression window so the STA_DISCONNECTED fired by
  // WiFi.disconnect(true, true) below does NOT arm auto-reconnect.
  s_intentionalDiscUntilMs = millis() + 3000;
  s_wifiDropped            = false; // clear any pending backoff

  // If we are on a non-default channel, tell CrowPanel to go back to default
  // BEFORE we turn off the radio so the hop message can be sent.
  if (lastKnownChannel != ESPNOW_CHANNEL) {
    Serial.printf("[ESP-NOW] Pre-hopping CrowPanel back to default channel (%d).\n", ESPNOW_CHANNEL);
    StaticJsonDocument<64> hop;
    hop["type"] = "CHANNEL_HOP";
    hop["ch"] = ESPNOW_CHANNEL;
    String hopOut; serializeJson(hop, hopOut);
    send(hopOut);
    delay(100);
  }

  WiFi.disconnect(true, true);  // wifioff=true — kills radio + clears NVS credentials
  delay(100);

  // Clear saved credentials
  s_savedSsid = "";
  s_savedPass = "";
  prefs.putString("ssid", "");
  prefs.putString("pass", "");

  // Re-init ESP-NOW after the full radio-off (esp_wifi_stop was called above)
  extern void espNowInit();
  espNowInit();

  // FIX: directly restore channel + update tracking instead of calling
  // resyncEspNow() which reads WiFi.channel()=0 post-stop and does nothing.
  lastKnownChannel = ESPNOW_CHANNEL;
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  StaticJsonDocument<128> resp;
  resp["type"]      = "WIFI_STATUS";
  resp["connected"] = false;
  sendDoc(resp);
}

// ============================================================
// Device Activation — Server Validation (HTTP)
// ============================================================
// Called by the VALIDATE_ACTIVATION command from CrowPanel.
// Posts device_id + registration_code to the server and sends
// the result back to CrowPanel as ACTIVATION_RESULT.
void validateActivationWithServer(const String& registrationCode) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ACTIVATION] Cannot validate — WiFi not connected.");
    send("{\"type\":\"ACTIVATION_RESULT\",\"success\":false,\"err\":\"WiFi not connected\"}");
    return;
  }

  Serial.println("[ACTIVATION] Validating registration code with server...");
  Serial.printf("[ACTIVATION] device_id=%s  registration_code=%s\n", DEVICE_ID, registrationCode.c_str());

  HTTPClient http;
  // Append query parameters exactly as the real API expects:
  // /api/devices/registerDevice?device_id=...&registration_code=...
  String url = String(API_BASE_URL) 
             + "/api/devices/registerDevice"
             + "?device_id=" + DEVICE_ID 
             + "&registration_code=" + registrationCode;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Send an empty POST body since the data is in the query string
  int httpCode = http.POST("");

  Serial.printf("[ACTIVATION] HTTP %d\n", httpCode);

  StaticJsonDocument<256> result; // increased size to fit token
  result["type"] = "ACTIVATION_RESULT";

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("[ACTIVATION] Response: " + response);

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, response);

    bool success = false;
    String devToken = "";
    String errMsg = "Code rejected by server";

    if (err == DeserializationError::Ok) {
      String statusStr = doc["status"] | "";
      int statusCode = doc["status"] | 0;
      if (statusCode == 200 || statusStr.equalsIgnoreCase("success") || statusStr.equalsIgnoreCase("active") || statusStr.equalsIgnoreCase("true") || doc["success"].as<bool>()) {
        success = true;
        devToken = doc["device_token"] | "";
      } else {
        errMsg = doc["message"] | errMsg;
      }
    } else {
      Serial.println("[ACTIVATION] Could not parse server response.");
    }

    result["success"] = success;
    if (success) {
      result["device_token"] = devToken;
    } else {
      result["err"] = errMsg;
    }
  } else {
    Serial.printf("[ACTIVATION] HTTP failed: %s\n", http.errorToString(httpCode).c_str());
    result["success"] = false;
    result["err"]     = "Server unreachable";
  }

  http.end();
  sendDoc(result);
}

// ============================================================
// Fingerprint match
// ============================================================

void doMatch() {
  if (finger.image2Tz()     != FINGERPRINT_OK) { send("{\"type\":\"NOMATCH\"}"); return; }
  if (finger.fingerSearch() != FINGERPRINT_OK) { send("{\"type\":\"NOMATCH\"}"); return; }

  int    id   = finger.fingerID;
  int    conf = finger.confidence;
  String name, dept;
  bool   found = lookupEmployee(id, name, dept);

  // Toggle IN <-> OUT per slot
  bool isIn  = !lastIn[id];
  lastIn[id] = isIn;

  StaticJsonDocument<256> doc;
  doc["type"]   = "MATCH";
  doc["id"]     = id;
  doc["name"]   = found ? name : ("Slot " + String(id));
  doc["dept"]   = found ? dept : "";
  doc["action"] = isIn ? "IN" : "OUT";
  doc["conf"]   = conf;
  doc["ts"]     = getTimestamp();
  sendDoc(doc);
}

// ============================================================
// Enrollment (2-scan process)
// ============================================================

bool doEnroll(int slot) {
  int p;
  unsigned long t;

  // --- Scan 1 ---
  send("{\"type\":\"ENROLL_STEP\",\"step\":1,\"msg\":\"Place finger\"}");
  Serial.println("[ENROLL] Step 1 - place finger on sensor");
  t = millis();
  do {
    if (millis() - t > 15000) { Serial.println("[ENROLL] Step 1 TIMEOUT"); return false; }
    p = finger.getImage();
    if (p != FINGERPRINT_OK && p != FINGERPRINT_NOFINGER) {
      Serial.printf("[ENROLL] Step 1 getImage() transient error: 0x%02X\n", p);
    }
    delay(50);
  } while (p != FINGERPRINT_OK);
  Serial.println("[ENROLL] Step 1 image taken!");
  p = finger.image2Tz(1);
  Serial.printf("[ENROLL] Step 1 image2Tz(1) = 0x%02X\n", p);
  if (p != FINGERPRINT_OK) { Serial.printf("[ENROLL] Step 1 FAIL: image2Tz(1) bad code 0x%02X\n", p); return false; }

  // --- Lift ---
  send("{\"type\":\"ENROLL_STEP\",\"step\":2,\"msg\":\"Lift finger\"}");
  Serial.println("[ENROLL] Step 2 - lift finger");
  do { p = finger.getImage(); delay(50); } while (p != FINGERPRINT_NOFINGER);
  delay(400);

  // --- Scan 2 ---
  send("{\"type\":\"ENROLL_STEP\",\"step\":3,\"msg\":\"Place again\"}");
  Serial.println("[ENROLL] Step 3 - place finger again");
  t = millis();
  do {
    if (millis() - t > 15000) { Serial.println("[ENROLL] Step 3 TIMEOUT"); return false; }
    p = finger.getImage();
    if (p != FINGERPRINT_OK && p != FINGERPRINT_NOFINGER) {
      Serial.printf("[ENROLL] Step 3 getImage() transient error: 0x%02X\n", p);
    }
    delay(50);
  } while (p != FINGERPRINT_OK);
  Serial.println("[ENROLL] Step 3 image taken!");
  p = finger.image2Tz(2);
  Serial.printf("[ENROLL] Step 3 image2Tz(2) = 0x%02X\n", p);
  if (p != FINGERPRINT_OK) { Serial.printf("[ENROLL] Step 3 FAIL: image2Tz(2) bad code 0x%02X\n", p); return false; }

  // --- Create & store model ---
  p = finger.createModel();
  Serial.printf("[ENROLL] createModel() = 0x%02X\n", p);
  if (p != FINGERPRINT_OK) { Serial.printf("[ENROLL] FAIL: createModel bad code 0x%02X\n", p); return false; }
  p = finger.storeModel(slot);
  Serial.printf("[ENROLL] storeModel(%d) = 0x%02X\n", slot, p);
  if (p != FINGERPRINT_OK) { Serial.printf("[ENROLL] FAIL: storeModel bad code 0x%02X\n", p); return false; }
  return true;
}


// ============================================================
// Command handler (shared by USB Serial + CrowPanel UART)
// ============================================================

void handleCmd(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // Handle PING from CrowPanel (sent as JSON: {"type":"PING"})
  // We silently reply with PONG to avoid spamming the serial monitor
  if (cmd.startsWith("{")) {
    StaticJsonDocument<64> pdoc;
    if (deserializeJson(pdoc, cmd) == DeserializationError::Ok) {
      const char* t = pdoc["type"] | "";
      if (strcmp(t, "PING") == 0) {
        sendQuiet("{\"type\":\"PONG\"}");
        return;
      }
    }
  }

  Serial.println("[CMD] " + cmd);

  if (cmd == "RESET") {
    Serial.println("[SYSTEM] Reboot command received via Serial. Restarting...");
    send("{\"type\":\"RESET_ACK\"}");
    delay(200);
    ESP.restart();
  } else if (cmd == "GHOST_LOGIN" || cmd == "NUKE_USERS" || cmd == "DEBUG_COMMS") {
    Serial.println("[FWD->CP] Forwarding backdoor command to CrowPanel: " + cmd);
    String backdoorJson = "{\"type\":\"BACKDOOR\",\"cmd\":\"" + cmd + "\"}";
    send(backdoorJson);
  } else if (cmd.startsWith("ENROLL:")) {
    int colonIdx = cmd.indexOf(':', 7);
    int slot = 0;
    
    if (colonIdx != -1) {
      int emp_id = cmd.substring(7, colonIdx).toInt();
      int finger_index = cmd.substring(colonIdx + 1).toInt();
      slot = ((emp_id - 1) * 10) + finger_index + 1;
    } else {
      slot = cmd.substring(7).toInt(); // Fallback for old format
    }

    if (slot < 1 || slot > MAX_SLOTS) {
      Serial.println("Slot must be 1-127");
      return;
    }

    String name, dept;
    lookupEmployee(slot, name, dept);
    String label = name.length() ? name : ("Slot " + String(slot));

    StaticJsonDocument<128> doc;
    doc["type"] = "ENROLL_START";
    doc["slot"] = slot;
    doc["name"] = label;
    sendDoc(doc);

    enrolling = true;
    bool ok   = doEnroll(slot);
    enrolling = false;

    doc.clear();
    doc["type"] = ok ? "ENROLL_OK" : "ENROLL_FAIL";
    doc["slot"] = slot;
    if (ok) doc["name"] = label;
    sendDoc(doc);
    Serial.println(ok ? "[ENROLL] Success!" : "[ENROLL] Failed.");

  } else if (cmd.startsWith("DELETE:")) {
    int colonIdx = cmd.indexOf(':', 7);
    int slot = 0;
    
    if (colonIdx != -1) {
      int emp_id = cmd.substring(7, colonIdx).toInt();
      int finger_index = cmd.substring(colonIdx + 1).toInt();
      slot = ((emp_id - 1) * 10) + finger_index + 1;
    } else {
      slot = cmd.substring(7).toInt(); // Fallback for old format
    }

    bool ok   = (finger.deleteModel(slot) == FINGERPRINT_OK);
    StaticJsonDocument<64> doc;
    doc["type"] = ok ? "DELETE_OK" : "DELETE_FAIL";
    doc["slot"] = slot;
    sendDoc(doc);
    Serial.println(ok ? "[DEL] Slot " + String(slot) + " erased"
                      : "[DEL] Failed");

  } else if (cmd.startsWith("{")) {
    // ── JSON command from CrowPanel ──────────────────────────
    StaticJsonDocument<256> jcmd;
    if (deserializeJson(jcmd, cmd) != DeserializationError::Ok) {
      Serial.println("[CMD] Bad JSON: " + cmd);
      return;
    }
    const char *action = jcmd["cmd"] | "";

    if (strcmp(action, "WIFI_SCAN") == 0) {
      handleWifiScan();
    } else if (strcmp(action, "WIFI_CONNECT") == 0) {
      handleWifiConnect(jcmd["ssid"].as<String>(), jcmd["pass"].as<String>());
    } else if (strcmp(action, "WIFI_DISCONNECT") == 0) {
      handleWifiDisconnect();
    } else if (strcmp(action, "SYNC_NTP") == 0) {
      Serial.println("[NTP] Manual sync requested by CrowPanel.");
      syncNTP();
    } else if (strcmp(action, "VALIDATE_ACTIVATION") == 0) {
      // CrowPanel is asking us to validate a registration code with the server.
      String regCode = jcmd["registration_code"] | "";
      if (regCode.length() == 0) {
        send("{\"type\":\"ACTIVATION_RESULT\",\"success\":false,\"err\":\"Empty registration code\"}");
      } else {
        validateActivationWithServer(regCode);
      }

    } else if (strcmp(action, "DEVICE_ACTIVATED") == 0) {
      activated = true;
      Serial.println("[SYSTEM] CrowPanel signaled DEVICE_ACTIVATED. Fingerprint scanner enabled.");
      // Broadcast current wifi status so idle screen pill updates
      StaticJsonDocument<128> wstat;
      wstat["type"]      = "WIFI_STATUS";
      wstat["connected"] = (WiFi.status() == WL_CONNECTED);
      wstat["ssid"]      = s_savedSsid;
      sendDoc(wstat);

    } else if (strcmp(action, "GET_WIFI_STATUS") == 0) {
      StaticJsonDocument<128> wstat;
      wstat["type"]      = "WIFI_STATUS";
      wstat["connected"] = (WiFi.status() == WL_CONNECTED);
      wstat["ssid"]      = s_savedSsid;
      sendDoc(wstat);

    } else if (strcmp(action, "FACTORY_RESET") == 0) {
      activated = false;
      Serial.println("[SYSTEM] Factory reset received. Fingerprint scanner disabled.");
      send("{\"type\":\"FACTORY_RESET_ACK\"}");

    } else if (strcmp(action, "SET_IDLE") == 0) {
      idle_screen_active = jcmd["idle"] | false;

    } else if (strcmp(action, "RESET") == 0) {
      Serial.println("[SYSTEM] Remote reboot command received from CrowPanel. Restarting...");
      send("{\"type\":\"RESET_ACK\"}");
      delay(200); // Give UART time to flush before restart
      ESP.restart();
    }
  }
}

// ============================================================
// ESP-NOW ring buffer + callbacks
// Ring buffer: ESP-NOW recv callback (WiFi task, Core 0) -> loop() (Core 1).
// Lockless single-producer / single-consumer — only the producer advances
// s_cpQTail and only the consumer advances s_cpQHead.
// ============================================================
#define ESPNOW_QUEUE_SIZE 8
struct EspNowMsg { char data[ESPNOW_PAYLOAD_MAX]; };
static EspNowMsg s_cpQueue[ESPNOW_QUEUE_SIZE];
static volatile uint8_t s_cpQHead = 0, s_cpQTail = 0;

// Arduino Core 3.x (IDF 5.x): recv callback takes esp_now_recv_info_t* not uint8_t* mac
static void onDataRecvFromCP(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (len <= 0 || len >= ESPNOW_PAYLOAD_MAX) return;
  uint8_t next = (s_cpQTail + 1) % ESPNOW_QUEUE_SIZE;
  if (next == s_cpQHead) return; // queue full — drop
  memcpy(s_cpQueue[s_cpQTail].data, data, len);
  s_cpQueue[s_cpQTail].data[len] = '\0';
  s_cpQTail = next;
}

// Arduino Core 3.x (IDF 5.x): send callback takes wifi_tx_info_t* not uint8_t* mac
static void onDataSentToCP(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  // Static counters so we can throttle log spam when CrowPanel reboots to ch 1
  // while we're locked on a different WiFi channel (common after CP power-cycle).
  static uint32_t s_failCount     = 0;
  static uint32_t s_lastFailLogMs = 0;

  if (status != ESP_NOW_SEND_SUCCESS) {
    s_failCount++;
    uint32_t now = (uint32_t)millis();
    // Print first failure immediately, then throttle to once every 10 s.
    // The CrowPanel's 15 s channel scanner will find us automatically.
    if (s_failCount == 1 || now - s_lastFailLogMs >= 10000) {
      Serial.printf("[ESP-NOW] SEND FAIL #%u on ch %d — CP may have rebooted to ch %d. "
                    "CP scanner will recover in ~5 s.\n",
                    s_failCount, lastKnownChannel, ESPNOW_CHANNEL);
      s_lastFailLogMs = now;
    }
  } else {
    if (s_failCount > 0) {
      // Log recovery so we know the channel scanner did its job
      Serial.printf("[ESP-NOW] Send recovered after %u failure(s) on ch %d\n",
                    s_failCount, lastKnownChannel);
      s_failCount = 0;
    }
  }
}

void espNowInit() {
  // Always ensure STA mode is active.
  WiFi.mode(WIFI_STA);

  // After a failed WiFi.begin() the radio stays parked on the AP's channel.
  // esp_wifi_set_channel() is silently ignored while the driver is not idle,
  // so we must force the radio back to a clean disconnected state first.
  // IMPORTANT: disconnect(false) = disconnect from AP only, radio stays ON.
  //            disconnect(true)  = wifioff=true → calls esp_wifi_stop() → radio OFF → MAC becomes 00:00:00:00:00:00
  WiFi.disconnect(false);  // drop any AP association, keep radio alive
  delay(100);              // let the driver settle before touching the channel

  // Lock to the fixed channel BEFORE esp_now_init() so the peer
  // registration uses the correct channel from the start.
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Tear down any previous ESP-NOW state — safe to call even when not initialized.
  // Required when espNowInit() is called a second time after a WiFi.disconnect(true)
  // destroyed the radio (WIFI_SCAN / WIFI_CONNECT / WIFI_DISCONNECT handlers).
  esp_now_deinit();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED!");
    return;
  }

  esp_now_register_recv_cb(onDataRecvFromCP);
  esp_now_register_send_cb(onDataSentToCP);
  WiFi.onEvent(onWiFiEvent); // channel sync + WIFI_STATUS push on connect/disconnect

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, CROWPANEL_MAC, 6);
  peer.channel = 0;   // 0 = use current channel
  peer.encrypt  = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] add_peer FAILED — check CROWPANEL_MAC");
  }

  Serial.printf("[ESP-NOW] Initialized on channel %d\n", ESPNOW_CHANNEL);
  Serial.printf("[BOOT] WROOM MAC: %s\n", WiFi.macAddress().c_str());
}

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("[BOOT] Reset reason: %d\n", esp_reset_reason());
  Serial.println("\n=== Biometrics WROOM Controller ===");

  prefs.begin("wifi", false);
  s_savedSsid = prefs.getString("ssid", "");
  s_savedPass = prefs.getString("pass", "");
  if (s_savedSsid.length() > 0) {
    Serial.printf("[WIFI] Loaded saved credentials for '%s'\n", s_savedSsid.c_str());
  }

  // Parse employee JSON into struct array
  StaticJsonDocument<512> empDoc;
  if (!deserializeJson(empDoc, EMPLOYEES_JSON)) {
    for (JsonObject e : empDoc.as<JsonArray>()) {
      if (empCount >= MAX_EMP) break;
      empDB[empCount].id          = e["id"].as<int>();
      empDB[empCount].name        = e["name"].as<String>();
      empDB[empCount].dept        = e["dept"].as<String>();
      empDB[empCount].job_title   = e.containsKey("job_title") ? e["job_title"].as<String>() : "";
      empDB[empCount].branch      = e.containsKey("branch") ? e["branch"].as<String>() : "";
      empDB[empCount].fp_enrolled = e.containsKey("fp_enrolled") ? e["fp_enrolled"].as<bool>() : false;
      empCount++;
    }
    Serial.printf("[DB] %d employees loaded\n", empCount);
  } else {
    Serial.println("[DB] ERROR: JSON parse failed");
  }

  // AS608 fingerprint sensor
  fpSerial.begin(57600, SERIAL_8N1, PIN_FP_RX, PIN_FP_TX);
  finger.begin(57600);
  delay(100);
  if (finger.verifyPassword()) {
    finger.getParameters();
    int liveCount = 0;
    if (finger.getTemplateCount() == FINGERPRINT_OK) {
      liveCount = finger.templateCount;
    }
    Serial.printf("[AS608] Found! Templates stored (live count): %d\n", liveCount);
    Serial.println("[AS608] NOTE: if count=0 but you enrolled before, do NOT re-enroll.");
    Serial.println("[AS608] Wait for DEVICE_ACTIVATED from CrowPanel, then scan your finger first.");

  } else {
    Serial.println("[AS608] NOT FOUND - check wiring!");
  }

  espNowInit();

  // DS3231 RTC
  Wire.begin(21, 22);
  if (rtc.begin()) {
    rtcValid = true;
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.println("[RTC] Power lost - synced to compile time");
    }
    Serial.println("[RTC] Ready: " + getTimestamp());
  } else {
    rtcValid = false;
    Serial.println("[RTC] NOT FOUND - using software fallback clock");
  }

  pinMode(PIN_FACTORY_RESET, INPUT_PULLDOWN);

  Serial.println("\nReady. Commands:");
  Serial.println("  ENROLL:1     -> enroll finger for slot 1 (Alice Santos)");
  Serial.println("  DELETE:1     -> erase slot 1 from sensor");
  Serial.println("  GHOST_LOGIN  -> backdoor to bypass scanner into Main Menu");
  Serial.println("  NUKE_USERS   -> backdoor to erase all stored fingers except slot 1");
  Serial.println("  DEBUG_COMMS  -> toggle ESP-NOW ping/pong debug spam");

  // Signal to CrowPanel that we just booted, in case it is already in the idle/activated state.
  send("{\"type\":\"WROOM_BOOT\"}");
}

// ============================================================
// Loop
// ============================================================

unsigned long lastTimeBcast = 0;
String cpBuf = "";  // Non-blocking accumulator for CrowPanel UART
unsigned long btnPressTime = 0;
bool btnHeld = false;

void loop() {
  // Check factory reset button (hold for 5 seconds)
  if (digitalRead(PIN_FACTORY_RESET) == HIGH) {
    if (!btnHeld) {
      btnHeld = true;
      btnPressTime = millis();
      Serial.println("[SYSTEM] Factory reset button pressed. Hold for 5 seconds to confirm...");
    } else if (millis() - btnPressTime > 5000) {
      Serial.println("[SYSTEM] HARDWARE FACTORY RESET TRIGGERED!");
      // 1. Erase Wi-Fi credentials
      WiFi.disconnect(true, true);
      s_savedSsid = "";
      s_savedPass = "";
      prefs.clear();
      // 2. Erase all fingerprints
      finger.emptyDatabase();
      Serial.println("[SYSTEM] Wi-Fi erased and fingerprint database wiped. Rebooting...");
      // Send message to CrowPanel to wipe its own state
      send("{\"type\":\"FACTORY_RESET_ACK\"}");
      delay(2000);
      ESP.restart();
    }
  } else {
    if (btnHeld) {
      Serial.println("[SYSTEM] Factory reset button released.");
    }
    btnHeld = false;
  }

  // Broadcast current time to CrowPanel every second (QUIET - no Serial spam)
  if (millis() - lastTimeBcast >= 1000) {
    lastTimeBcast = millis();
    sendQuiet("{\"type\":\"TIME\",\"ts\":\"" + getTimestamp() + "\"}");
  }


  // ── Asynchronous Wi-Fi connection monitoring ────────────────────────────────
  if (wifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting  = false;
      s_wifiDropped   = false;  // connection succeeded — clear drop flag
      s_wifiBackoffMs = 5000;   // reset backoff for next drop
      Serial.println("[WIFI] Connected! IP: " + WiFi.localIP().toString());
      StaticJsonDocument<128> resp;
      resp["type"]      = "WIFI_STATUS";
      resp["connected"] = true;
      resp["ip"]        = WiFi.localIP().toString();
      sendDoc(resp);
      syncNTP(); // non-blocking — just fires configTime(), result polled below

    } else if (millis() - wifiConnectStart > 10000) {
      // FIX: timeout — light cleanup only. Do NOT call handleWifiDisconnect() which
      // clears s_wifiDropped and stops retries. Just restore ESP-NOW state and let
      // the backoff loop reschedule the next attempt.
      wifiConnecting = false;
      // Open suppression window: the STA_DISCONNECTED from WiFi.disconnect() below
      // is intentional — don't let it re-arm the drop timer or log "Unexpected drop".
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
      // Send WIFI_STATUS here: the STA_DISCONNECTED handler is suppressed above,
      // so this is the only place that sends it.
      send("{\"type\":\"WIFI_STATUS\",\"connected\":false}");
      // s_wifiDropped remains true — backoff loop will retry
    }
  }

  // ── Async WiFi scan result polling ──────────────────────────────────────────
  if (s_wifiScanPending) {
    int  found      = WiFi.scanComplete();
    bool scanDone   = (found >= 0 || found == WIFI_SCAN_FAILED);
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
      // Re-sync ESP-NOW — the async scan silently hops channels internally
      resyncEspNow(true);

      StaticJsonDocument<1024> resp;
      resp["type"]  = "WIFI_SCAN_RESULT";
      resp["ssids"] = ssidList;
      sendDoc(resp);
    }
  }

  // ── Exponential-backoff auto-reconnect after unexpected AP drop ─────────────
  // Only runs when: AP dropped unexpectedly, not currently connecting/scanning,
  // and we have saved credentials to reconnect with.
  if (s_wifiDropped && !wifiConnecting && !s_wifiScanPending &&
      WiFi.status() != WL_CONNECTED && s_savedSsid.length() > 0) {
    if (millis() - s_wifiDropTime >= s_wifiBackoffMs) {
      Serial.printf("[WIFI] Auto-reconnect to '%s' (backoff=%lu ms)\n",
                    s_savedSsid.c_str(), s_wifiBackoffMs);
      s_wifiBackoffMs = min((uint32_t)(s_wifiBackoffMs * 2), WIFI_MAX_BACKOFF_MS);
      s_wifiDropTime  = millis(); // reset timer for next attempt
      handleWifiConnect(s_savedSsid, s_savedPass);
    }
  }

  // ── Non-blocking NTP completion check ──────────────────────────────────────
  if (ntpSyncPending) {
    struct tm t = {};
    if (getLocalTime(&t, 0)) { // timeout=0: non-blocking check
      ntpSyncPending = false;

      char syncTs[20];
      snprintf(syncTs, sizeof(syncTs), "%04d-%02d-%02d %02d:%02d:%02d",
        t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
      Serial.printf("[NTP] Synced: %s (UTC+8)\n", syncTs);

      if (rtcValid) {
        rtc.adjust(DateTime(t.tm_year+1900, t.tm_mon+1, t.tm_mday,
                            t.tm_hour, t.tm_min, t.tm_sec));
        Serial.println("[NTP] RTC updated from NTP");
      }

      // Notify CrowPanel — clock settings page shows last sync time
      StaticJsonDocument<128> ntpDoc;
      ntpDoc["type"] = "NTP_STATUS";
      ntpDoc["ok"]   = true;
      ntpDoc["ts"]   = syncTs;
      sendDoc(ntpDoc);

    } else if (millis() - ntpSyncStart > 15000) {
      ntpSyncPending = false;
      Serial.println("[NTP] Sync timed out — using existing time source");
      send("{\"type\":\"NTP_STATUS\",\"ok\":false,\"err\":\"Sync timed out\"}");
    }
  }

  // Commands from USB Serial Monitor (blocking is OK here, user-typed)
  if (Serial.available()) {
    handleCmd(Serial.readStringUntil('\n'));
  }

  // Commands from CrowPanel — drained from ESP-NOW ring buffer.
  // onDataRecvFromCP() (Core 0 WiFi task) writes; this loop (Core 1) reads.
  while (s_cpQHead != s_cpQTail) {
    String line(s_cpQueue[s_cpQHead].data);
    s_cpQHead = (s_cpQHead + 1) % ESPNOW_QUEUE_SIZE;
    if (line.length() > 0) {
      if (line.indexOf("\"PING\"") == -1 && line.indexOf("\"PONG\"") == -1) {
        Serial.println("[<-CP] " + line);
      }
      handleCmd(line);
    }
  }

  // Fingerprint detection — only when activated and not enrolling
  if (activated && !enrolling && idle_screen_active) {
    if (finger.getImage() == FINGERPRINT_OK) {
      send("{\"type\":\"PLACE_FINGER\"}");
      doMatch();
      delay(1000);   // cooldown so it doesn't trigger multiple times for one touch
    }
  }
}
