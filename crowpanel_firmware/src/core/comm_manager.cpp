#include "comm_manager.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <mbedtls/base64.h>
#include <HTTPClient.h>
#include "data_manager.h"
#include "display_driver.h"
#include "../ui/ui_manager.h"
#include "../splash/manpro_splash.h"
#include "sync_receiver.h"
#include "sync_protocol.h"

// UI forward declarations
extern void uiShowIdle();
extern void uiUpdateClock(const char* ts);
extern void uiShowPlaceFinger();
extern void uiShowMatch(const char* name, const char* dept, const char* action, const char* ts);
extern void uiShowNoMatch();
extern void uiShowActionDenied(const char* name, uint8_t action_type);
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
extern void uiSyncStatusOnSyncResult(bool ok); // Sync Status page feedback

// ESP-NOW message queue (Ring Buffer)
// 16 is enough for bursts. 128 uses too much RAM and caused reboots.
#define ESPNOW_QUEUE_SIZE  16
#define ESPNOW_PAYLOAD_MAX 251

struct EspNowMsg { char data[ESPNOW_PAYLOAD_MAX]; };
static EspNowMsg  s_queue[ESPNOW_QUEUE_SIZE];
static volatile uint8_t s_qHead = 0;
static volatile uint8_t s_qTail = 0;

// Binary sync packet queue
// Binary packets (magic byte 0x5A) must NOT be dispatched directly from the
// ESP-NOW RX callback because SyncReceiver::handleIncomingPacket() calls
// malloc() for SYNC_START, which is unsafe in ISR/callback context.
// They are pushed here and dispatched in process() instead.
#define SYNC_QUEUE_SIZE    8
struct SyncMsg { uint8_t data[ESPNOW_PAYLOAD_MAX]; uint8_t len; };
static SyncMsg          s_syncQueue[SYNC_QUEUE_SIZE];
static volatile uint8_t s_sqHead = 0;
static volatile uint8_t s_sqTail = 0;

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

#ifdef ENABLE_DEV_TOOLS
void executeBackdoor(String cmd);
#endif

// Tracks if we already tried to auto-reconnect to WiFi
static bool s_autoReconnectAttempted = false;

// Static member definitions
String CommManager::serialBuf = "";

// ESP-NOW receive callback. Adds incoming data to the appropriate queue.
void CommManager::onEspNowRecv(const esp_now_recv_info_t* recv_info,
                                const uint8_t* data, int len) {
    if (len <= 0 || len > ESPNOW_PAYLOAD_MAX) return;

    // Binary sync packets go to the sync queue so malloc() is deferred to process().
    if (data[0] == SYNC_MAGIC_BYTE) {
        uint8_t next = (s_sqTail + 1) % SYNC_QUEUE_SIZE;
        if (next != s_sqHead) { // drop silently if full
            memcpy(s_syncQueue[s_sqTail].data, data, len);
            s_syncQueue[s_sqTail].len = (uint8_t)len;
            __atomic_thread_fence(__ATOMIC_RELEASE);
            s_sqTail = next;
        }
        return;
    }

    if (len >= ESPNOW_PAYLOAD_MAX) return; // Prevent buffer overflow for JSON
    
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
    esp_wifi_set_ps(WIFI_PS_NONE); // Disable modem sleep to prevent missing ESP-NOW packets

    // Set the WiFi channel for ESP-NOW
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        if (Serial) Serial.println("[ESP-NOW] Init FAILED!");
        return;
    }

    // Bind the static member as the C callback
    esp_now_register_recv_cb(CommManager::onEspNowRecv);

    // Register WROOM as a unicast peer (done on every boot)
    esp_now_del_peer(WROOM_MAC); // ensure clean state
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, WROOM_MAC, 6);
    peer.channel = 0;      // 0 = use current channel
    peer.encrypt  = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        if (Serial) Serial.println("[ESP-NOW] add_peer FAILED — check WROOM_MAC");
    }

    SyncReceiver::init(); // Initialize the binary sync receiver

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

void CommManager::sendSyncPacket(const uint8_t* payload, size_t len) {
    if (len > 250) return;
    esp_now_send(WROOM_MAC, payload, len);
}

// Main processing loop: handles incoming messages and connection recovery
void CommManager::process() {
    SyncReceiver::loop();

    // Drain binary sync packets (queued from the RX callback — ISR-safe path)
    while (s_sqHead != s_sqTail) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        SyncReceiver::handleIncomingPacket(
            s_syncQueue[s_sqHead].data,
            s_syncQueue[s_sqHead].len);
        s_sqHead = (s_sqHead + 1) % SYNC_QUEUE_SIZE;
        s_lastRecvMs = millis(); // treat any sync packet as a heartbeat
    }

    // If no message is received for 5 seconds, start scanning channels to find WROOM.
    unsigned long silenceRef = (s_lastRecvMs > 0) ? (unsigned long)s_lastRecvMs : s_beginMs;
    if (silenceRef > 0 && millis() - silenceRef > 5000) {
        if (!s_scanningChannels) {
            s_scanningChannels = true;
            if (Serial) Serial.println("[ESP-NOW] Link lost! Entering auto-recovery channel scan...");
            
            // JUMP TO DEFAULT CHANNEL IMMEDIATELY
            // This recovers the link instantly if WROOM dropped back to default after a failed WiFi connection.
            s_currentChannel = ESPNOW_CHANNEL;
            esp_wifi_set_channel(s_currentChannel, WIFI_SECOND_CHAN_NONE);
            s_lastScanMs = millis();
        } else {
            // Scan each channel for 1.5 seconds
            if (millis() - s_lastScanMs > 1500) {
                s_lastScanMs = millis();
                s_currentChannel++;
                if (s_currentChannel > 13) s_currentChannel = 1;
                
                if (Serial) Serial.printf("[ESP-NOW] Scanning channel %d...\n", s_currentChannel);
                esp_wifi_set_channel(s_currentChannel, WIFI_SECOND_CHAN_NONE);
            }
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
            String actCmd = "{\"cmd\":\"DEVICE_ACTIVATED\",\"token\":\"" + DataManager::getActivationCode() + "\"}";
            sendCommand(actCmd);
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
#ifdef ENABLE_DEV_TOOLS
                    if (serialBuf == "GHOST_LOGIN" || serialBuf == "NUKE_USERS") {
                        executeBackdoor(serialBuf);
                    } else {
                        // Forward typed commands to WROOM via ESP-NOW
                        esp_now_send(WROOM_MAC,
                                     (const uint8_t*)serialBuf.c_str(),
                                     serialBuf.length());
                        Serial.println("[FWD->WROOM] " + serialBuf);
                    }
#else
                    // Forward typed commands to WROOM via ESP-NOW
                    esp_now_send(WROOM_MAC,
                                 (const uint8_t*)serialBuf.c_str(),
                                 serialBuf.length());
                    Serial.println("[FWD->WROOM] " + serialBuf);
#endif
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
    if (cmd.length() == 0) return;
    esp_now_send(WROOM_MAC, (const uint8_t*)cmd.c_str(), cmd.length());
}

void CommManager::sendDebug(const String& msg) {
    StaticJsonDocument<256> doc;
    doc["cmd"] = "DEBUG";
    doc["msg"] = msg;
    String out;
    serializeJson(doc, out);
    sendCommand(out);
}

#ifdef ENABLE_DEV_TOOLS
void executeBackdoor(String cmd) {
    if (cmd == "GHOST_LOGIN") {
        Serial.println("[BACKDOOR] GHOST_LOGIN activated. Entering Main Menu.");
        UIManager::showMainMenu();
    } else if (cmd == "NUKE_USERS") {
        Serial.println("[BACKDOOR] NUKE_USERS activated. Deleting enrolled FPs (except Slot 1).");
        for (int i = 0; i < DataManager::getEmployeeCount(); i++) {
            if (DataManager::getEmployees()[i].id != "1") {
                // Pass -1 to clear ALL enrolled fingers for this employee
                DataManager::updateEmployeeFpEnrolled(DataManager::getEmployees()[i].id, false, -1);
                for (int f = 0; f < 10; f++) {
                    String delCmd = "DELETE:" + String(DataManager::getEmployees()[i].id) + ":" + String(f);
                    esp_now_send(WROOM_MAC, (const uint8_t*)delCmd.c_str(), delCmd.length());
                    delay(50);
                }
            }
        }
    } else if (cmd == "NUKE_DB") {
        Serial.println("[BACKDOOR] NUKE_DB activated. Deleting downloaded employees DB.");
        DataManager::nukeDatabase();
        UIManager::showToast("Employee Database Nuked!", true);
    } else if (cmd == "DEBUG_COMMS") {
        s_debugComms = !s_debugComms;
        Serial.print("[BACKDOOR] DEBUG_COMMS ");
        Serial.println(s_debugComms ? "ON" : "OFF");
    }
}
#endif // ENABLE_DEV_TOOLS

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
#ifdef ENABLE_DEV_TOOLS
        const char *b_cmd = doc["cmd"] | "";
        executeBackdoor(String(b_cmd));
#else
        if (Serial) Serial.println("[BACKDOOR] Ignored in production build.");
#endif
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
            String actCmd = "{\"cmd\":\"DEVICE_ACTIVATED\",\"token\":\"" + DataManager::getActivationCode() + "\"}";
            sendCommand(actCmd);  // unlock WROOM scanner
            uiShowIdle();
            if (Serial) Serial.println("[ACTIVATION] Device activated. Scanner unlocked.");
        } else {
            // Server rejected the code — ensure the device stays deactivated (BUG-12 fix)
            DataManager::setActivatedByServer(false);
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
            String actCmd = "{\"cmd\":\"DEVICE_ACTIVATED\",\"token\":\"" + DataManager::getActivationCode() + "\"}";
            sendCommand(actCmd.c_str());
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
            String actCmd = "{\"cmd\":\"DEVICE_ACTIVATED\",\"token\":\"" + DataManager::getActivationCode() + "\"}";
            sendCommand(actCmd);
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
                // Reconnect failed / WROOM is handling backoff retries in the background.
                // We intentionally don't show a toast here to avoid spamming the UI.
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
        if (doc["success"] | false) {
            DataManager::addSyncLog("Time synced via NTP");
        } else {
            DataManager::addSyncLog("NTP time sync failed");
        }
        uiSettingsUpdateNtpStatus(ok, ts, err);
    } else if (strcmp(type, "WIFI_SCAN_RESULT") == 0) {
        const char* ssids = doc["ssids"] | "";
        uiWifiUpdateScanResult(ssids);
        uiSettingsUpdateWifiScan(ssids);
    } else if (strcmp(type, "FACTORY_RESET_ACK") == 0) {
        uiFactoryResetComplete();
    } else if (strcmp(type, "RESET_ACK") == 0) {
        if (Serial) Serial.println("[SYSTEM] Rebooting...");
        extern LGFX lcd;
        lcd.setBrightness(0);
        delay(200);
        ESP.restart();
    } else {
        if (!DataManager::isActivated()) {
            if (Serial) Serial.println("[UART] Ignored (not activated): " + String(type));
            return;
        }

        static String temp_emp_id = "";

        if (strcmp(type, "EMP_SYNC_START") == 0) {
            temp_emp_id = "";  // EDGE-01: reset stale ID from any previous batch
            DataManager::syncStart();
        }
        else if (strcmp(type, "E1") == 0) {
            temp_emp_id = doc["id"] | "";
        }
        else if (strcmp(type, "E2") == 0) {
            DataManager::syncAddEmployee(
                temp_emp_id,
                doc["n"] | "",
                doc["d"] | "",
                doc["j"] | "",
                doc["b"] | ""
            );
        }
        else if (strcmp(type, "EMP_BATCH_DONE") == 0) {
            CommManager::sendCommand("{\"cmd\":\"EMP_BATCH_ACK\"}");
        }
        else if (strcmp(type, "EMP_SYNC_DONE") == 0) {
            DataManager::syncDone();
            DataManager::addSyncLog("Employee sync successful");
            uiSyncStatusOnSyncResult(true);
            UIManager::showToast("Employees synced successfully!", false);
        }
        else if (strcmp(type, "EMP_SYNC_FAIL") == 0) {
            DataManager::syncAbort();
            DataManager::addSyncLog("Employee sync failed");
            uiSyncStatusOnSyncResult(false);
            UIManager::showToast(doc["msg"] | "Failed to sync employees", true);
        }
        if (strcmp(type, "PLACE_FINGER") == 0)        uiShowPlaceFinger();
        else if (strcmp(type, "MATCH") == 0) {
            int empId         = doc["emp_id"] | 0;
            int fIdx          = doc["f_idx"]  | 0;
            String realName   = "";
            String realDept   = "";
            const char* ts    = doc["ts"]     | "";
            int  conf         = doc["conf"]   | 0;

            extern int pending_action;
            uint8_t action_type = 1;
            if (pending_action == 0) {
                const char* act = doc["action"] | "IN";
                if (strcmp(act, "IN") == 0) action_type = 1;
                else if (strcmp(act, "OUT") == 0) action_type = 2;
                else if (strcmp(act, "OVERTIME_IN") == 0) action_type = 3;
                else if (strcmp(act, "OVERTIME_OUT") == 0) action_type = 4;
            } else {
                action_type = pending_action;
            }

            // Lookup the real employee name from the local DB using empId
            const Employee* db = DataManager::getEmployees();
            int count = DataManager::getEmployeeCount();
            for (int i=0; i<count; i++) {
                if (db[i].id.toInt() == empId) {
                    realName = db[i].name;
                    realDept = db[i].dept;
                    break;
                }
            }
            if (realName.length() == 0) realName = String("Emp ") + String(empId);
            if (realDept.length() == 0) realDept = doc["dept"] | "";

            // Wait, for admins, how do we identify them?
            // Usually admins were slot 1-5. Now admin might just be empId 1-5.
            if (empId >= 1 && empId <= 5) {
                // Admin/system slots: jump to main menu instead of logging
                UIManager::showMainMenu();
            } else {
                if (!DataManager::isActionAllowed(empId, action_type)) {
                    uiShowActionDenied(realName.c_str(), action_type);
                    return; // Prevent log creation
                }
                
                // Record the attendance log and attempt to upload
                DataManager::addLog(realName, String(ts), action_type, conf, empId);
                DataManager::uploadPendingLogs();
                
                String action_str = "IN";
                if (action_type == 2) action_str = "OUT";
                else if (action_type == 3) action_str = "OT IN";
                else if (action_type == 4) action_str = "OT OUT";

                uiShowMatch(realName.c_str(), realDept.c_str(), action_str.c_str(), ts);
            }
        }
        else if (strcmp(type, "NOMATCH") == 0)        uiShowNoMatch();
        else if (strcmp(type, "ENROLL_CHUNK") == 0) {
            static String b64Buffer = "";
            int c = doc["c"] | 0;
            int t = doc["t"] | 0;
            if (c == 0) b64Buffer = ""; // reset on first chunk
            b64Buffer += doc["d"].as<String>();
            
            if (c == t - 1) {
                // Last chunk received. Decode and save to SD card.
                size_t outputLen = 0;
                unsigned char decodeBuf[768];
                int ret = mbedtls_base64_decode(decodeBuf, sizeof(decodeBuf), &outputLen, (const unsigned char*)b64Buffer.c_str(), b64Buffer.length());
                
                if (ret == 0 && outputLen > 0) {
                    String empIdStr = doc["emp_id"].as<String>();
                    int idx = doc["idx"] | 0;
                    if (empIdStr.length() > 0 && empIdStr != "0") {
                        DataManager::saveTemplate(empIdStr, idx, decodeBuf, outputLen);
                        DataManager::updateEmployeeFpEnrolled(empIdStr, true, idx);
                        
                        // We also need to upload it to the API here so the backend has the backup
                        // The WROOM used to do this, now we do it.
                        // We can run this in a fire-and-forget task or queue it.
                        // Let's just create a quick xTask to POST it.
                        struct UploadCtx {
                            String empName;
                            int fingerIndex;
                            int slot;
                            String b64Data;
                            size_t tplSize;
                        };
                        UploadCtx* ctx = new UploadCtx;
                        ctx->empName = doc["name"].as<String>();
                        ctx->fingerIndex = idx;
                        ctx->slot = doc["slot"] | 0;
                        ctx->b64Data = b64Buffer;
                        ctx->tplSize = outputLen;
                        
                        TaskFunction_t uploadFn = [](void* arg) {
                            UploadCtx* ctx = (UploadCtx*)arg;
                            if (WiFi.status() == WL_CONNECTED && DataManager::isActivated()) {
                                HTTPClient http;
                                String url = String(API_BASE_URL) + "/api/devices/enrollFingerprint";
                                http.begin(url);
                                http.addHeader("Content-Type", "application/json");
                                http.addHeader("Authorization", "Bearer " + DataManager::getActivationCode());
                                
                                StaticJsonDocument<1024> body;
                                body["employee_name"] = ctx->empName;
                                body["finger_index"] = ctx->fingerIndex;
                                body["slot"] = ctx->slot;
                                body["device_id"] = DataManager::getDeviceId();
                                body["template_data"] = ctx->b64Data;
                                body["template_size"] = ctx->tplSize;
                                
                                http.setTimeout(5000);
                                String bodyStr;
                                serializeJson(body, bodyStr);
                                http.POST(bodyStr);
                                http.end();
                            }
                            delete ctx;
                            vTaskDelete(NULL);
                        };
                        if (xTaskCreate(uploadFn, "UploadTpl", 8192, ctx, 1, NULL) != pdPASS) {
                            delete ctx;
                        }
                    }
                }
            }
        }
        else if (strcmp(type, "ENROLL_START") == 0)   uiShowEnrollStart(doc["name"]);
        else if (strcmp(type, "ENROLL_STEP") == 0)    uiShowEnrollStep(doc["step"] | 0, doc["msg"] | "");
        else if (strcmp(type, "ENROLL_OK") == 0)      uiShowEnrollResult(true, doc["name"]);
        else if (strcmp(type, "ENROLL_FAIL") == 0)    uiShowEnrollResult(false, nullptr);
    }
}
