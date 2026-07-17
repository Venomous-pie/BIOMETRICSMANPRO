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
extern void uiSettingsUpdateNtpStatus(bool ok, const char* ts, const char* err);
extern void uiActivationResult(bool success, const char* err); // Activation screen result

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

// Auto-recovery state
static volatile unsigned long s_lastRecvMs = 0;
static bool s_scanningChannels = false;
static unsigned long s_lastScanMs = 0;
static unsigned long s_lastPingMs = 0;
static uint32_t s_pingCount = 0;
static uint32_t s_pongCount = 0;
static uint8_t s_currentChannel = ESPNOW_CHANNEL;

// WiFi auto-reconnect state — file-scope so WROOM_BOOT can reset it
// when the WROOM reboots independently of the CrowPanel.
static bool s_autoReconnectAttempted = false;

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
    s_lastRecvMs = millis();    // heartbeat for auto-recovery
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

    // Request real WiFi status from WROOM immediately on boot.
    // Without this, the CrowPanel UI always starts as "Offline" after a
    // one-sided reboot because the WROOM has no unprompted trigger to push
    // its current connected state to a freshly booted CrowPanel.
    // The WROOM handles GET_WIFI_STATUS and replies with a WIFI_STATUS packet.
    // We delay slightly so ESP-NOW has time to stabilise before the first TX.
    delay(500);
    const char* req = "{\"cmd\":\"GET_WIFI_STATUS\"}";
    esp_now_send(WROOM_MAC, (const uint8_t*)req, strlen(req));
    if (Serial) Serial.println("[BOOT] Sent GET_WIFI_STATUS to WROOM");
}

// ============================================================
// process() — called continuously from loop() (Core 1)
// ============================================================
void CommManager::process() {
    // ── Auto-recovery / Channel Scan Mode ──────────────────────────────────
    // WROOM broadcasts TIME every 1s. If we hear nothing for 10s, it either crashed
    // or changed channels without telling us. If so, start hunting for it.
    if (s_lastRecvMs > 0 && millis() - s_lastRecvMs > 10000) {
        if (!s_scanningChannels) {
            s_scanningChannels = true;
            if (Serial) Serial.println("[ESP-NOW] Link lost! Entering auto-recovery channel scan...");
        }
        
        // Spend 1.5 seconds on each channel (enough to catch a 1Hz TIME broadcast)
        if (millis() - s_lastScanMs > 1500) {
            s_lastScanMs = millis();
            s_currentChannel++;
            if (s_currentChannel > 13) s_currentChannel = 1;
            
            if (Serial) Serial.printf("[ESP-NOW] Scanning channel %d...\n", s_currentChannel);
            esp_wifi_set_channel(s_currentChannel, WIFI_SECOND_CHAN_NONE);
        }
    } else if (s_scanningChannels && s_lastRecvMs > 0 && millis() - s_lastRecvMs <= 1000) {
        // We received a message! Lock recovered.
        s_scanningChannels = false;
        if (Serial) Serial.printf("[ESP-NOW] Link recovered on channel %d!\n", s_currentChannel);
        
        // Update the peer entry so we can TX back to WROOM on this new channel
        esp_now_del_peer(WROOM_MAC);
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, WROOM_MAC, 6);
        peer.channel = 0; // Use current
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    // Drain all messages deposited by the ESP-NOW callback
    while (s_qHead != s_qTail) {
        String line(s_queue[s_qHead].data);
        s_qHead = (s_qHead + 1) % ESPNOW_QUEUE_SIZE;

        if (line.length() == 0) continue;

        if (strcmp(line.c_str(), "{\"type\":\"TIME\"}") != 0 && strcmp(line.c_str(), "{\"type\":\"PONG\"}") != 0) {
            if (Serial && Serial.availableForWrite() > 32) Serial.println("[<-WROOM] " + line);
        }
        dispatchJson(line);
    }

    // PING WROOM every 3 seconds to verify bidirectional comms
    if (millis() - s_lastPingMs >= 3000) {
        s_lastPingMs = millis();
        s_pingCount++;
        sendCommand("{\"type\":\"PING\"}");
        if (Serial && Serial.availableForWrite() > 32) Serial.printf("[PING] Sent PING #%u to WROOM (awaiting PONG)\n", s_pingCount);
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
        if (Serial && Serial.availableForWrite() > 32) Serial.printf("[ESP-NOW] TX SKIP: too large (%d bytes)\n", cmd.length());
        return;
    }
    esp_now_send(WROOM_MAC, (const uint8_t*)cmd.c_str(), cmd.length());
    if (Serial && Serial.availableForWrite() > 32) Serial.println("[->WROOM] " + cmd);
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
    } else if (strcmp(type, "PONG") == 0) {
        s_pongCount++;
        if (Serial) Serial.printf("[PING] PONG received from WROOM! (ping=%u pong=%u)\n", s_pingCount, s_pongCount);
    } else if (strcmp(type, "ACTIVATION_RESULT") == 0) {
        // WROOM has finished the server round-trip for the registration code.
        bool success        = doc["success"] | false;
        const char* err     = doc["err"] | "";
        const char* token   = doc["device_token"] | "";
        if (Serial) Serial.printf("[ACTIVATION] Result from server: success=%d err=%s token=%s\n", success, err, token);

        if (success) {
            DataManager::setDeviceToken(String(token));
            DataManager::setActivatedByServer(true);
            sendCommand("{\"cmd\":\"DEVICE_ACTIVATED\"}");  // unlock WROOM scanner
            uiShowIdle();
            if (Serial) Serial.println("[ACTIVATION] Device activated. Scanner unlocked.");
        }
        // Notify activation screen (clears spinner, re-enables inputs on failure)
        uiActivationResult(success, err);

    } else if (strcmp(type, "WROOM_BOOT") == 0) {
        if (Serial) Serial.println("[UART] WROOM booted. Checking activation state.");
        if (DataManager::isActivated()) {
            sendCommand("{\"cmd\":\"DEVICE_ACTIVATED\"}");
        }
        // WROOM just rebooted — its WiFi is disconnected.
        // Reset the reconnect flag so the attempt fires fresh, then immediately
        // send WIFI_CONNECT if we have saved credentials (don't wait for a
        // WIFI_STATUS round-trip, which adds several seconds of delay).
        s_autoReconnectAttempted = false;
        if (DataManager::hasSavedWifi()) {
            String savedSsid = DataManager::getWifiSsid();
            String savedPass = DataManager::getWifiPass();
            if (savedPass.length() > 0) {
                s_autoReconnectAttempted = true;  // mark done; WIFI_STATUS path won't double-send
                if (Serial) Serial.println("[WiFi] WROOM rebooted — auto-reconnecting to: " + savedSsid);
                StaticJsonDocument<256> req;
                req["cmd"]  = "WIFI_CONNECT";
                req["ssid"] = savedSsid;
                req["pass"] = savedPass;
                String out; serializeJson(req, out);
                sendCommand(out);
            }
        }
    } else if (strcmp(type, "ACTIVATION_STATUS") == 0) {
        // WROOM performed an HTTP check against the server and reports back here.
        bool activated = doc["activated"] | false;
        const char* devId = doc["device_id"] | "";
        if (Serial) Serial.printf("[ACTIVATION] Server says activated=%d for device_id=%s\n", activated, devId);

        if (activated) {
            // Persist activated state on CrowPanel so it survives reboot
            DataManager::setActivatedByServer(true);
            // Tell WROOM to unlock the fingerprint scanner
            sendCommand("{\"cmd\":\"DEVICE_ACTIVATED\"}");
            // Switch to idle screen (device is now live)
            uiShowIdle();
            if (Serial) Serial.println("[ACTIVATION] Device activated by server. Fingerprint scanner unlocked.");
        } else {
            // Not activated — log it; UI stays on activation screen
            if (Serial) Serial.println("[ACTIVATION] Server says device is NOT activated.");
        }
    } else if (strcmp(type, "WIFI_STATUS") == 0) {
        bool connected = doc["connected"] | false;
        DataManager::setWifiConnected(connected);
        UIManager::updateHeaderWifi(connected);
        uiWifiUpdateStatus(connected);
        uiIdleUpdateWifi(connected);
        uiSettingsUpdateWifiStatus(connected);

        if (!connected && !s_autoReconnectAttempted && DataManager::hasSavedWifi()) {
            String savedSsid = DataManager::getWifiSsid();
            String savedPass = DataManager::getWifiPass();
            // Only auto-reconnect if we have both SSID and a non-empty password
            if (savedPass.length() > 0) {
                s_autoReconnectAttempted = true;
                if (Serial) Serial.println("[WiFi] Auto-reconnecting to: " + savedSsid);
                StaticJsonDocument<256> req;
                req["cmd"]  = "WIFI_CONNECT";
                req["ssid"] = savedSsid;
                req["pass"] = savedPass;
                String out; serializeJson(req, out);
                sendCommand(out);
            } else {
                if (Serial) Serial.println("[WiFi] Saved password is empty — skipping auto-reconnect.");
                // Clear the bad credential so we don't retry every boot
                DataManager::clearWifiCredentials();
            }
        }
    } else if (strcmp(type, "CHANNEL_HOP") == 0) {
        int targetChannel = doc["ch"] | 1;
        if (Serial) Serial.printf("[ESP-NOW] Hopping to channel %d to follow WROOM\n", targetChannel);
        esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
        // Re-register WROOM peer so the peer entry reflects the new channel.
        // (peer.channel=0 means "use current" — must re-add after the channel changes)
        esp_now_del_peer(WROOM_MAC);
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, WROOM_MAC, 6);
        peer.channel = 0;
        peer.encrypt  = false;
        esp_now_add_peer(&peer);
        if (Serial) Serial.printf("[ESP-NOW] Peer re-registered on channel %d\n", targetChannel);
    } else if (strcmp(type, "NTP_STATUS") == 0) {
        bool ok         = doc["ok"] | false;
        const char* ts  = doc["ts"]  | "";
        const char* err = doc["err"] | "";
        if (Serial) Serial.printf("[NTP] Status from WROOM: ok=%d ts=%s err=%s\n", ok, ts, err);
        uiSettingsUpdateNtpStatus(ok, ts, err);
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
