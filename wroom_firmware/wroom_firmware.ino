/**
 * wroom_firmware.ino
 * Biometrics Employee Time-In/Time-Out System — Controller Node
 *
 * Board    : ESP32-WROOM-32
 * AS608    : UART1  RX=GPIO16, TX=GPIO17, TOUCH=GPIO34
 * DS3231   : I2C    SDA=GPIO21, SCL=GPIO22
 * CrowPanel: UART2  TX=GPIO33 --> CP IO44, RX=GPIO32 <-- CP IO43
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

// ============================================================
// Pin definitions
// ============================================================
#define PIN_FP_RX    16   // AS608 TX  -> WROOM (UART1 RX)
#define PIN_FP_TX    17   // AS608 RX  <- WROOM (UART1 TX)
#define PIN_FP_TOUCH 34   // AS608 T-OUT  HIGH when finger present
#define PIN_CP_TX    33   // -> CrowPanel IO44  (UART2 TX)
#define PIN_CP_RX    32   // <- CrowPanel IO43  (UART2 RX)
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
bool enrolling = false;
bool rtcValid = false;
uint32_t rtcFallbackOffset = 0; // for software clock fallback

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

  // Handle PONG reply from CrowPanel
  if (cmd == "PONG") {
    pongCount++;
    pongReceived = true;
    Serial.printf("[PING] PONG received from CrowPanel! (ping=%u pong=%u)\n", pingCount, pongCount);
    return;
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

  // CrowPanel UART2
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

void loop() {
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

  // Fingerprint detection (poll sensor directly to avoid T-OUT spam)
  if (!enrolling) {
    if (finger.getImage() == FINGERPRINT_OK) {
      send("{\"type\":\"PLACE_FINGER\"}");
      doMatch();
      delay(1000);   // cooldown so it doesn't trigger multiple times for one touch
    }
  }
}
