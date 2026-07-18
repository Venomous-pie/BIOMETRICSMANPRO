#include "fingerprint_manager.h"
#include "comms.h"
#include "employee_db.h"
#include "time_manager.h"
#include "config.h"

HardwareSerial       fpSerial(1);        // UART1 → AS608
Adafruit_Fingerprint finger(&fpSerial);

// Per-slot IN/OUT toggle. lastIn[slot] == true means the last action was Time In,
// so the next scan for that slot will record Time Out.
static bool lastIn[MAX_SLOTS + 1] = {};

void fingerprintManagerInit() {
  fpSerial.begin(57600, SERIAL_8N1, PIN_FP_RX, PIN_FP_TX);
  finger.begin(57600);
  delay(100);

  if (finger.verifyPassword()) {
    finger.getParameters();
    int liveCount = 0;
    if (finger.getTemplateCount() == FINGERPRINT_OK) {
      liveCount = finger.templateCount;
    }
    Serial.printf("[AS608] Found! Templates stored: %d\n", liveCount);
  } else {
    Serial.println("[AS608] NOT FOUND — check wiring!");
  }
}

void doMatch() {
  if (finger.image2Tz()     != FINGERPRINT_OK) { send("{\"type\":\"NOMATCH\"}"); return; }
  if (finger.fingerSearch() != FINGERPRINT_OK) { send("{\"type\":\"NOMATCH\"}"); return; }

  int    id    = finger.fingerID;
  int    conf  = finger.confidence;
  String name, dept;
  bool   found = lookupEmployee(id, name, dept);

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

bool doEnroll(int slot) {
  int p;
  unsigned long t;

  // ── Scan 1 ────────────────────────────────────────────────────────────────
  send("{\"type\":\"ENROLL_STEP\",\"step\":1,\"msg\":\"Place finger\"}");
  Serial.println("[ENROLL] Step 1 — place finger on sensor");
  t = millis();
  do {
    if (millis() - t > 15000) { Serial.println("[ENROLL] Step 1 TIMEOUT"); return false; }
    p = finger.getImage();
    if (p != FINGERPRINT_OK && p != FINGERPRINT_NOFINGER)
      Serial.printf("[ENROLL] Step 1 image error: 0x%02X\n", p);
    delay(50);
  } while (p != FINGERPRINT_OK);
  Serial.println("[ENROLL] Step 1 image captured.");
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) { Serial.printf("[ENROLL] Step 1 FAIL: image2Tz(1) = 0x%02X\n", p); return false; }

  // ── Lift ──────────────────────────────────────────────────────────────────
  send("{\"type\":\"ENROLL_STEP\",\"step\":2,\"msg\":\"Lift finger\"}");
  Serial.println("[ENROLL] Step 2 — lift finger");
  do { p = finger.getImage(); delay(50); } while (p != FINGERPRINT_NOFINGER);
  delay(400);

  // ── Scan 2 ────────────────────────────────────────────────────────────────
  send("{\"type\":\"ENROLL_STEP\",\"step\":3,\"msg\":\"Place again\"}");
  Serial.println("[ENROLL] Step 3 — place finger again");
  t = millis();
  do {
    if (millis() - t > 15000) { Serial.println("[ENROLL] Step 3 TIMEOUT"); return false; }
    p = finger.getImage();
    if (p != FINGERPRINT_OK && p != FINGERPRINT_NOFINGER)
      Serial.printf("[ENROLL] Step 3 image error: 0x%02X\n", p);
    delay(50);
  } while (p != FINGERPRINT_OK);
  Serial.println("[ENROLL] Step 3 image captured.");
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) { Serial.printf("[ENROLL] Step 3 FAIL: image2Tz(2) = 0x%02X\n", p); return false; }

  // ── Create and store model ─────────────────────────────────────────────────
  p = finger.createModel();
  if (p != FINGERPRINT_OK) { Serial.printf("[ENROLL] FAIL: createModel = 0x%02X\n", p); return false; }
  p = finger.storeModel(slot);
  if (p != FINGERPRINT_OK) { Serial.printf("[ENROLL] FAIL: storeModel(%d) = 0x%02X\n", slot, p); return false; }
  return true;
}
