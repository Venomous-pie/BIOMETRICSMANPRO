#include "comm_manager.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>
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

// ============================================================
// ESP-NOW ring buffer
// Lockless single-producer (Core 0 WiFi task) / single-consumer (Core 1 loop()).
// Producer only advances s_qTail; consumer only advances s_qHead.
// ============================================================
#define ESPNOW_QUEUE_SIZE  8
#define ESPNOW_PAYLOAD_MAX 251

struct EspNowMsg { char data[ESPNOW_PAYLOAD_MAX]; };
static EspNowMsg  s_queue[ESPNOW_QUEUE_SIZE];
static volatile uint8_t s_qHead = 0;
static volatile uint8_t s_qTail = 0;

// Static member definitions
String CommManager::serialBuf = "";

// ============================================================
// ESP-NOW receive callback  (runs in WiFi task, Core 0)
// Only copies bytes into the ring buffer — zero parsing here.
// ============================================================
// Arduino Core 3.x (IDF 5.x): recv callback takes esp_now_recv_info_t* not uint8_t* mac
void CommManager::onEspNowRecv(const esp_now_recv_info_t* recv_info,
                                const uint8_t* data, int len) {
    if (len <= 0 || len >= ESPNOW_PAYLOAD_MAX) return;
    uint8_t next = (s_qTail + 1) % ESPNOW_QUEUE_SIZE;
    if (next == s_qHead) return; // queue full — drop
    memcpy(s_queue[s_qTail].data, data, len);
    s_queue[s_qTail].data[len] = '\0';
    s_qTail = next;             // publish to consumer
}

// ============================================================
// begin()
// ============================================================
void CommManager::begin() {
    // WiFi STA mode is required for ESP-NOW on ESP32-S3.
    // We don't connect to a router here — just enable the radio.
    WiFi.mode(WIFI_STA);

    // Pin to the fixed channel before esp_now_init() so peer
    // registration uses the correct channel from the start.
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        if (Serial) Serial.println("[ESP-NOW] Init FAILED!");
        return;
    }

    // Bind the static member as the C callback
    esp_now_register_recv_cb(CommManager::onEspNowRecv);

    // Register WROOM as a unicast peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, WROOM_MAC, 6);
    peer.channel = 0;      // 0 = use current channel
    peer.encrypt  = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        if (Serial) Serial.println("[ESP-NOW] add_peer FAILED — check WROOM_MAC");
    }

    if (Serial) {
        Serial.printf("[ESP-NOW] Initialized on channel %d\n", ESPNOW_CHANNEL);
        Serial.printf("[BOOT] CP MAC: %s\n", WiFi.macAddress().c_str());
        Serial.println("[UART] CommManager ready");
    }
}

// ============================================================
// process()  — called from loop() on Core 1
// Drains the ring buffer and dispatches each JSON line.
// All LVGL/UI calls stay on Core 1 — no mutex needed for LVGL.
// ============================================================
void CommManager::process() {
    // Drain all messages deposited by the ESP-NOW callback
    while (s_qHead != s_qTail) {
        String line(s_queue[s_qHead].data);
        s_qHead = (s_qHead + 1) % ESPNOW_QUEUE_SIZE;

        if (line.length() == 0) continue;

        if (strcmp(line.c_str(), "{\"type\":\"TIME\"}") != 0) {
            if (Serial) Serial.println("[<-WROOM] " + line);
        }
        dispatchJson(line);
    }

    // NON-BLOCKING: USB Serial forwarder (char-by-char so we never stall the loop)
    if (Serial) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n') {
                serialBuf.trim();
                if (serialBuf.length() > 0) {
                    // Forward typed commands to WROOM via ESP-NOW
                    esp_now_send(WROOM_MAC,
                                 (const uint8_t*)serialBuf.c_str(),
                                 serialBuf.length());
                    Serial.println("[FWD->WROOM] " + serialBuf);
                }
                serialBuf = "";
            } else if (c != '\r') {
                serialBuf += c;
            }
        }
    }
}

// ============================================================
// sendCommand()
// ============================================================
void CommManager::sendCommand(const String& cmd) {
    if (cmd.length() >= ESPNOW_PAYLOAD_MAX) {
        if (Serial) Serial.printf("[ESP-NOW] TX SKIP: too large (%d bytes)\n", cmd.length());
        return;
    }
    esp_now_send(WROOM_MAC, (const uint8_t*)cmd.c_str(), cmd.length());
    if (Serial) Serial.println("[->WROOM] " + cmd);
}

// ============================================================
// dispatchJson()
// ============================================================
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
        // Reply directly to WROOM MAC
        const char* pong = "{\"type\":\"PONG\"}";
        esp_now_send(WROOM_MAC, (const uint8_t*)pong, strlen(pong));
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
