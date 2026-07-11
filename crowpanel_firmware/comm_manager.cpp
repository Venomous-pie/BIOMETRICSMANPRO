#include "comm_manager.h"
#include <ArduinoJson.h>
#include "data_manager.h"

// UI forward declarations (to avoid circular dependency right now, or include UI headers later)
extern void uiShowIdle();
extern void uiUpdateClock(const char* ts);
extern void uiShowPlaceFinger();
extern void uiShowMatch(const char* name, const char* dept, const char* action, const char* ts);
extern void uiShowNoMatch();
extern void uiShowEnrollStart(const char* name);
extern void uiShowEnrollStep(int step, const char* msg);
extern void uiShowEnrollResult(bool ok, const char* name);
extern void uiWifiUpdateStatus(bool connected);
extern void uiIdleUpdateWifi(bool connected);
extern void uiWifiUpdateScanResult(const char* ssids);
extern void uiFactoryResetComplete();

extern HardwareSerial WroomSerial;

String CommManager::uartBuf = "";

void CommManager::begin() {
    Serial.println("[UART] Initializing WROOM UART...");
    // Configuration happens in setup() via WroomSerial.begin() usually,
    // but we can assume WroomSerial is started before CommManager::begin()
    Serial.println("[UART] CommManager ready");
}

void CommManager::process() {
    while (WroomSerial.available()) {
        char c = WroomSerial.read();
        if (c == '\n') {
            uartBuf.trim();
            if (uartBuf.length() > 0) dispatchJson(uartBuf);
            uartBuf = "";
        } else {
            uartBuf += c;
        }
    }

    if (Serial && Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0) {
            WroomSerial.println(cmd);
            if (Serial) Serial.println("[FWD->WROOM] " + cmd);
        }
    }
}

void CommManager::sendCommand(const String& cmd) {
    WroomSerial.println(cmd);
    Serial.println("[->WROOM] " + cmd);
}

void CommManager::dispatchJson(const String& line) {
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        return;  // Silently ignore malformed packets
    }

    const char *type = doc["type"];
    if (!type) return;

    if (Serial && strcmp(type, "TIME") != 0) {
        Serial.println("[<-WROOM] " + line);
    }

    if (strcmp(type, "TIME") == 0) {
        uiUpdateClock(doc["ts"] | "");
    } else if (strcmp(type, "PING") == 0) {
        WroomSerial.println("PONG");
        if (Serial) Serial.println("[PING] Got PING from WROOM -> sent PONG");
    } else if (strcmp(type, "WIFI_STATUS") == 0) {
        bool connected = doc["connected"] | false;
        uiWifiUpdateStatus(connected);
        uiIdleUpdateWifi(connected);

        // ── Auto-reconnect on boot ──
        // If the WROOM reports it is disconnected and we have saved credentials,
        // immediately attempt to reconnect. We use a one-shot flag so we don't
        // spam reconnect attempts; the user can always reconnect manually from
        // the WiFi setup screen, which will reset the flag for the next boot.
        static bool autoReconnectAttempted = false;
        if (!connected && !autoReconnectAttempted && DataManager::hasSavedWifi()) {
            autoReconnectAttempted = true;
            String savedSsid = DataManager::getWifiSsid();
            String savedPass = DataManager::getWifiPass();
            Serial.printf("[WiFi] Auto-reconnecting to saved SSID: %s\n", savedSsid.c_str());
            StaticJsonDocument<256> req;
            req["cmd"]  = "WIFI_CONNECT";
            req["ssid"] = savedSsid;
            req["pass"] = savedPass;
            String out;
            serializeJson(req, out);
            sendCommand(out);
        }

    } else if (strcmp(type, "WIFI_SCAN_RESULT") == 0) {
        const char* ssids = doc["ssids"] | "";
        uiWifiUpdateScanResult(ssids);
    } else if (strcmp(type, "FACTORY_RESET_ACK") == 0) {
        uiFactoryResetComplete();
    } else {
        if (!DataManager::isActivated()) {
            if (Serial) Serial.println("[UART] Ignored event (device not activated): " + String(type));
            return;
        }

        if (strcmp(type, "PLACE_FINGER") == 0) {
            uiShowPlaceFinger();
        } else if (strcmp(type, "MATCH") == 0) {
            uiShowMatch(doc["name"], doc["dept"], doc["action"], doc["ts"]);
        } else if (strcmp(type, "NOMATCH") == 0) {
            uiShowNoMatch();
        } else if (strcmp(type, "ENROLL_START") == 0) {
            uiShowEnrollStart(doc["name"]);
        } else if (strcmp(type, "ENROLL_STEP") == 0) {
            uiShowEnrollStep(doc["step"] | 0, doc["msg"] | "");
        } else if (strcmp(type, "ENROLL_OK") == 0) {
            uiShowEnrollResult(true, doc["name"]);
        } else if (strcmp(type, "ENROLL_FAIL") == 0) {
            uiShowEnrollResult(false, nullptr);
        }
    }
}
