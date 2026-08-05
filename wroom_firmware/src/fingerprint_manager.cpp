#include "fingerprint_manager.h"
#include "comms.h"
#include "employee_db.h"
#include "time_manager.h"
#include "config.h"
#include "audio_manager.h"
#include <Preferences.h>   // dedicated NVS handle for admin template storage

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
    // empId == 0 is the reserved admin identity.
    // Admin fingerIdx 0..MAX_ADMIN_FINGERS-1 map to AS608 slots 1..MAX_ADMIN_FINGERS.
    // All admin slots are protected — never evicted by the LRU algorithm.
    if (empId == 0) {
        int adminSlot = fingerIdx + 1;
        if (adminSlot < 1 || adminSlot > MAX_ADMIN_FINGERS) adminSlot = 1;
        l1_slots[adminSlot].active    = true;
        l1_slots[adminSlot].empId     = 0;
        l1_slots[adminSlot].fingerIdx = fingerIdx;
        l1_slots[adminSlot].lastUsed  = millis();
        return adminSlot;
    }

    // Regular employees: slots MAX_ADMIN_FINGERS+1 through MAX_SLOTS.
    int existing = getL1SlotFor(empId, fingerIdx);
    if (existing != -1) {
        l1_slots[existing].lastUsed = millis();
        Serial.printf("[L1] Reusing slot %d for empId=%d finger=%d\n", existing, empId, fingerIdx);
        return existing;
    }

    int empStart  = MAX_ADMIN_FINGERS + 1;
    int oldestSlot = empStart;
    unsigned long oldestTime = 0xFFFFFFFF;

    for (int i = empStart; i <= MAX_SLOTS; i++) {
        if (!l1_slots[i].active) {
            l1_slots[i].active    = true;
            l1_slots[i].empId     = empId;
            l1_slots[i].fingerIdx = fingerIdx;
            l1_slots[i].lastUsed  = millis();
            return i;
        }
        if (l1_slots[i].lastUsed < oldestTime) {
            oldestTime = l1_slots[i].lastUsed;
            oldestSlot = i;
        }
    }

    // Evict oldest employee slot (never evicts admin slots 1..MAX_ADMIN_FINGERS)
    l1_slots[oldestSlot].empId     = empId;
    l1_slots[oldestSlot].fingerIdx = fingerIdx;
    l1_slots[oldestSlot].lastUsed  = millis();
    return oldestSlot;
}

void deleteL1Slot(int slot) {
    if (slot >= 1 && slot <= MAX_SLOTS) {
#ifndef MOCK_SENSOR
        finger.deleteModel(slot);
#endif
        // Admin slots 1..MAX_ADMIN_FINGERS: clear the NVS entry for that finger only.
        if (slot >= 1 && slot <= MAX_ADMIN_FINGERS) {
            clearAdminTemplate(slot - 1); // fingerIdx = slot - 1
        }
        l1_slots[slot].active = false;
        Serial.printf("[FP] Deleted physical slot %d from AS608\n", slot);
    }
}

// ── Admin template NVS persistence ────────────────────────────────────────────
// Admin (empId=0) always occupies AS608 slot 1. Its template is stored in NVS
// so it can be automatically restored after a power cycle without any CrowPanel
// involvement.
//
// IMPORTANT: We use a dedicated Preferences object (not the shared `prefs`
// from wifi_manager) so that admin NVS operations can never be silently
// blocked by another part of the code that left the shared handle open.
static Preferences adminPrefs;

void saveAdminTemplate(int fingerIdx, const uint8_t* data, size_t len) {
    if (fingerIdx < 0 || fingerIdx >= MAX_ADMIN_FINGERS) return;
    if (!adminPrefs.begin("admin_fp", false)) {
        Serial.println("[ADMIN] ERROR: Could not open NVS namespace for save.");
        return;
    }
    char keyTpl[6], keyLen[6];
    snprintf(keyTpl, sizeof(keyTpl), "tpl%d", fingerIdx);
    snprintf(keyLen, sizeof(keyLen), "len%d", fingerIdx);
    adminPrefs.putBytes(keyTpl, data, len);
    adminPrefs.putUInt(keyLen, (uint32_t)len);
    adminPrefs.end();
    Serial.printf("[ADMIN] Saved finger %d template (%u bytes) to NVS.\n", fingerIdx, len);
}

bool loadAdminTemplate(int fingerIdx, uint8_t* outData, size_t maxLen, size_t* outLen) {
    *outLen = 0;
    if (fingerIdx < 0 || fingerIdx >= MAX_ADMIN_FINGERS) return false;
    if (!adminPrefs.begin("admin_fp", true)) {
        Serial.println("[ADMIN] ERROR: Could not open NVS namespace for load.");
        return false;
    }
    char keyTpl[6], keyLen[6];
    snprintf(keyTpl, sizeof(keyTpl), "tpl%d", fingerIdx);
    snprintf(keyLen, sizeof(keyLen), "len%d", fingerIdx);
    size_t stored = adminPrefs.getUInt(keyLen, 0);
    if (stored == 0 || stored > maxLen) {
        adminPrefs.end();
        return false;
    }
    size_t got = adminPrefs.getBytes(keyTpl, outData, stored);
    adminPrefs.end();
    if (got != stored) {
        Serial.printf("[ADMIN] NVS read mismatch for finger %d: expected %u, got %u.\n", fingerIdx, stored, got);
        return false;
    }
    *outLen = got;
    Serial.printf("[ADMIN] Loaded finger %d template (%u bytes) from NVS.\n", fingerIdx, got);
    return true;
}

void clearAdminTemplate(int fingerIdx) {
    if (!adminPrefs.begin("admin_fp", false)) {
        Serial.println("[ADMIN] ERROR: Could not open NVS namespace for clear.");
        return;
    }
    if (fingerIdx < 0) {
        // Clear all admin templates
        adminPrefs.clear();
        adminPrefs.end();
        Serial.println("[ADMIN] All admin templates cleared from NVS.");
        return;
    }
    char keyTpl[6], keyLen[6];
    snprintf(keyTpl, sizeof(keyTpl), "tpl%d", fingerIdx);
    snprintf(keyLen, sizeof(keyLen), "len%d", fingerIdx);
    adminPrefs.remove(keyTpl);
    adminPrefs.remove(keyLen);
    adminPrefs.end();
    Serial.printf("[ADMIN] Cleared finger %d template from NVS.\n", fingerIdx);
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

    // Smart Cache Architecture — employee slots are ephemeral (rebuilt from
    // CrowPanel SD on demand), so clear them on boot. Admin slots
    // (1..MAX_ADMIN_FINGERS) are NEVER erased: AS608 flash is non-volatile.
    for (int i = MAX_ADMIN_FINGERS + 1; i <= MAX_SLOTS; i++) {
      finger.deleteModel(i);
    }
    Serial.printf("[AS608] Employee slots cleared; admin slots 1-%d preserved.\n", MAX_ADMIN_FINGERS);

    // Restore all enrolled admin fingerprints.
    // For each fingerIdx 0..MAX_ADMIN_FINGERS-1:
    //   1. If the AS608 slot is still in flash → just activate l1_slots.
    //   2. Else if NVS has the template → restore via installTemplateBytes.
    static uint8_t adminTpl[768];
    int adminCount = 0;
    for (int fi = 0; fi < MAX_ADMIN_FINGERS; fi++) {
      int    slot   = fi + 1;
      size_t tplLen = 0;
      bool   hasNvs = loadAdminTemplate(fi, adminTpl, sizeof(adminTpl), &tplLen);

      // Check if this slot survived in AS608 flash.
      bool inFlash = (finger.loadModel(slot) == FINGERPRINT_OK);

      if (inFlash) {
        l1_slots[slot].active    = true;
        l1_slots[slot].empId     = 0;
        l1_slots[slot].fingerIdx = fi;
        l1_slots[slot].lastUsed  = millis();
        Serial.printf("[ADMIN] Finger %d active (AS608 slot %d from flash).\n", fi, slot);
        adminCount++;
      } else if (hasNvs && tplLen > 0) {
        Serial.printf("[ADMIN] Finger %d: slot %d empty, restoring from NVS...\n", fi, slot);
        if (installTemplateBytes(slot, adminTpl, tplLen)) {
          l1_slots[slot].active    = true;
          l1_slots[slot].empId     = 0;
          l1_slots[slot].fingerIdx = fi;
          l1_slots[slot].lastUsed  = millis();
          Serial.printf("[ADMIN] Finger %d restored to slot %d from NVS.\n", fi, slot);
          adminCount++;
        } else {
          Serial.printf("[ADMIN] WARNING: NVS restore failed for finger %d.\n", fi);
        }
      }
      // else: this fingerIdx was never enrolled — skip silently.
    }
    Serial.printf("[ADMIN] %d admin fingerprint(s) active.\n", adminCount);

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
// We call loadModel() to reload from flash, then getModel() (UpChar 0x08) to
// stream it back over UART. We drain the packets and return the raw bytes.
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

  // Drain the UART. Collect only the payload bytes from data/end-data packets.
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

// ── Template installation (DownChar + Store) ──────────────────────────────────
// Loads a 512-byte template blob into AS608 CharBuffer1 via DownChar, then
// commits it to flash with storeModel.
//
// CRITICAL: The data packets during DownChar MUST use the packet size the sensor
// is configured for (finger.packet_len, set by getParameters() on boot).
// Sending 128-byte packets to a sensor configured for 256 bytes silently corrupts
// the CharBuffer and makes storeModel write garbage — causing NOMATCH after reboot.

bool installTemplateBytes(int slot, const uint8_t* data, size_t len) {
    if (len != 512) {
        Serial.printf("[FP] installTemplateBytes: bad len=%u (need 512)\n", len);
        return false;
    }

    // Determine the packet data size the sensor expects for DownChar.
    // finger.packet_len: 0=32B, 1=64B, 2=128B, 3=256B (set by getParameters())
    static const int pktSizes[] = {32, 64, 128, 256};
    int pktDataLen = 128; // safe default
    if (finger.packet_len >= 0 && finger.packet_len < 4) {
        pktDataLen = pktSizes[finger.packet_len];
    }
    int numPackets = (int)len / pktDataLen; // e.g. 512/256=2 or 512/128=4
    Serial.printf("[FP] DownChar: %d-byte packets, %d total\n", pktDataLen, numPackets);

    // Flush any stale UART bytes before starting
    while (fpSerial.available()) { fpSerial.read(); delay(1); }

    // ── Step 1: DownChar command ──────────────────────────────────────────────
    // Checksum = PID+LenH+LenL+Cmd+BufID = 0x01+0x00+0x04+0x09+0x01 = 0x0F
    const uint8_t cmdPkt[] = {
        0xEF, 0x01,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x01,       // PID: command packet
        0x00, 0x04, // Length: 4 bytes
        0x09,       // DownChar
        0x01,       // CharBuffer1
        0x00, 0x0F  // Checksum
    };
    fpSerial.write(cmdPkt, sizeof(cmdPkt));
    fpSerial.flush();

    // Wait for DownChar ACK
    uint8_t b2 = 0;
    unsigned long dl = millis() + 2000;
    bool ackOk = false;
    while (millis() < dl) {
        if (readByteTimeout(fpSerial, b2, dl) && b2 == 0xEF) {
            readByteTimeout(fpSerial, b2, dl);
            for (int k = 0; k < 4; k++) readByteTimeout(fpSerial, b2, dl);
            readByteTimeout(fpSerial, b2, dl); // PID
            readByteTimeout(fpSerial, b2, dl); // LenH
            readByteTimeout(fpSerial, b2, dl); // LenL
            uint8_t cc = 0;
            readByteTimeout(fpSerial, cc, dl);
            readByteTimeout(fpSerial, b2, dl);
            readByteTimeout(fpSerial, b2, dl);
            if (cc == 0x00) ackOk = true;
            else Serial.printf("[FP] DownChar ACK error: 0x%02X\n", cc);
            break;
        }
    }
    if (!ackOk) {
        Serial.println("[FP] installTemplateBytes: DownChar ACK not received");
        return false;
    }

    // ── Step 2: Data packets using the sensor's native packet size ────────────
    for (int i = 0; i < numPackets; i++) {
        bool     last   = (i == numPackets - 1);
        uint8_t  pid    = last ? 0x08 : 0x02;
        uint16_t pktLen = (uint16_t)(pktDataLen + 2); // data bytes + 2 checksum bytes
        uint16_t sum    = (uint16_t)pid + (pktLen >> 8) + (pktLen & 0xFF);

        const uint8_t hdr[] = {
            0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
            pid,
            (uint8_t)(pktLen >> 8), (uint8_t)(pktLen & 0xFF)
        };
        fpSerial.write(hdr, sizeof(hdr));

        for (int j = 0; j < pktDataLen; j++) {
            uint8_t d = data[i * pktDataLen + j];
            sum += d;
            fpSerial.write(d);
        }
        const uint8_t csum[] = { (uint8_t)(sum >> 8), (uint8_t)(sum & 0xFF) };
        fpSerial.write(csum, sizeof(csum));
        fpSerial.flush(); // Fully transmit before sending next packet
    }

    // ── Step 3: Post-data ACK ─────────────────────────────────────────────────
    // Many AS608 variants ACK after the final data packet.
    // Unread, it would corrupt storeModel()'s ACK. Some clones omit it.
    bool dataAckOk = false;
    dl = millis() + 1000;
    while (millis() < dl) {
        if (fpSerial.available()) {
            b2 = fpSerial.read();
            if (b2 == 0xEF) {
                readByteTimeout(fpSerial, b2, dl);
                for (int k = 0; k < 4; k++) readByteTimeout(fpSerial, b2, dl);
                readByteTimeout(fpSerial, b2, dl); // PID
                readByteTimeout(fpSerial, b2, dl); // LenH
                readByteTimeout(fpSerial, b2, dl); // LenL
                uint8_t cc = 0;
                readByteTimeout(fpSerial, cc, dl);
                readByteTimeout(fpSerial, b2, dl);
                readByteTimeout(fpSerial, b2, dl);
                if (cc == 0x00) dataAckOk = true;
                else Serial.printf("[FP] Post-data ACK error: 0x%02X\n", cc);
                break;
            }
        }
        delay(1);
    }
    if (!dataAckOk) {
        Serial.println("[FP] No post-data ACK (normal for some sensor variants).");
    }

    delay(100); // Let sensor finish writing CharBuffer1

    // ── Step 4: storeModel — commit CharBuffer1 to flash ─────────────────────
    uint8_t result = finger.storeModel(slot);
    if (result != FINGERPRINT_OK) {
        Serial.printf("[FP] storeModel(%d) FAILED: 0x%02X\n", slot, result);
        return false;
    }

    delay(50);
    Serial.printf("[FP] installTemplateBytes: slot %d OK (pktSize=%d)\n", slot, pktDataLen);
    return true;
}
