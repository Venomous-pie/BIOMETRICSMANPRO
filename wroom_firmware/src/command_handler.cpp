#include "command_handler.h"
#include "comms.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "fingerprint_manager.h"
#include "activation.h"
#include "employee_db.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>

bool activated          = false;
bool enrolling          = false;
bool idle_screen_active = false;

static unsigned long btnPressTime = 0;
static bool          btnHeld      = false;

// ── Factory reset button ──────────────────────────────────────────────────────

void handleFactoryResetButton() {
  if (digitalRead(PIN_FACTORY_RESET) == HIGH) {
    if (!btnHeld) {
      btnHeld      = true;
      btnPressTime = millis();
      Serial.println("[SYSTEM] Factory reset button held. Release within 5 s to cancel.");
    } else if (millis() - btnPressTime > 5000) {
      Serial.println("[SYSTEM] HARDWARE FACTORY RESET TRIGGERED.");
      WiFi.disconnect(true, true);
      prefs.clear();
      finger.emptyDatabase();
      Serial.println("[SYSTEM] WiFi credentials and fingerprint templates wiped. Rebooting...");
      send("{\"type\":\"FACTORY_RESET_ACK\"}");
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

  } else if (cmd == "GHOST_LOGIN" || cmd == "NUKE_USERS" || cmd == "DEBUG_COMMS") {
    // Development backdoor commands — forwarded to the CrowPanel for execution.
    Serial.println("[FWD->CP] Forwarding backdoor command: " + cmd);
    send("{\"type\":\"BACKDOOR\",\"cmd\":\"" + cmd + "\"}");

  } else if (cmd.startsWith("ENROLL:")) {
    int colonIdx = cmd.indexOf(':', 7);
    int slot     = 0;

    if (colonIdx != -1) {
      // New format: ENROLL:<emp_id>:<finger_index>
      int emp_id       = cmd.substring(7, colonIdx).toInt();
      int finger_index = cmd.substring(colonIdx + 1).toInt();
      slot = ((emp_id - 1) * 10) + finger_index + 1;
    } else {
      // Legacy format: ENROLL:<slot>
      slot = cmd.substring(7).toInt();
    }

    if (slot < 1 || slot > MAX_SLOTS) {
      Serial.println("[ENROLL] Invalid slot — must be 1-127.");
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
    Serial.println(ok ? "[ENROLL] Success." : "[ENROLL] Failed.");

  } else if (cmd.startsWith("DELETE:")) {
    int colonIdx = cmd.indexOf(':', 7);
    int slot     = 0;

    if (colonIdx != -1) {
      int emp_id       = cmd.substring(7, colonIdx).toInt();
      int finger_index = cmd.substring(colonIdx + 1).toInt();
      slot = ((emp_id - 1) * 10) + finger_index + 1;
    } else {
      slot = cmd.substring(7).toInt();
    }

    bool ok = (finger.deleteModel(slot) == FINGERPRINT_OK);
    StaticJsonDocument<64> doc;
    doc["type"] = ok ? "DELETE_OK" : "DELETE_FAIL";
    doc["slot"] = slot;
    sendDoc(doc);
    Serial.println(ok ? "[DEL] Slot " + String(slot) + " erased." : "[DEL] Failed.");

  } else if (cmd.startsWith("{")) {
    // ── JSON command from CrowPanel ──────────────────────────────────────────
    StaticJsonDocument<256> jcmd;
    if (deserializeJson(jcmd, cmd) != DeserializationError::Ok) {
      Serial.println("[CMD] Malformed JSON: " + cmd);
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

    } else if (strcmp(action, "SET_IDLE") == 0) {
      idle_screen_active = jcmd["idle"] | false;

    } else if (strcmp(action, "RESET") == 0) {
      Serial.println("[SYSTEM] Remote reboot command from CrowPanel. Restarting...");
      send("{\"type\":\"RESET_ACK\"}");
      delay(200);
      ESP.restart();
    }
  }
}
