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

// ── Enroll cancellation ───────────────────────────────────────────────────────
// Drains the inbound ESP-NOW queue and sets enrollCancelled if a CANCEL_ENROLL
// message is found. Called on every poll tick inside doEnroll()'s wait loops so
// the cancel arrives in <50 ms rather than after the 15-second timeout.
static inline void pollCancelCheck() {
  while (!cpQueueEmpty()) {
    String msg = cpQueuePop();
    msg.trim();
    if (msg == "CANCEL_ENROLL") {
      enrollCancelled = true;
      Serial.println("[ENROLL] Cancel received via ESP-NOW during scan.");
    }
    // Other queued messages are intentionally dropped here — they will be
    // re-processed by loop() after doEnroll() returns (the queue is separate).
    // Note: only CANCEL_ENROLL is time-critical inside the blocking scan loop.
  }
}

// ── UART helpers ──────────────────────────────────────────────────────────────
// Block until the UART has a byte available or deadline is reached.
// Returns true if a byte was read, false on timeout.
static bool readByteTimeout(HardwareSerial& s, uint8_t& out, unsigned long deadline) {
  while (millis() < deadline) {
    if (s.available()) { out = s.read(); return true; }
    delay(1);
  }
  return false;
}

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

void doMockMatch(int slot) {
  bool isIn  = !lastIn[slot];
  lastIn[slot] = isIn;

  StaticJsonDocument<256> doc;
  doc["type"]   = "MATCH";
  doc["id"]     = slot;
  doc["name"]   = "Slot " + String(slot);
  doc["dept"]   = "";
  doc["action"] = isIn ? "IN" : "OUT";
  doc["conf"]   = 99;
  doc["ts"]     = getTimestamp();
  sendDoc(doc);
  Serial.printf("[FP-MOCK] Generated fake %s match for slot %d\n", doc["action"].as<const char*>(), slot);
}

volatile bool enrollCancelled = false;

void cancelEnroll() {
  enrollCancelled = true;
  Serial.println("[ENROLL] Cancel requested by CrowPanel.");
}

bool doEnroll(int slot) {
#ifdef MOCK_SENSOR
  enrollCancelled = false;
  
  send("{\"type\":\"ENROLL_STEP\",\"step\":1,\"msg\":\"Place finger\"}");
  Serial.println("[ENROLL-MOCK] Step 1 — place finger on sensor");
  for(int i=0; i<15; i++) { delay(100); pollCancelCheck(); if(enrollCancelled) return false; }

  send("{\"type\":\"ENROLL_STEP\",\"step\":2,\"msg\":\"Lift finger\"}");
  Serial.println("[ENROLL-MOCK] Step 2 — lift finger");
  for(int i=0; i<15; i++) { delay(100); pollCancelCheck(); if(enrollCancelled) return false; }

  send("{\"type\":\"ENROLL_STEP\",\"step\":3,\"msg\":\"Place again\"}");
  Serial.println("[ENROLL-MOCK] Step 3 — place finger again");
  for(int i=0; i<15; i++) { delay(100); pollCancelCheck(); if(enrollCancelled) return false; }

  Serial.println("[ENROLL-MOCK] Mock enrollment successful.");
  return true;
#else
  int p;
  unsigned long t, lastPing;
  enrollCancelled = false;

  // ── Scan 1 ────────────────────────────────────────────────────────────────
  send("{\"type\":\"ENROLL_STEP\",\"step\":1,\"msg\":\"Place finger\"}");
  Serial.println("[ENROLL] Step 1 — place finger on sensor");
  t = millis();
  lastPing = t;
  do {
    pollCancelCheck();
    if (enrollCancelled) { Serial.println("[ENROLL] Cancelled at step 1."); return false; }
    if (millis() - t > 15000) { Serial.println("[ENROLL] Step 1 TIMEOUT"); return false; }
    if (millis() - lastPing > 2000) {
        sendQuiet("{\"type\":\"PING\"}");
        lastPing = millis();
    }
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
  lastPing = millis();
  do {
    pollCancelCheck();
    if (enrollCancelled) { Serial.println("[ENROLL] Cancelled at step 2."); return false; }
    if (millis() - lastPing > 2000) {
        sendQuiet("{\"type\":\"PING\"}");
        lastPing = millis();
    }
    p = finger.getImage();
    delay(50);
  } while (p != FINGERPRINT_NOFINGER);
  delay(400);

  // ── Scan 2 ────────────────────────────────────────────────────────────────
  send("{\"type\":\"ENROLL_STEP\",\"step\":3,\"msg\":\"Place again\"}");
  Serial.println("[ENROLL] Step 3 — place finger again");
  t = millis();
  lastPing = t;
  do {
    pollCancelCheck();
    if (enrollCancelled) { Serial.println("[ENROLL] Cancelled at step 3."); return false; }
    if (millis() - t > 15000) { Serial.println("[ENROLL] Step 3 TIMEOUT"); return false; }
    if (millis() - lastPing > 2000) {
        sendQuiet("{\"type\":\"PING\"}");
        lastPing = millis();
    }
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
#endif
}

// ── Template extraction ───────────────────────────────────────────────────────
// After storeModel() the template is still live in the sensor's char buffer.
// We call getModel() (UpChar command 0x08) which makes the AS608 stream the
// buffer contents back over UART as structured packets, then we drain the UART
// into our flat buffer.
//
// Packet wire format (from AS608 datasheet):
//   [EF01][FFFFFFFF][type 1B][length 2B][data 0-256B][checksum 2B]
//
// Data packets have type 0x02; the final packet has type 0x08 (end-data).
// We strip headers/checksums and copy only the payload bytes.

int getTemplateBytes(int slot, uint8_t* buf, size_t bufSize) {
#ifdef MOCK_SENSOR
  // Generate a fake 512-byte template to test the base64 / upload flow
  int len = (bufSize < 512) ? bufSize : 512;
  for (int i = 0; i < len; i++) {
    buf[i] = (uint8_t)(i % 256);
  }
  Serial.printf("[FP-MOCK] Generated %d dummy bytes for slot %d\n", len, slot);
  return len;
#else
  // loadModel re-reads the template from flash into the sensor's char buffer.
  if (finger.loadModel(slot) != FINGERPRINT_OK) {
    Serial.println("[FP] getTemplateBytes: loadModel failed");
    return 0;
  }

  // getModel() sends the "UpChar" command (0x08); the AS608 then streams
  // the buffer contents as structured packets back over the same UART.
  if (finger.getModel() != FINGERPRINT_OK) {
    Serial.println("[FP] getTemplateBytes: getModel failed");
    return 0;
  }

  // Drain the UART. Packet wire format (AS608 datasheet):
  //   Header[EF 01](2) + Address(4) + PktType(1) + PktLen(2) + Data(n) + CRC(2)
  // Data packets = type 0x02; End-data packet = type 0x08.
  // We collect only the payload Data bytes from those two packet types.
  int totalRead = 0;
  unsigned long deadline = millis() + 3000; // 3-second hard timeout
  uint8_t b = 0;

  while (millis() < deadline && totalRead < (int)bufSize) {
    // Sync on EF 01 header
    if (!readByteTimeout(fpSerial, b, deadline) || b != 0xEF) continue;
    if (!readByteTimeout(fpSerial, b, deadline) || b != 0x01) continue;

    // Skip 4-byte address
    for (int i = 0; i < 4; i++) if (!readByteTimeout(fpSerial, b, deadline)) goto done;

    // Packet type
    uint8_t pktType;
    if (!readByteTimeout(fpSerial, pktType, deadline)) break;

    // Packet length (includes 2-byte CRC)
    uint8_t lenHi, lenLo;
    if (!readByteTimeout(fpSerial, lenHi, deadline)) break;
    if (!readByteTimeout(fpSerial, lenLo, deadline)) break;
    int dataLen = (((int)lenHi << 8) | lenLo) - 2;

    // Read payload
    for (int i = 0; i < dataLen; i++) {
      if (!readByteTimeout(fpSerial, b, deadline)) goto done;
      if ((pktType == FINGERPRINT_DATAPACKET || pktType == FINGERPRINT_ENDDATAPACKET)
          && totalRead < (int)bufSize) {
        buf[totalRead++] = b;
      }
    }

    // Skip 2-byte CRC
    readByteTimeout(fpSerial, b, deadline);
    readByteTimeout(fpSerial, b, deadline);

    if (pktType == FINGERPRINT_ENDDATAPACKET) break;
  }

done:
  Serial.printf("[FP] getTemplateBytes: read %d bytes from slot %d\n", totalRead, slot);
  return totalRead;
#endif
}

