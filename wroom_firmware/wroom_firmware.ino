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

// ============================================================
// Pin definitions
// ============================================================
#define PIN_FP_RX    17   // AS608 TX  -> WROOM (UART1 RX)
#define PIN_FP_TX    16   // AS608 RX  <- WROOM (UART1 TX)
#define PIN_FP_TOUCH 34   // AS608 T-OUT  HIGH when finger present
#define PIN_FACTORY_RESET 14 // Factory Reset hardware button (active low)
#define MAX_SLOTS    127

// ============================================================
// Device Registration / Activation
// ============================================================
// Hardcoded device ID — used as the payload when querying the
// backend API to verify if this unit has been activated.
#define DEVICE_ID    "P001-2607-6AEC-YRH5"

// Base URL of your backend server.
// Using LAN IP for local mock server testing.
// Change to your server PC's LAN IP (e.g. http://192.168.1.50:8000) for production.
#define API_BASE_URL "http://192.168.0.105:8000"

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
bool rtcValid   = false;
uint32_t rtcFallbackOffset = 0;

bool wifiConnecting = false;
unsigned long wifiConnectStart = 0;
uint8_t lastKnownChannel = ESPNOW_CHANNEL;

// NTP sync state (non-blocking)
bool ntpSyncPending = false;
unsigned long ntpSyncStart = 0;

// PING/PONG test state
unsigned long lastPingMs = 0;
bool pongReceived = false;
uint32_t pingCount = 0;
uint32_t pongCount = 0;

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

  if (curCh != lastKnownChannel || force) {
    if (curCh != lastKnownChannel) {
      Serial.printf("[ESP-NOW] Radio channel secretly changed from %d to %d (e.g. by scan). Pre-hopping peer.\n", lastKnownChannel, curCh);
      
      // Temporarily switch radio BACK to CrowPanel's known channel so it can hear us
      esp_wifi_set_channel(lastKnownChannel, WIFI_SECOND_CHAN_NONE);
      
      StaticJsonDocument<64> hop;
      hop["type"] = "CHANNEL_HOP";
      hop["ch"]   = curCh;
      String hopOut; serializeJson(hop, hopOut);
      
      // Add peer temporarily on old channel to dispatch hop
      esp_now_del_peer(CROWPANEL_MAC);
      esp_now_peer_info_t peerOld = {};
      memcpy(peerOld.peer_addr, CROWPANEL_MAC, 6);
      peerOld.channel = lastKnownChannel;
      peerOld.encrypt = false;
      esp_now_add_peer(&peerOld);
      
      send(hopOut);
      delay(100); // Give ESP-NOW time to physically transmit
    } else {
      Serial.printf("[ESP-NOW] Forcing ESP-NOW resync on channel %d\n", curCh);
    }
    
    // Now switch to the new actual channel
    lastKnownChannel = curCh;
    esp_wifi_set_channel(curCh, WIFI_SECOND_CHAN_NONE);
    esp_now_del_peer(CROWPANEL_MAC);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, CROWPANEL_MAC, 6);
    peerInfo.channel = curCh;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    if (force && curCh == lastKnownChannel) {
      StaticJsonDocument<64> hop;
      hop["type"] = "CHANNEL_HOP";
      hop["ch"] = curCh;
      String hopOut; serializeJson(hop, hopOut);
      send(hopOut);
    }
  }
}

// ============================================================
// WiFi Event Handler
// ============================================================
void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WIFI] Connected to AP");
      resyncEspNow(); // Channel usually changes on connect
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WIFI] STA Disconnected");
      // Force back to default channel when disconnected
      esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
      resyncEspNow(); 
      break;
    default:
      break;
  }
}

// ============================================================
// WiFi Command Handlers
// ============================================================
void handleWifiScan() {
  Serial.println("[WIFI] Scanning networks...");

  // Soft disconnect to prepare for scan
  WiFi.disconnect(false);
  delay(100);

  int found = WiFi.scanNetworks(false, true); // blocking, show hidden
  Serial.printf("[WIFI] scanNetworks() returned: %d\n", found);

  if (found == -2) {
    Serial.println("[WIFI] Scan failed, retrying...");
    delay(500);
    found = WiFi.scanNetworks(false, true);
    Serial.printf("[WIFI] Retry returned: %d\n", found);
  }

  String ssidList = "";
  if (found > 0) {
    Serial.printf("[WIFI] %d networks found\n", found);
    int limit = min(found, 5); // Cap at 5
    for (int i = 0; i < limit; i++) {
      if (i > 0) ssidList += ",";
      ssidList += WiFi.SSID(i);
    }
  }
  WiFi.scanDelete();

  // Re-sync ESP-NOW FIRST in case scanNetworks silently desynced the radio,
  // otherwise sendDoc will transmit on the wrong channel and CrowPanel will never hear it!
  resyncEspNow(true);

  StaticJsonDocument<1024> resp;
  resp["type"]  = "WIFI_SCAN_RESULT";
  resp["ssids"] = ssidList;
  sendDoc(resp);
}

void handleWifiConnect(const String& ssidStr, const String& passStr) {
  Serial.printf("[WIFI] Connecting to: '%s'\n", ssidStr.c_str());

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

  // If we are on a non-default channel, tell CrowPanel to go back to default BEFORE we turn off the radio.
  if (lastKnownChannel != ESPNOW_CHANNEL) {
    Serial.printf("[ESP-NOW] Pre-hopping CrowPanel back to default channel (%d).\n", ESPNOW_CHANNEL);
    StaticJsonDocument<64> hop;
    hop["type"] = "CHANNEL_HOP";
    hop["ch"] = ESPNOW_CHANNEL;
    String hopOut; serializeJson(hop, hopOut);
    send(hopOut);
    delay(100);
    lastKnownChannel = ESPNOW_CHANNEL;
  }

  WiFi.disconnect(true, true);
  delay(100);
  
  // Re-init ESP-NOW after full radio off
  extern void espNowInit();
  espNowInit();
  
  // Force channel back and notify
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  resyncEspNow();

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
  // Matches the endpoint:
  // POST /api/devices/registerDevice?device_id=<ID>
  String url = String(API_BASE_URL)
             + "/api/devices/registerDevice"
             + "?device_id=" + DEVICE_ID;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  // Send the activation code (token) securely via Authorization header
  http.addHeader("Authorization", "Bearer " + registrationCode);
  
  int httpCode = http.POST("");   // params are in the URL, body is empty

  Serial.printf("[ACTIVATION] HTTP %d\n", httpCode);

  StaticJsonDocument<128> result;
  result["type"] = "ACTIVATION_RESULT";

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("[ACTIVATION] Response: " + response);

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, response);

    bool success = false;
    if (err == DeserializationError::Ok) {
      // Accept common field names from the server response
      if      (doc.containsKey("activated"))  success = doc["activated"].as<bool>();
      else if (doc.containsKey("is_active"))  success = doc["is_active"].as<bool>();
      else if (doc.containsKey("success"))    success = doc["success"].as<bool>();
      else if (doc.containsKey("status"))     success = (String(doc["status"] | "") == "active");
    } else {
      Serial.println("[ACTIVATION] Could not parse server response.");
    }

    result["success"] = success;
    if (!success) result["err"] = "Code rejected by server";
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

  // Handle PONG reply from CrowPanel (sent as JSON: {"type":"PONG"})
  if (cmd.startsWith("{")) {
    StaticJsonDocument<64> pdoc;
    if (deserializeJson(pdoc, cmd) == DeserializationError::Ok) {
      const char* t = pdoc["type"] | "";
      if (strcmp(t, "PONG") == 0) {
        pongCount++;
        pongReceived = true;
        Serial.printf("[PING] PONG received from CrowPanel! (ping=%u pong=%u)\n", pingCount, pongCount);
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
      sendDoc(wstat);

    } else if (strcmp(action, "GET_WIFI_STATUS") == 0) {
      StaticJsonDocument<128> wstat;
      wstat["type"]      = "WIFI_STATUS";
      wstat["connected"] = (WiFi.status() == WL_CONNECTED);
      sendDoc(wstat);

    } else if (strcmp(action, "FACTORY_RESET") == 0) {
      activated = false;
      Serial.println("[SYSTEM] Factory reset received. Fingerprint scanner disabled.");
      send("{\"type\":\"FACTORY_RESET_ACK\"}");

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
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("[ESP-NOW] SEND FAIL — CP unreachable or MAC mismatch");
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
  Serial.println("  ENROLL:1  -> enroll finger for slot 1 (Alice Santos)");
  Serial.println("  ENROLL:2  -> enroll finger for slot 2 (Bob Cruz)");
  Serial.println("  DELETE:1  -> erase slot 1 from sensor");

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

  // PING CrowPanel every 3 seconds to verify bidirectional comms.
  // Suppress during active WiFi connection so we don't overload the radio.
  if (!wifiConnecting && millis() - lastPingMs >= 3000) {
    lastPingMs = millis();
    pingCount++;
    pongReceived = false;
    send("{\"type\":\"PING\"}");
    Serial.printf("[PING] Sent PING #%u to CrowPanel (awaiting PONG)\n", pingCount);
  }

  // Asynchronous Wi-Fi connection monitoring
  if (wifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting = false;
      Serial.println("[WIFI] Connected! IP: " + WiFi.localIP().toString());
      StaticJsonDocument<128> resp;
      resp["type"]      = "WIFI_STATUS";
      resp["connected"] = true;
      resp["ip"]        = WiFi.localIP().toString();
      sendDoc(resp);
      syncNTP();               // non-blocking — just fires configTime(), result polled below
      // Note: activation is now user-triggered (VALIDATE_ACTIVATION from CrowPanel),
      // not auto-checked on WiFi connect.

    } else if (millis() - wifiConnectStart > 10000) {
      wifiConnecting = false;
      Serial.println("[WIFI] Connection timed out.");
      handleWifiDisconnect();
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
      Serial.println("[<-CP] " + line);
      handleCmd(line);
    }
  }

  // Fingerprint detection — only when activated and not enrolling
  if (activated && !enrolling) {
    if (finger.getImage() == FINGERPRINT_OK) {
      send("{\"type\":\"PLACE_FINGER\"}");
      doMatch();
      delay(1000);   // cooldown so it doesn't trigger multiple times for one touch
    }
  }
}
