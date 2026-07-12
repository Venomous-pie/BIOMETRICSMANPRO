/**
 * wroom_firmware.ino
 * Biometrics Employee Time-In/Time-Out System — Controller Node
 *
 * Board    : ESP32-WROOM-32
 * AS608    : UART1  RX=GPIO16, TX=GPIO17, TOUCH=GPIO34
 * DS3231   : I2C    SDA=GPIO21, SCL=GPIO22
 * CrowPanel: UART2  TX=GPIO33 --> CP IO38, RX=GPIO32 <-- CP IO43
 *
 * Libraries (install via Arduino Library Manager):
 *   - Adafruit Fingerprint Sensor Library  (Adafruit)
 *   - RTClib                               (Adafruit)
 *   - ArduinoJson                          (Benoit Blanchon)
 *
 * Serial commands (USB monitor OR CrowPanel UART):
 *   ENROLL:<slot>   enroll a finger into slot 1-127
 *   DELETE:<slot>   erase stored template from sensor
 */

#include <Adafruit_Fingerprint.h>
#include <Wire.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>

// ============================================================
// Pin definitions
// ============================================================
#define PIN_FP_RX    16   // AS608 TX  -> WROOM (UART1 RX)
#define PIN_FP_TX    17   // AS608 RX  <- WROOM (UART1 TX)
#define PIN_FP_TOUCH 34   // AS608 T-OUT  HIGH when finger present
#define PIN_CP_TX    33   // -> CrowPanel IO44  (UART2 TX)
#define PIN_CP_RX    32   // <- CrowPanel IO43  (UART2 RX)
#define PIN_FACTORY_RESET 14 // Factory Reset hardware button (active low)
#define MAX_SLOTS    127

// ============================================================
// UART instances
// ============================================================
HardwareSerial fpSerial(1);   // UART1 -> AS608
HardwareSerial cpSerial(2);   // UART2 -> CrowPanel

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
  {"id":1,"name":"Alice Santos","dept":"HR"},
  {"id":2,"name":"Bob Cruz","dept":"IT"},
  {"id":3,"name":"Carol Reyes","dept":"Finance"},
  {"id":4,"name":"Dave Lim","dept":"Security"},
  {"id":5,"name":"Eve Tan","dept":"Admin"}
])";

struct Employee { int id; String name; String dept; };
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

// PING/PONG test state
unsigned long lastPingMs = 0;
bool pongReceived = false;
uint32_t pingCount = 0;
uint32_t pongCount = 0;

// ============================================================
// Helpers
// ============================================================

bool lookupEmployee(int id, String &name, String &dept) {
  for (int i = 0; i < empCount; i++) {
    if (empDB[i].id == id) {
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

// Sync time from NTP and update RTC if available
void syncNTP() {
  if (WiFi.status() != WL_CONNECTED) return;
  Serial.println("[NTP] Syncing time...");
  configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com"); // UTC+8 (adjust offset as needed)
  struct tm t;
  if (getLocalTime(&t, 8000)) {
    Serial.printf("[NTP] Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
      t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    if (rtcValid) {
      rtc.adjust(DateTime(t.tm_year+1900, t.tm_mon+1, t.tm_mday,
                          t.tm_hour, t.tm_min, t.tm_sec));
      Serial.println("[NTP] RTC updated from NTP");
    }
  } else {
    Serial.println("[NTP] Sync failed - using existing time");
  }
}

// Full send: UART + Serial log (for important events)
void send(const String &json) {
  cpSerial.println(json);
  Serial.println("[->CP] " + json);
}

// Quiet send: UART only, no Serial spam (used for high-frequency TIME broadcasts)
void sendQuiet(const String &json) {
  cpSerial.println(json);
}

void sendDoc(JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  send(out);
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
    if (millis() - t > 15000) return false;
    p = finger.getImage();
    delay(50);
  } while (p == FINGERPRINT_NOFINGER);
  if (p != FINGERPRINT_OK)              return false;
  if (finger.image2Tz(1) != FINGERPRINT_OK) return false;

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
    if (millis() - t > 15000) return false;
    p = finger.getImage();
    delay(50);
  } while (p == FINGERPRINT_NOFINGER);
  if (p != FINGERPRINT_OK)                return false;
  if (finger.image2Tz(2) != FINGERPRINT_OK)   return false;

  // --- Create & store model ---
  if (finger.createModel()    != FINGERPRINT_OK) return false;
  if (finger.storeModel(slot) != FINGERPRINT_OK) return false;
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

  if (cmd.startsWith("ENROLL:")) {
    int slot = cmd.substring(7).toInt();
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
    int  slot = cmd.substring(7).toInt();
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
      Serial.println("[WIFI] Scanning networks...");

      // Must be in STA mode to scan
      WiFi.disconnect(true);
      WiFi.mode(WIFI_STA);
      delay(100);

      int found = WiFi.scanNetworks(false, true); // blocking, show hidden
      Serial.printf("[WIFI] scanNetworks() returned: %d\n", found);

      StaticJsonDocument<1024> resp;   // 1024 to hold long SSID lists
      resp["type"] = "WIFI_SCAN_RESULT";

      if (found <= 0) {
        // -1 = scan running (shouldn't happen in blocking mode)
        // -2 = scan failed — retry once
        if (found == -2) {
          Serial.println("[WIFI] Scan failed, retrying...");
          delay(500);
          found = WiFi.scanNetworks(false, true);
          Serial.printf("[WIFI] Retry returned: %d\n", found);
        }
        if (found <= 0) {
          resp["ssids"] = "";
          sendDoc(resp);
          WiFi.scanDelete();
          return;
        }
      }

      Serial.printf("[WIFI] %d networks found\n", found);
      String ssidList = "";
      for (int i = 0; i < found; i++) {
        if (i > 0) ssidList += ",";
        ssidList += WiFi.SSID(i);
      }
      WiFi.scanDelete();

      resp["ssids"] = ssidList;
      sendDoc(resp);

    } else if (strcmp(action, "WIFI_CONNECT") == 0) {
      const char *ssid = jcmd["ssid"] | "";
      const char *pass = jcmd["pass"] | "";
      Serial.printf("[WIFI] Connecting to SSID: '%s' (len %d), PASS: '%s' (len %d)\n", ssid, strlen(ssid), pass, strlen(pass));

      // Robust connection sequence for ESP32
      WiFi.disconnect(false, true); // Keep radio on, but ERASE saved AP credentials (clears BSSID lock)
      delay(500);            
      WiFi.mode(WIFI_STA);
      delay(100);
      WiFi.begin(ssid, pass);

      unsigned long t = millis();
      bool connected = false;
      int lastStatus = -1;
      
      while (millis() - t < 15000) {   // 15 s timeout
        int status = WiFi.status();
        if (status != lastStatus) {
            lastStatus = status;
            Serial.printf("[WIFI] Status changed: %d\n", status);
        }
        if (status == WL_CONNECTED) { connected = true; break; }
        
        // If router aggressively rejects us (e.g. WPA3 transition or Fast Roaming), re-attempt immediately
        if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
            Serial.println("[WIFI] Re-attempting begin()...");
            WiFi.disconnect(false, true);
            delay(100);
            WiFi.begin(ssid, pass);
            lastStatus = -1; // force status print again
        }
        
        delay(500);
        Serial.print(".");
      }
      Serial.println();

      StaticJsonDocument<128> resp;
      resp["type"]      = "WIFI_STATUS";
      resp["connected"] = connected;
      if (connected) {
        resp["ip"] = WiFi.localIP().toString();
        Serial.println("[WIFI] Connected! IP: " + WiFi.localIP().toString());
        sendDoc(resp);
        syncNTP();  // Sync time from NTP on successful connect
      } else {
        Serial.println("[WIFI] Connection failed.");
        sendDoc(resp);
      }

    } else if (strcmp(action, "WIFI_DISCONNECT") == 0) {
      Serial.println("[WIFI] Disconnecting...");
      WiFi.disconnect(true, true);
      StaticJsonDocument<128> resp;
      resp["type"]      = "WIFI_STATUS";
      resp["connected"] = false;
      sendDoc(resp);

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
    }
  }
}

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Biometrics WROOM Controller ===");

  // Parse employee JSON into struct array
  StaticJsonDocument<512> empDoc;
  if (!deserializeJson(empDoc, EMPLOYEES_JSON)) {
    for (JsonObject e : empDoc.as<JsonArray>()) {
      if (empCount >= MAX_EMP) break;
      empDB[empCount].id   = e["id"].as<int>();
      empDB[empCount].name = e["name"].as<String>();
      empDB[empCount].dept = e["dept"].as<String>();
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
    Serial.printf("[AS608] Found! Templates stored: %d\n", finger.templateCount);
  } else {
    Serial.println("[AS608] NOT FOUND - check wiring!");
  }

  // CrowPanel UART2 - Large RX buffer so PONG/messages aren't dropped during blocking WiFi scans
  cpSerial.setRxBufferSize(2048);
  cpSerial.begin(115200, SERIAL_8N1, PIN_CP_RX, PIN_CP_TX);
  Serial.println("[UART2] CrowPanel link ready");

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

  // We no longer rely on PIN_FP_TOUCH, polling sensor instead to avoid spam.
  // pinMode(PIN_FP_TOUCH, INPUT);

  // Factory reset button module (Active-High)
  pinMode(PIN_FACTORY_RESET, INPUT_PULLDOWN);

  Serial.println("\nReady. Commands:");
  Serial.println("  ENROLL:1  -> enroll finger for slot 1 (Alice Santos)");
  Serial.println("  ENROLL:2  -> enroll finger for slot 2 (Bob Cruz)");
  Serial.println("  DELETE:1  -> erase slot 1 from sensor");
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

  // PING CrowPanel every 30 seconds to verify bidirectional UART comms
  if (millis() - lastPingMs >= 30000) {
    lastPingMs = millis();
    pingCount++;
    pongReceived = false;
    send("{\"type\":\"PING\"}");
    Serial.printf("[PING] Sent PING #%u to CrowPanel (awaiting PONG)\n", pingCount);
  }

  // Commands from USB Serial Monitor (blocking is OK here, user-typed)
  if (Serial.available()) {
    handleCmd(Serial.readStringUntil('\n'));
  }

  // Commands from CrowPanel - NON-BLOCKING char-by-char
  // (avoids readStringUntil timeout dropping PONG or other replies)
  while (cpSerial.available()) {
    char c = cpSerial.read();
    if (c == '\n') {
      cpBuf.trim();
      if (cpBuf.length() > 0) handleCmd(cpBuf);
      cpBuf = "";
    } else {
      cpBuf += c;
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
