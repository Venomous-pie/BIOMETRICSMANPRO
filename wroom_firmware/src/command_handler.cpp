#include "command_handler.h"
#include "comms.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "fingerprint_manager.h"
#include "activation.h"
#include "employee_db.h"
#include "sync_manager.h"
#include "audio_manager.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <mbedtls/base64.h>

bool activated          = false;
bool enrolling          = false;
String deviceToken      = "";
bool idle_screen_active = false;

static unsigned long btnPressTime = 0;
static bool          btnHeld      = false;

void wipeExceptAdmin() {
  Serial.println("[SYSTEM] Wiping database except slots 1-5 (Admin)...");
#ifdef MOCK_SENSOR
  Serial.println("[SYSTEM] MOCK_SENSOR active — skipping hardware wipe to prevent timeouts.");
#else
  for (int i = 6; i <= MAX_SLOTS; i++) {
    finger.deleteModel(i);
  }
#endif
  Serial.println("[SYSTEM] Wipe complete.");
}

// ── Factory reset button ──────────────────────────────────────────────────────

void handleFactoryResetButton() {
  if (digitalRead(PIN_FACTORY_RESET) == HIGH) {
    if (!btnHeld) {
      btnHeld      = true;
      btnPressTime = millis();
      Serial.println("[SYSTEM] Factory reset button held. Release within 5 s to cancel.");
    } else if (millis() - btnPressTime > 5000) {
      Serial.println("[SYSTEM] HARDWARE FACTORY RESET TRIGGERED.");
      send("{\"type\":\"FACTORY_RESET_ACK\"}");
      delay(100); // Allow time for ESP-NOW packet to transmit
      WiFi.disconnect(true, true);
      prefs.clear();
      wipeExceptAdmin();
      Serial.println("[SYSTEM] WiFi credentials and fingerprint templates wiped. Rebooting...");
      beep(1000); // 1-second long beep to confirm reset
      delay(2000);
      ESP.restart();
    }
  } else {
    if (btnHeld) Serial.println("[SYSTEM] Factory reset button released — cancelled.");
    btnHeld = false;
  }
}

// ── Fingerprint poll ──────────────────────────────────────────────────────────

void fingerprintPoll() {
  if (!activated || enrolling || !idle_screen_active) return;
  if (finger.getImage() == FINGERPRINT_OK) {
    send("{\"type\":\"PLACE_FINGER\"}");
    doMatch();
    delay(1000); // cooldown prevents multiple triggers from a single finger touch
  }
}

// ── Command dispatcher ────────────────────────────────────────────────────────

void handleCmd(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // PING from CrowPanel — reply quietly to avoid polluting the Serial monitor.
  if (cmd.startsWith("{")) {
    StaticJsonDocument<64> pdoc;
    if (deserializeJson(pdoc, cmd) == DeserializationError::Ok) {
      if (strcmp(pdoc["type"] | "", "PING") == 0) {
        sendQuiet("{\"type\":\"PONG\"}");
        return;
      }
    }
  }

  Serial.println("[CMD] " + cmd);

  if (cmd == "RESET") {
    Serial.println("[SYSTEM] Reboot command received. Restarting...");
    send("{\"type\":\"RESET_ACK\"}");
    delay(200);
    ESP.restart();

  } else if (cmd == "TEST_HW") {
    Serial.println("[TEST] Testing Hardware Wiring...");
    Serial.println("[TEST] 1. Beeping Buzzer 3 times...");
    for (int i = 0; i < 3; i++) {
        beep(100);
        delay(100);
    }
    Serial.println("[TEST] 2. Playing Test Audio (Track 1)...");
    playTrack(TRACK_TIME_IN);
    Serial.println("[TEST] If you heard 3 beeps and a voice, your wiring is perfect!");

  } else if (cmd == "GHOST_LOGIN" || cmd == "NUKE_USERS" || cmd == "NUKE_DB" || cmd == "DEBUG_COMMS") {
    // Development backdoor commands — forwarded to the CrowPanel for execution.
    Serial.println("[FWD->CP] Forwarding backdoor command: " + cmd);
    send("{\"type\":\"BACKDOOR\",\"cmd\":\"" + cmd + "\"}");

  } else if (cmd == "CANCEL_ENROLL") {
    cancelEnroll();
    Serial.println("[ENROLL] Enrollment cancelled by CrowPanel.");

  } else if (cmd.startsWith("MOCK_SCAN:")) {
    int slot = cmd.substring(10).toInt();
    if (slot >= 1 && slot <= MAX_SLOTS) {
        Serial.printf("[SYSTEM] Triggering MOCK_SCAN for slot %d\n", slot);
        doMockMatch(slot);
    } else {
        Serial.println("[SYSTEM] MOCK_SCAN failed: Invalid slot.");
    }

  } else if (cmd.startsWith("DELETE_FP:")) {
    int slot = cmd.substring(10).toInt();
    if (slot >= 1 && slot <= MAX_SLOTS) {
        deleteL1Slot(slot);
        send("{\"type\":\"DELETE_FP_OK\",\"slot\":" + String(slot) + "}");
    } else {
        Serial.println("[SYSTEM] DELETE_FP failed: Invalid slot.");
    }

  } else if (cmd.startsWith("ENROLL:")) {
    int colonIdx    = cmd.indexOf(':', 7);
    int slot        = 0;
    int emp_id      = 0;
    int finger_index = 0;
    String parsedName = "";

    if (colonIdx != -1) {
      // New format: ENROLL:<emp_id>:<finger_index>[:<name>]
      int nextColon = cmd.indexOf(':', colonIdx + 1);
      emp_id = cmd.substring(7, colonIdx).toInt();
      
      if (nextColon != -1) {
          finger_index = cmd.substring(colonIdx + 1, nextColon).toInt();
          parsedName = cmd.substring(nextColon + 1);
      } else {
          finger_index = cmd.substring(colonIdx + 1).toInt();
      }
      
      slot = assignL1Slot(emp_id, finger_index);
      if (slot < 1 || slot > MAX_SLOTS) {
        Serial.println("[ENROLL] L1 Cache full/error.");
        return;
      }
    } else {
      // Legacy format: ENROLL:<slot>
      slot         = cmd.substring(7).toInt();
      finger_index = (slot - 1) % 10; // derive from slot
    }

    if (slot < 1 || slot > MAX_SLOTS) {
      Serial.println("[ENROLL] Invalid slot.");
      return;
    }

    String label = parsedName.length() ? parsedName : ("Slot " + String(slot));

    StaticJsonDocument<128> doc;
    doc["type"] = "ENROLL_START";
    doc["slot"] = slot;
    doc["name"] = label;
    sendDoc(doc);

    enrolling = true;
    bool ok   = doEnroll(slot);
    enrolling = false;

    // If the user cancelled, the CrowPanel already navigated away —
    // sending ENROLL_FAIL would show a spurious error on the screen.
    if (enrollCancelled) {
      Serial.println("[ENROLL] Cancelled — suppressing ENROLL_FAIL to CrowPanel.");
      enrollCancelled = false;
      return;
    }

    String errMsg = "";
    if (ok) {
      // Extract template bytes immediately while the AS608 buffer is still hot
      static uint8_t tplBuf[768];
      int tplLen = getTemplateBytes(slot, tplBuf, sizeof(tplBuf));
      String b64 = base64Encode(tplBuf, tplLen);
      int chunkSize = 140; // Leaves plenty of headroom for the JSON wrapper under 250 bytes
      int totalChunks = (b64.length() + chunkSize - 1) / chunkSize;
      
      for (int i = 0; i < totalChunks; i++) {
        StaticJsonDocument<512> chunkDoc;
        chunkDoc["type"] = "ENROLL_CHUNK";
        chunkDoc["slot"] = slot;
        chunkDoc["name"] = label;
        chunkDoc["emp_id"] = emp_id;
        chunkDoc["idx"] = finger_index;
        chunkDoc["c"] = i;
        chunkDoc["t"] = totalChunks;
        chunkDoc["d"] = b64.substring(i * chunkSize, (i + 1) * chunkSize);
        
        // Send directly. Using sendQuiet to avoid flooding serial output.
        String out;
        serializeJson(chunkDoc, out);
        sendQuiet(out);
        delay(40); // Allow ESP-NOW to physically transmit
      }
    }

    doc.clear();
    doc["type"] = ok ? "ENROLL_OK" : "ENROLL_FAIL";
    doc["slot"] = slot;
    if (ok) {
      doc["name"] = label;
      playTrack(TRACK_ENROLLED);
    } else if (errMsg.length() > 0) {
      doc["err"] = errMsg;
      playTrack(TRACK_ENROLL_FAILED);
    } else {
      playTrack(TRACK_ENROLL_FAILED);
    }
    sendDoc(doc);
    Serial.println(ok ? "[ENROLL] Success." : "[ENROLL] Failed.");

  } else if (cmd.startsWith("DELETE:")) {
    int colonIdx = cmd.indexOf(':', 7);
    int slot     = 0;
    bool ok      = false;

    if (colonIdx != -1) {
      int emp_id       = cmd.substring(7, colonIdx).toInt();
      int finger_index = cmd.substring(colonIdx + 1).toInt();
      // Look up the actual L1 slot for this (empId, fingerIdx) pair.
      // The old formula ((emp_id-1)*10)+finger_index+1 was from the legacy
      // fixed-slot scheme and is wrong now that assignL1Slot is dynamic.
      slot = getL1SlotFor(emp_id, finger_index);
      if (slot != -1) {
        deleteL1Slot(slot); // deletes from AS608 + clears l1_slots entry
        ok = true;
        Serial.printf("[DEL] Released L1 slot %d for empId=%d finger=%d\n", slot, emp_id, finger_index);
      } else {
        Serial.printf("[DEL] No L1 slot found for empId=%d finger=%d\n", emp_id, finger_index);
        ok = true; // Not a fatal error — template may already be gone
      }
    } else {
      slot = cmd.substring(7).toInt();
      if (slot >= 1 && slot <= MAX_SLOTS) {
        deleteL1Slot(slot);
        ok = true;
      }
    }

    StaticJsonDocument<64> doc;
    doc["type"] = ok ? "DELETE_OK" : "DELETE_FAIL";
    doc["slot"] = slot > 0 ? slot : 0;
    sendDoc(doc);
    Serial.println(ok ? "[DEL] Slot " + String(slot) + " erased." : "[DEL] Failed.");

  } else if (cmd.startsWith("WIPE_ALL")) {
    wipeExceptAdmin();
    Serial.println("[SYSTEM] Fingerprint database completely wiped via WIPE_ALL command.");

  } else if (cmd.startsWith("{")) {
    // ── JSON command from CrowPanel ──────────────────────────────────────────
    StaticJsonDocument<256> jcmd;
    if (deserializeJson(jcmd, cmd) != DeserializationError::Ok) {
      Serial.println("[CMD] Malformed JSON: " + cmd);
      return;
    }
    const char *action = jcmd["cmd"] | "";
    const char *type = jcmd["type"] | "";

    if (strcmp(type, "CACHE_CHUNK") == 0) {
      static String b64Buffer = "";
      int c = jcmd["c"] | 0;
      int t = jcmd["t"] | 0;
      int empId = jcmd["emp_id"] | 0;
      int fIdx = jcmd["f_idx"] | 0;
      
      if (c == 0) b64Buffer = "";
      b64Buffer += jcmd["d"].as<String>();
      
      if (c == t - 1) {
          size_t outputLen = 0;
          unsigned char decodeBuf[768];
          int ret = mbedtls_base64_decode(decodeBuf, sizeof(decodeBuf), &outputLen, (const unsigned char*)b64Buffer.c_str(), b64Buffer.length());
          if (ret == 0 && outputLen == 512) {
              int slot = assignL1Slot(empId, fIdx);
              installTemplateBytes(slot, decodeBuf, outputLen);
              Serial.printf("[CACHE] Cached emp %d finger %d to L1 slot %d\n", empId, fIdx, slot);
          } else {
              Serial.println("[CACHE] Failed to decode base64 template chunk.");
          }
      }
    } else if (strcmp(action, "DEBUG") == 0) {
      Serial.println(jcmd["msg"].as<String>());

    } else if (strcmp(action, "WIFI_SCAN") == 0) {
      handleWifiScan();

    } else if (strcmp(action, "WIFI_CONNECT") == 0) {
      handleWifiConnect(jcmd["ssid"].as<String>(), jcmd["pass"].as<String>());

    } else if (strcmp(action, "WIFI_DISCONNECT") == 0) {
      handleWifiDisconnect();

    } else if (strcmp(action, "SYNC_NTP") == 0) {
      Serial.println("[NTP] Manual sync requested by CrowPanel.");
      syncNTP();

    } else if (strcmp(action, "SET_TIME") == 0) {
      Serial.println("[TIME] Manual time setup requested by CrowPanel.");
      int y   = jcmd["y"] | 2024;
      int m   = jcmd["m"] | 1;
      int d   = jcmd["d"] | 1;
      int h   = jcmd["h"] | 0;
      int min = jcmd["min"] | 0;
      setManualTime(y, m, d, h, min);

    } else if (strcmp(action, "TEST_API") == 0) {
      Serial.println("[TEST_API] API connection test requested.");
      testApiConnection();

    } else if (strcmp(action, "VALIDATE_ACTIVATION") == 0) {
      String regCode = jcmd["registration_code"] | "";
      if (regCode.length() == 0) {
        send("{\"type\":\"ACTIVATION_RESULT\",\"success\":false,\"err\":\"Empty registration code\"}");
      } else {
        validateActivationWithServer(regCode);
      }

    } else if (strcmp(action, "DEVICE_ACTIVATED") == 0) {
      activated = true;
      Serial.println("[SYSTEM] Device activated. Fingerprint scanner enabled.");
      String token = jcmd["token"] | "";
      if (token.length() > 0) {
        deviceToken = token;
        Serial.printf("[SYSTEM] Activation Token stored: %s\n", token.c_str());
      }
      StaticJsonDocument<128> wstat;
      wstat["type"]      = "WIFI_STATUS";
      wstat["connected"] = (WiFi.status() == WL_CONNECTED);
      wstat["ssid"]      = getWifiSsid();
      sendDoc(wstat);

    } else if (strcmp(action, "GET_WIFI_STATUS") == 0) {
      StaticJsonDocument<128> wstat;
      wstat["type"]      = "WIFI_STATUS";
      wstat["connected"] = (WiFi.status() == WL_CONNECTED);
      wstat["ssid"]      = getWifiSsid();
      sendDoc(wstat);

    } else if (strcmp(action, "FACTORY_RESET") == 0) {
      activated = false;
      Serial.println("[SYSTEM] Factory reset received from CrowPanel. Scanner disabled.");
      send("{\"type\":\"FACTORY_RESET_ACK\"}");
      delay(100); // Allow time for ESP-NOW packet to transmit
      WiFi.disconnect(true, true);
      prefs.clear();
      wipeExceptAdmin();
      Serial.println("[SYSTEM] WiFi credentials and fingerprint database wiped. Rebooting...");
      beep(1000); // 1-second long beep to confirm reset
      delay(2000);
      ESP.restart();

    } else if (strcmp(action, "SET_IDLE") == 0) {
      idle_screen_active = jcmd["idle"] | false;

    } else if (strcmp(action, "SYNC_EMP") == 0) {
      Serial.println("[SYNC] Manual employee sync requested by CrowPanel.");
      String token = jcmd["token"] | "";
      if (token.length() > 0) {
        deviceToken = token;
      }
      SyncManager::triggerSync(deviceToken);

    } else if (strcmp(action, "RESET") == 0) {
      Serial.println("[SYSTEM] Remote reboot command from CrowPanel. Restarting...");
      send("{\"type\":\"RESET_ACK\"}");
      delay(200);
      ESP.restart();
    }
  }
}
