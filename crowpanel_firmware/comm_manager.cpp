#include "comm_manager.h"
#include <ArduinoJson.h>
#include "data_manager.h"
#include "ui_manager.h"

// UI forward declarations
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
extern void uiSettingsUpdateClock(const char* ts);
extern void uiSettingsUpdateWifiScan(const char* ssids);
extern void uiSettingsUpdateWifiStatus(bool connected);

extern HardwareSerial WroomSerial;

String CommManager::uartBuf  = "";
String CommManager::serialBuf = "";

void CommManager::begin() {
    if (Serial) Serial.println("[UART] CommManager ready");
}

void CommManager::process() {
    // NON-BLOCKING: read WROOM UART char-by-char
    while (WroomSerial.available()) {
        char c = WroomSerial.read();
        if (c == '\n') {
            uartBuf.trim();
            if (uartBuf.length() > 0) {
                if (strcmp(uartBuf.c_str(), "{\"type\":\"TIME\"}") != 0) {
                    if (Serial) Serial.println("[<-WROOM] " + uartBuf);
                }
                dispatchJson(uartBuf);
            }
            uartBuf = "";
        } else if (c != '\r') {
            uartBuf += c;
            if (uartBuf.length() > 1024) {
                if (Serial) Serial.println("[UART] Overflow — dropping.");
                uartBuf = "";
            }
        }
    }

    // NON-BLOCKING: USB Serial forwarder
    if (Serial) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n') {
                serialBuf.trim();
                if (serialBuf.length() > 0) {
                    WroomSerial.println(serialBuf);
                    if (Serial) Serial.println("[FWD->WROOM] " + serialBuf);
                }
                serialBuf = "";
            } else if (c != '\r') {
                serialBuf += c;
            }
        }
    }
}

void CommManager::sendCommand(const String& cmd) {
    WroomSerial.println(cmd);
    if (Serial) Serial.println("[->WROOM] " + cmd);
}

void CommManager::dispatchJson(const String& line) {
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        if (Serial) Serial.println("[UART] JSON parse error: " + line);
        return;
    }

    const char *type = doc["type"];
    if (!type) return;

    if (strcmp(type, "TIME") == 0) {
        uiUpdateClock(doc["ts"] | "");
        uiSettingsUpdateClock(doc["ts"] | "");
    } else if (strcmp(type, "PING") == 0) {
        WroomSerial.println("{\"type\":\"PONG\"}");
        if (Serial) Serial.println("[PING] Got PING -> sent PONG");
    } else if (strcmp(type, "WIFI_STATUS") == 0) {
        bool connected = doc["connected"] | false;
        DataManager::setWifiConnected(connected);
        UIManager::updateHeaderWifi(connected);
        uiWifiUpdateStatus(connected);
        uiIdleUpdateWifi(connected);
        uiSettingsUpdateWifiStatus(connected);

        static bool autoReconnectAttempted = false;
        if (!connected && !autoReconnectAttempted && DataManager::hasSavedWifi()) {
            autoReconnectAttempted = true;
            String savedSsid = DataManager::getWifiSsid();
            String savedPass = DataManager::getWifiPass();
            if (Serial) Serial.println("[WiFi] Auto-reconnecting to: " + savedSsid);
            StaticJsonDocument<256> req;
            req["cmd"]  = "WIFI_CONNECT";
            req["ssid"] = savedSsid;
            req["pass"] = savedPass;
            String out; serializeJson(req, out);
            sendCommand(out);
        }
    } else if (strcmp(type, "WIFI_SCAN_RESULT") == 0) {
        const char* ssids = doc["ssids"] | "";
        uiWifiUpdateScanResult(ssids);
        uiSettingsUpdateWifiScan(ssids);
    } else if (strcmp(type, "FACTORY_RESET_ACK") == 0) {
        uiFactoryResetComplete();
    } else if (strcmp(type, "RESET_ACK") == 0) {
        if (Serial) Serial.println("[SYSTEM] Rebooting...");
        delay(200);
        ESP.restart();
    } else {
        if (!DataManager::isActivated()) {
            if (Serial) Serial.println("[UART] Ignored (not activated): " + String(type));
            return;
        }
        if (strcmp(type, "PLACE_FINGER") == 0)       uiShowPlaceFinger();
        else if (strcmp(type, "MATCH") == 0)          uiShowMatch(doc["name"], doc["dept"], doc["action"], doc["ts"]);
        else if (strcmp(type, "NOMATCH") == 0)        uiShowNoMatch();
        else if (strcmp(type, "ENROLL_START") == 0)   uiShowEnrollStart(doc["name"]);
        else if (strcmp(type, "ENROLL_STEP") == 0)    uiShowEnrollStep(doc["step"] | 0, doc["msg"] | "");
        else if (strcmp(type, "ENROLL_OK") == 0)      uiShowEnrollResult(true, doc["name"]);
        else if (strcmp(type, "ENROLL_FAIL") == 0)    uiShowEnrollResult(false, nullptr);
    }
}
