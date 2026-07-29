#include "fingerprint_manager.h"
#include "comms.h"
#include "employee_db.h"
#include "time_manager.h"
#include "config.h"
#include "audio_manager.h"

HardwareSerial       fpSerial(1);        // UART1 → AS608
Adafruit_Fingerprint finger(&fpSerial);

// Per-slot IN/OUT toggle. lastIn[slot] == true means the last action was Time In,
// so the next scan for that slot will record Time Out.
static bool lastIn[MAX_SLOTS + 1] = {};

struct L1Slot {
    int empId;
    int fingerIdx;
    unsigned long lastUsed;
    bool active;
};
static L1Slot l1_slots[MAX_SLOTS + 1] = {};

int getL1SlotFor(int empId, int fingerIdx) {
    for (int i = 1; i <= MAX_SLOTS; i++) {
        if (l1_slots[i].active && l1_slots[i].empId == empId && l1_slots[i].fingerIdx == fingerIdx) {
            return i;
        }
    }
    return -1;
}

int assignL1Slot(int empId, int fingerIdx) {
    if (empId >= 1 && empId <= 5) {
        // Reserve slots 1-5 strictly for Admins/System.
        l1_slots[empId].active = true;
        l1_slots[empId].empId = empId;
        l1_slots[empId].fingerIdx = fingerIdx;
        l1_slots[empId].lastUsed = millis();
        return empId;
    }

    // First, check if this (empId, fingerIdx) pair already has a slot.
    // Re-use it so we don't leak slots on overwrite enrollments.
    int existing = getL1SlotFor(empId, fingerIdx);
    if (existing != -1) {
        l1_slots[existing].lastUsed = millis();
        Serial.printf("[L1] Reusing slot %d for empId=%d finger=%d\n", existing, empId, fingerIdx);
        return existing;
    }

    int oldestSlot = 6; // Start eviction check from 6 onwards
    unsigned long oldestTime = 0xFFFFFFFF;
    
    // First, try to find an empty slot (6 to MAX_SLOTS)
    for (int i = 6; i <= MAX_SLOTS; i++) {
        if (!l1_slots[i].active) {
            l1_slots[i].active = true;
            l1_slots[i].empId = empId;
            l1_slots[i].fingerIdx = fingerIdx;
            l1_slots[i].lastUsed = millis();
            return i;
        }
        if (l1_slots[i].lastUsed < oldestTime) {
            oldestTime = l1_slots[i].lastUsed;
            oldestSlot = i;
        }
    }
    
    // Evict oldest slot
    l1_slots[oldestSlot].empId = empId;
    l1_slots[oldestSlot].fingerIdx = fingerIdx;
    l1_slots[oldestSlot].lastUsed = millis();
    return oldestSlot;
}

void deleteL1Slot(int slot) {
    if (slot >= 1 && slot <= MAX_SLOTS) {
#ifndef MOCK_SENSOR
        finger.deleteModel(slot);
#endif
        l1_slots[slot].active = false;
        Serial.printf("[FP] Deleted physical slot %d from AS608\n", slot);
    }
}

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
    
    // Smart Cache Architecture: The CrowPanel SD card is Deep Storage.
    // The AS608 is merely the L1 Cache. We empty it on boot to ensure 
    // it perfectly matches our volatile l1_slots array.
    finger.emptyDatabase();
    Serial.println("[AS608] L1 Cache cleared on boot.");
    
  } else {
    Serial.println("[AS608] NOT FOUND — check wiring!");
  }
}

void doMatch() {
  beep(50); // instant beep upon finger detection
  if (finger.image2Tz()     != FINGERPRINT_OK) { send("{\"type\":\"NOMATCH\"}"); playTrack(TRACK_NOT_RECOGNIZED); return; }
  if (finger.fingerSearch() != FINGERPRINT_OK) { send("{\"type\":\"NOMATCH\"}"); playTrack(TRACK_NOT_RECOGNIZED); return; }

  int    id    = finger.fingerID;
  int    conf  = finger.confidence;
  
  if (!l1_slots[id].active) {
      // Ghost template matched? Should not happen if we wiped it on boot,
      // but just in case, ignore it.
      Serial.printf("[FP] Ghost match on slot %d\n", id);
      return;
  }
  
  l1_slots[id].lastUsed = millis(); // Refresh LRU

  int empId = l1_slots[id].empId;
  int fingerIdx = l1_slots[id].fingerIdx;
  
  // Wait, the MATCH payload currently sends name and dept from WROOM.
  // We need to change the protocol so WROOM sends empId, and CrowPanel resolves it.
  
  bool isIn  = !lastIn[id];
  lastIn[id] = isIn;

  StaticJsonDocument<256> doc;
  doc["type"]   = "MATCH";
  doc["emp_id"] = empId;
  doc["f_idx"]  = fingerIdx;
  doc["action"] = isIn ? "IN" : "OUT";
  doc["conf"]   = conf;
  doc["ts"]     = getTimestamp();
  sendDoc(doc);
  
  if (isIn) {
    playTrack(TRACK_TIME_IN);
  } else {
    playTrack(TRACK_TIME_OUT);
  }
}

void doMockMatch(int slot) {
  if (!l1_slots[slot].active) {
      send("{\"type\":\"NOMATCH\"}");
      Serial.printf("[FP-MOCK] Slot %d is empty, returning NOMATCH\n", slot);
      return;
  }
  
  int empId = l1_slots[slot].empId;
  int fingerIdx = l1_slots[slot].fingerIdx;

  bool isIn  = !lastIn[slot];
  lastIn[slot] = isIn;

  StaticJsonDocument<256> doc;
  doc["type"]   = "MATCH";
  doc["emp_id"] = empId;
  doc["f_idx"]  = fingerIdx;
  doc["action"] = isIn ? "IN" : "OUT";
  doc["conf"]   = 99;
  doc["ts"]     = getTimestamp();
  sendDoc(doc);
  Serial.printf("[FP-MOCK] Generated fake %s match for slot %d (empId: %d)\n", doc["action"].as<const char*>(), slot, empId);
  
  if (isIn) {
    playTrack(TRACK_TIME_IN);
  } else {
    playTrack(TRACK_TIME_OUT);
  }
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

  // Clear serial buffer to prevent leftover garbage from breaking image capture
  while (fpSerial.available()) { fpSerial.read(); delay(1); }

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
  beep(50);
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
  beep(50);
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
  // Flush any trailing garbage
  while (fpSerial.available()) { fpSerial.read(); delay(1); }
  
  Serial.printf("[FP] getTemplateBytes: read %d bytes from slot %d\n", totalRead, slot);
  return totalRead;
#endif
}

bool installTemplateBytes(int slot, const uint8_t* data, size_t len) {
    if (len != 512) return false;

    // 1. Send DownChar command (0x09) for CharBuffer1 (0x01)
    uint8_t cmdPacket[] = {
        0xEF, 0x01, 
        0xFF, 0xFF, 0xFF, 0xFF, 
        0x01, 
        0x00, 0x04, 
        0x09, 
        0x01, 
        0x00, 0x0F
    };
    fpSerial.write(cmdPacket, sizeof(cmdPacket));

    uint8_t b;
    unsigned long deadline = millis() + 1000;
    bool ackOk = false;
    while(millis() < deadline) {
        if(readByteTimeout(fpSerial, b, deadline) && b == 0xEF) {
            readByteTimeout(fpSerial, b, deadline); // 0x01
            for(int i=0; i<4; i++) readByteTimeout(fpSerial, b, deadline); // Addr
            readByteTimeout(fpSerial, b, deadline); // PID (0x07 = ACK)
            readByteTimeout(fpSerial, b, deadline); // LenH
            readByteTimeout(fpSerial, b, deadline); // LenL
            uint8_t confirmCode;
            readByteTimeout(fpSerial, confirmCode, deadline); // Confirmation code
            readByteTimeout(fpSerial, b, deadline); // SumH
            readByteTimeout(fpSerial, b, deadline); // SumL
            if(confirmCode == 0x00) ackOk = true;
            break;
        }
    }
    if(!ackOk) return false;

    // 2. Send 512 bytes in four 128-byte data packets
    for(int i=0; i<4; i++) {
        uint8_t pid = (i == 3) ? 0x08 : 0x02; // End data packet vs Data packet
        uint16_t pktLen = 128 + 2;
        uint16_t sum = pid + (pktLen >> 8) + (pktLen & 0xFF);
        
        uint8_t header[] = {
            0xEF, 0x01, 
            0xFF, 0xFF, 0xFF, 0xFF, 
            pid, 
            (uint8_t)(pktLen >> 8), (uint8_t)(pktLen & 0xFF)
        };
        fpSerial.write(header, sizeof(header));
        
        for(int j=0; j<128; j++) {
            uint8_t d = data[i*128 + j];
            sum += d;
            fpSerial.write(d);
        }
        uint8_t checksum[] = { (uint8_t)(sum >> 8), (uint8_t)(sum & 0xFF) };
        fpSerial.write(checksum, sizeof(checksum));
    }

    // Give sensor a moment to process the buffer
    delay(50);
    
    // Flush the ACK packet that the AS608 sends after the final data packet
    while (fpSerial.available()) { fpSerial.read(); delay(1); }

    // 3. Store the model in flash at 'slot'
    if (finger.storeModel(slot) != FINGERPRINT_OK) {
        Serial.println("[FP] Failed to store installed model");
        return false;
    }
    
    Serial.printf("[FP] Successfully installed template to slot %d\n", slot);
    return true;
}

