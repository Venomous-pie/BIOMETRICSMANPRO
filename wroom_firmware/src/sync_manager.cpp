#include "sync_manager.h"
#include "comms.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <rom/crc.h>
#include <time.h>

#include "certs.h"
#include <WiFiClientSecure.h>

// Use the unified cap from sync_protocol.h — do NOT redefine locally (BUG-05 fix).
static EmployeeSync s_syncBuffer[MAX_SYNC_EMPLOYEES];
static uint16_t s_empCount = 0;

static SyncState s_state = SYNC_STATE_IDLE;
static String s_deviceToken = "";
static uint32_t s_syncId = 0;
static uint16_t s_totalChunks = 0;
static uint32_t s_totalBytes = 0;

static uint16_t s_currentChunk = 0;
static uint8_t  s_retryCount = 0;
static uint32_t s_stateStartTime = 0;
static int      s_lastSyncHour = -1;
static uint32_t s_fastRetryStartTime = 0;

static uint16_t s_missingIndices[64];
static uint8_t  s_missingCount = 0;
static uint8_t  s_missingIndex = 0; // index into s_missingIndices for resend

// Tracks how many times SYNC_END has been resent waiting for SYNC_RESULT.
// Uses a dedicated counter instead of s_retryCount because setState() resets
// s_retryCount to 0, which made the 3-retry limit unreachable (BUG-10 fix).
static uint8_t  s_syncEndResendCount = 0;

static void setState(SyncState newState) {
    s_state = newState;
    s_stateStartTime = millis();
    s_retryCount = 0;
}

void SyncManager::init() {
    s_state = SYNC_STATE_IDLE;
    s_lastSyncHour = -1;
}

void SyncManager::triggerSync(const String& token) {
    if (s_state != SYNC_STATE_IDLE && s_state != SYNC_STATE_FAST_RETRY_MODE) {
        Serial.println("[SYNC] Cannot trigger, sync already in progress.");
        return;
    }
    if (token.length() > 0) {
        s_deviceToken = token;
    }
    
    if (s_deviceToken.length() == 0) {
        Serial.println("[SYNC] Aborting trigger: no device token.");
        return;
    }
    
    // EDGE-07: guard against pre-NTP epoch values (time(NULL) ≈ 0 when NTP not synced).
    // If two syncs get the same sync_id, a stale packet from the previous session
    // could be accepted by the CrowPanel.
    time_t t = time(NULL);
    if (t > 1000000000UL) {
        s_syncId = (uint32_t)t;
    } else {
        static uint32_t s_syncIdCounter = 1;
        s_syncId = s_syncIdCounter++;
        Serial.printf("[SYNC] NTP not ready — using counter-based sync_id: %u\n", s_syncId);
    }

    Serial.println("[SYNC] Starting sync process...");
    setState(SYNC_STATE_FETCH_WIFI);
}

void SyncManager::failToFastRetry(const char* reason) {
    Serial.printf("[SYNC] Failed: %s. Entering FAST_RETRY_MODE.\n", reason);
    
    // Notify CrowPanel immediately so it can stop its UI loading animation
    StaticJsonDocument<128> failDoc;
    failDoc["type"] = "EMP_SYNC_FAIL";
    failDoc["msg"] = reason;
    sendDoc(failDoc);
    
    setState(SYNC_STATE_FAST_RETRY_MODE);
    s_fastRetryStartTime = millis();
}

void SyncManager::loop() {
    uint32_t now = millis();

    // Check hourly trigger
    if (s_state == SYNC_STATE_IDLE) {
        time_t t = time(NULL);
        if (t > 1000000000) { // Valid NTP time
            struct tm *tm_info = localtime(&t);
            if (tm_info->tm_min == 0 && tm_info->tm_hour != s_lastSyncHour) {
                s_lastSyncHour = tm_info->tm_hour;
                triggerSync("");
            }
        }
    }

    switch (s_state) {
        case SYNC_STATE_IDLE:
            break;

        case SYNC_STATE_FETCH_WIFI:
            fetchEmployeesFromApi();
            break;

        case SYNC_STATE_SET_ESPNOW_CHANNEL:
            changeToSyncChannel();
            break;

        case SYNC_STATE_SEND_PING:
            if (s_retryCount == 0 || (now - s_stateStartTime > 300)) {
                if (s_retryCount >= 3) {
                    failToFastRetry("PING timeout");
                } else {
                    sendPing();
                    s_stateStartTime = now;
                    s_retryCount++;
                }
            }
            break;

        case SYNC_STATE_SEND_SYNC_START:
            sendSyncStart();
            setState(SYNC_STATE_SEND_CHUNKS);
            break;

        case SYNC_STATE_SEND_CHUNKS:
            if (s_retryCount == 0 || (now - s_stateStartTime > 200)) {
                if (s_retryCount >= 3) {
                    failToFastRetry("Chunk timeout");
                } else {
                    // Decide if we are sending sequentially or resending missing
                    if (s_missingCount > 0) {
                        if (s_missingIndex < s_missingCount) {
                            sendChunk(s_missingIndices[s_missingIndex]);
                            s_stateStartTime = now;
                            s_retryCount++;
                        } else {
                            // Done resending missing chunks
                            setState(SYNC_STATE_SEND_SYNC_END);
                        }
                    } else {
                        if (s_currentChunk < s_totalChunks) {
                            sendChunk(s_currentChunk);
                            s_stateStartTime = now;
                            s_retryCount++;
                        } else {
                            setState(SYNC_STATE_SEND_SYNC_END);
                        }
                    }
                }
            }
            break;

        case SYNC_STATE_SEND_SYNC_END:
            sendSyncEnd();
            s_syncEndResendCount = 0; // reset before we start waiting for SYNC_RESULT
            setState(SYNC_STATE_AWAIT_SYNC_RESULT);
            break;

        case SYNC_STATE_AWAIT_SYNC_RESULT:
            if (now - s_stateStartTime > 1000) {
                s_syncEndResendCount++;
                if (s_syncEndResendCount >= 3) {
                    s_syncEndResendCount = 0;
                    failToFastRetry("SYNC_RESULT timeout after 3 resends");
                } else {
                    // Resend SYNC_END without calling setState() — that would reset
                    // s_retryCount and make the counter useless (BUG-10 fix).
                    Serial.printf("[SYNC] SYNC_RESULT not received, resending SYNC_END (%d/3).\n",
                                  s_syncEndResendCount);
                    sendSyncEnd();
                    s_stateStartTime = now; // reset the 1s window
                }
            }
            break;

        case SYNC_STATE_FAST_RETRY_MODE:
            // Retry every 5 minutes (300,000 ms)
            if (now - s_fastRetryStartTime > 300000) {
                Serial.println("[SYNC] Fast retry triggering...");
                triggerSync(""); // will reset state
            }
            break;
    }
}

void SyncManager::fetchEmployeesFromApi() {
    if (WiFi.status() != WL_CONNECTED) {
        failToFastRetry("WiFi not connected for fetch");
        return;
    }

    String url = "https://demo.manpromanagement.com/api/devices/employees";
    WiFiClientSecure client;
    client.setCACert(GTS_ROOT_R4); // EDGE-06: verify server TLS certificate
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(15000);
    http.addHeader("Authorization", "Bearer " + s_deviceToken);
    http.addHeader("Accept", "application/json");

    int httpCode = http.GET();
    if (httpCode == 200 || httpCode == 201) {
        WiFiClient* stream = http.getStreamPtr();

        StaticJsonDocument<1024> filter;
        filter["employees"][0]["first_name"] = true;
        filter["employees"][0]["last_name"] = true;
        filter["employees"][0]["name"] = true;
        filter["employees"][0]["role_name"] = true;
        filter["employees"][0]["job_title"] = true;
        filter["employees"][0]["branch_name"] = true;
        filter["employees"][0]["branch"] = true;
        filter["employees"][0]["department_name"] = true;
        filter["employees"][0]["dept"] = true;
        
        filter["data"][0]["first_name"] = true;
        filter["data"][0]["last_name"] = true;
        filter["data"][0]["name"] = true;
        filter["data"][0]["role_name"] = true;
        filter["data"][0]["job_title"] = true;
        filter["data"][0]["branch_name"] = true;
        filter["data"][0]["branch"] = true;
        filter["data"][0]["department_name"] = true;
        filter["data"][0]["dept"] = true;
        
        filter[0]["first_name"] = true;
        filter[0]["last_name"] = true;
        filter[0]["name"] = true;
        filter[0]["role_name"] = true;
        filter[0]["job_title"] = true;
        filter[0]["branch_name"] = true;
        filter[0]["branch"] = true;
        filter[0]["department_name"] = true;
        filter[0]["dept"] = true;
        
        filter["message"] = true;

        DynamicJsonDocument dDoc(16384);
        DeserializationError err = deserializeJson(dDoc, *stream, DeserializationOption::Filter(filter));
        if (err == DeserializationError::Ok) {
            JsonArray arr;
            if (dDoc.is<JsonArray>()) {
                arr = dDoc.as<JsonArray>();
            } else if (dDoc.containsKey("employees")) {
                arr = dDoc["employees"].as<JsonArray>();
            } else if (dDoc.containsKey("data")) {
                arr = dDoc["data"].as<JsonArray>();
            }
            if (arr.isNull()) {
                if (dDoc.containsKey("message")) {
                    String msg = dDoc["message"].as<String>();
                    Serial.printf("[SYNC] API Error Message: %s\n", msg.c_str());
                    failToFastRetry(msg.c_str());
                } else {
                    failToFastRetry("No employees array in JSON");
                }
                return;
            }

            s_empCount = 0;
            for (JsonObject e : arr) {
                if (s_empCount >= MAX_SYNC_EMPLOYEES) break; // BUG-05: use shared cap

                EmployeeSync& emp = s_syncBuffer[s_empCount];
                memset(&emp, 0, sizeof(EmployeeSync));

                String name = e.containsKey("name") ? e["name"].as<String>() : "";
                if (name.length() == 0) {
                    String first = e.containsKey("first_name") ? e["first_name"].as<String>() : "";
                    String last  = e.containsKey("last_name") ? e["last_name"].as<String>() : "";
                    name = first + " " + last;
                }
                strncpy(emp.name, name.c_str(), sizeof(emp.name) - 1);
                
                String role = e.containsKey("role_name") ? e["role_name"].as<String>() : (e.containsKey("job_title") ? e["job_title"].as<String>() : "");
                strncpy(emp.role, role.c_str(), sizeof(emp.role) - 1);
                
                String branch = e.containsKey("branch_name") ? e["branch_name"].as<String>() : (e.containsKey("branch") ? e["branch"].as<String>() : "");
                strncpy(emp.branch, branch.c_str(), sizeof(emp.branch) - 1);
                
                String dept = e.containsKey("department_name") ? e["department_name"].as<String>() : (e.containsKey("dept") ? e["dept"].as<String>() : "");
                strncpy(emp.department, dept.c_str(), sizeof(emp.department) - 1);

                s_empCount++;
            }
            
            s_totalBytes = s_empCount * sizeof(EmployeeSync);
            s_totalChunks = (s_totalBytes + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;
            
            Serial.printf("[SYNC] Fetched %d employees. Total bytes: %u, Chunks: %u\n", s_empCount, s_totalBytes, s_totalChunks);
            
            setState(SYNC_STATE_SET_ESPNOW_CHANNEL);
        } else {
            http.end();
            failToFastRetry("JSON parse failed");
        }
    } else {
        http.end();
        failToFastRetry("HTTP GET failed");
    }
}

void SyncManager::changeToSyncChannel() {
    // resyncEspNow(true) will force set channel to match WiFi channel 
    // and recreate ESP-NOW peer to ensure they match.
    resyncEspNow(true);
    
    // Give it a short time to apply
    delay(50);
    
    setState(SYNC_STATE_SEND_PING);
}

void SyncManager::sendPing() {
    SyncPingPacket pkt;
    pkt.header.magic = SYNC_MAGIC_BYTE;
    pkt.header.type = SYNC_PING;
    sendSyncPacket((const uint8_t*)&pkt, sizeof(pkt));
}

void SyncManager::sendSyncStart() {
    SyncStartPacket pkt;
    pkt.header.magic = SYNC_MAGIC_BYTE;
    pkt.header.type = SYNC_START;
    pkt.sync_id = s_syncId;
    pkt.total_chunks = s_totalChunks;
    pkt.total_bytes = s_totalBytes;
    sendSyncPacket((const uint8_t*)&pkt, sizeof(pkt));
    
    s_currentChunk = 0;
    s_missingCount = 0; // clear any missing logic
}

void SyncManager::sendChunk(uint16_t chunk_index) {
    SyncDataPacket pkt;
    pkt.header.magic = SYNC_MAGIC_BYTE;
    pkt.header.type = SYNC_DATA;
    pkt.sync_id = s_syncId;
    pkt.chunk_index = chunk_index;
    
    uint32_t offset = chunk_index * MAX_CHUNK_SIZE;
    uint32_t remain = s_totalBytes - offset;
    pkt.payload_len = (remain > MAX_CHUNK_SIZE) ? MAX_CHUNK_SIZE : remain;
    
    memset(pkt.payload, 0, MAX_CHUNK_SIZE);
    memcpy(pkt.payload, ((uint8_t*)s_syncBuffer) + offset, pkt.payload_len);
    
    sendSyncPacket((const uint8_t*)&pkt, sizeof(pkt) - MAX_CHUNK_SIZE + pkt.payload_len);
}

void SyncManager::sendSyncEnd() {
    SyncEndPacket pkt;
    pkt.header.magic = SYNC_MAGIC_BYTE;
    pkt.header.type = SYNC_END;
    pkt.sync_id = s_syncId;
    
    // Compute CRC32
    pkt.crc32 = crc32_le(0, (const uint8_t*)s_syncBuffer, s_totalBytes);
    
    sendSyncPacket((const uint8_t*)&pkt, sizeof(pkt));
}

void SyncManager::handleIncomingPacket(const uint8_t* data, size_t len) {
    if (len < sizeof(SyncHeader)) return;
    const SyncHeader* hdr = (const SyncHeader*)data;
    
    if (hdr->type == SYNC_PONG) {
        if (s_state == SYNC_STATE_SEND_PING) {
            Serial.println("[SYNC] PONG received!");
            setState(SYNC_STATE_SEND_SYNC_START);
        }
    } 
    else if (hdr->type == SYNC_CHUNK_ACK) {
        if (s_state == SYNC_STATE_SEND_CHUNKS) {
            if (len >= sizeof(SyncChunkAckPacket)) {
                const SyncChunkAckPacket* pkt = (const SyncChunkAckPacket*)data;
                if (pkt->sync_id == s_syncId) {
                    if (s_missingCount > 0) {
                        if (pkt->chunk_index == s_missingIndices[s_missingIndex]) {
                            s_missingIndex++;
                            s_retryCount = 0;
                        }
                    } else {
                        if (pkt->chunk_index == s_currentChunk) {
                            s_currentChunk++;
                            s_retryCount = 0;
                        }
                    }
                }
            }
        }
    }
    else if (hdr->type == SYNC_RESULT) {
        if (s_state == SYNC_STATE_AWAIT_SYNC_RESULT) {
            if (len >= sizeof(SyncHeader) + 6) { // basic fields
                const SyncResultPacket* pkt = (const SyncResultPacket*)data;
                if (pkt->sync_id == s_syncId) {
                    if (pkt->status == SYNC_STATUS_OK) {
                        Serial.println("[SYNC] Sync SUCCESS!");
                        s_state = SYNC_STATE_IDLE; // Reset to idle
                    } else {
                        Serial.printf("[SYNC] Sync NACK! Missing %d chunks.\n", pkt->missing_count);
                        if (pkt->missing_count > 0 && pkt->missing_count <= 64) {
                            s_missingCount = pkt->missing_count;
                            memcpy(s_missingIndices, pkt->missing_indices, s_missingCount * sizeof(uint16_t));
                            s_missingIndex = 0;
                            // Resend missing chunks (max 3 rounds logic can be added later if needed)
                            setState(SYNC_STATE_SEND_CHUNKS); 
                        } else {
                            failToFastRetry("Too many missing chunks or invalid missing count");
                        }
                    }
                }
            }
        }
    }
}
