#include "data_manager.h"
#include "comm_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "sync_protocol.h"
#include "certs.h"
#include <freertos/semphr.h>
#include <SPI.h>
#include <sys/time.h>   // settimeofday()
#include <time.h>       // time(), localtime(), strftime()

Employee DataManager::empDB[150];
int DataManager::empCount = 0;

String DataManager::_adminPin = "0000";
unsigned long DataManager::_lastSyncTimestamp = 0;
DataManager::SyncLogEntry DataManager::_syncLogs[MAX_SYNC_LOGS];
int DataManager::_syncLogCount = 0;
unsigned long DataManager::_wifiDropTime = 0;

// Mutex protecting liveLogs[] and liveLogCount against races between
// the main loop (addLog) and the async upload FreeRTOS task.
static SemaphoreHandle_t s_logMutex = nullptr;

// Interlock: prevents binary sync (applySyncBuffer) and JSON sync (syncStart/syncDone)
// from overwriting empDB simultaneously. Forward-declared here because applySyncBuffer
// appears earlier in the file than syncStart where it would otherwise be defined.
static bool s_empSyncActive = false;

// Protects primitive field modifications (fp_enrolled, enrolled_fingers) in empDB
// across cores.
static portMUX_TYPE s_empMutex = portMUX_INITIALIZER_UNLOCKED;

// ── Live attendance log ───────────────────────────────────────────────────────
// Stored in RAM (up to MAX_LOGS entries) and persisted to LittleFS.
// Oldest entries are overwritten when the buffer is full.

static constexpr int MAX_LOGS = 200;
static AttendanceLog liveLogs[MAX_LOGS];
static int           liveLogCount = 0;
static portMUX_TYPE  logMutex = portMUX_INITIALIZER_UNLOCKED;

void attendanceUploadTask(void *pvParameters) {
    while (true) {
        if (WiFi.status() == WL_CONNECTED && DataManager::isActivated()) {
            AttendanceLog logToSync;
            bool found = false;
            
            portENTER_CRITICAL(&logMutex);
            for (int i = 0; i < liveLogCount; i++) {
                if (!liveLogs[i].synced) {
                    logToSync = liveLogs[i];
                    found = true;
                    break;
                }
            }
            portEXIT_CRITICAL(&logMutex);
            
            if (found) {
                StaticJsonDocument<384> body;
                body["employee_name"] = logToSync.name;
                if (logToSync.action_type == 1) body["action"] = "IN";
                else if (logToSync.action_type == 2) body["action"] = "OUT";
                else if (logToSync.action_type == 3) body["action"] = "OVERTIME_IN";
                else if (logToSync.action_type == 4) body["action"] = "OVERTIME_OUT";
                body["timestamp"]     = logToSync.time_str;
                body["confidence"]    = logToSync.confidence;
                body["slot"]          = logToSync.slot;
                body["device_id"]     = DataManager::getDeviceId();

                String bodyStr;
                serializeJson(body, bodyStr);

                HTTPClient http;
                String url = String(API_BASE_URL) + "/api/attendance/log";
                http.begin(url);
                http.setTimeout(8000);
                http.addHeader("Content-Type", "application/json");
                http.addHeader("Authorization", "Bearer " + DataManager::getActivationCode());
                int code = http.POST(bodyStr);
                http.end();

                if (code >= 200 && code < 300) {
                    portENTER_CRITICAL(&logMutex);
                    for (int i = 0; i < liveLogCount; i++) {
                        if (!liveLogs[i].synced && 
                            liveLogs[i].time_str == logToSync.time_str && 
                            liveLogs[i].name == logToSync.name) {
                            liveLogs[i].synced = true;
                            break;
                        }
                    }
                    portEXIT_CRITICAL(&logMutex);
                    DataManager::saveAttendanceLogs();
                }
            }
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

const AttendanceLog* DataManager::getAttendanceLogs() { return liveLogs; }
int  DataManager::getAttendanceLogCount()             { return liveLogCount; }

int DataManager::getUnsyncedAttendanceCount() {
    int count = 0;
    for (int i = 0; i < liveLogCount; i++) {
        if (!liveLogs[i].synced) count++;
    }
    return count;
}

bool DataManager::isActionAllowed(int slot, uint8_t action_type) {
    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    bool last_was_in = false;
    bool found = false;

    // Search backwards for the last action by this user
    for (int i = liveLogCount - 1; i >= 0; i--) {
        if (liveLogs[i].slot == slot) {
            last_was_in = (liveLogs[i].action_type == 1 || liveLogs[i].action_type == 3);
            found = true;
            break;
        }
    }
    if (s_logMutex) xSemaphoreGive(s_logMutex);

    bool is_time_in = (action_type == 1 || action_type == 3);

    if (!found) {
        // If they have no history, they MUST Time In first.
        return is_time_in;
    }

    // If last action was IN, they must OUT
    // If last action was OUT, they must IN
    return (last_was_in != is_time_in);
}

void DataManager::addLog(const String& name, const String& time_str,
                         uint8_t action_type, int confidence, int slot) {
    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    if (liveLogCount < MAX_LOGS) {
        liveLogs[liveLogCount++] = AttendanceLog{name, time_str, action_type, false, confidence, slot};
    } else {
        // Ring: shift everything left, drop oldest
        memmove(&liveLogs[0], &liveLogs[1], sizeof(AttendanceLog) * (MAX_LOGS - 1));
        liveLogs[MAX_LOGS - 1] = AttendanceLog{name, time_str, action_type, false, confidence, slot};
    }
    if (s_logMutex) xSemaphoreGive(s_logMutex);
    saveAttendanceLogs();
}

void DataManager::loadAttendanceLogs() {
    liveLogCount = 0;
    File f = LittleFS.open("/attendance.jsonl", "r");
    if (!f) return;
    while (f.available() && liveLogCount < MAX_LOGS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        StaticJsonDocument<384> doc;  // 384 bytes: safely handles long names + all fields
        if (deserializeJson(doc, line) != DeserializationError::Ok) continue;
        const char* act = doc["action"] | "IN";
        uint8_t action_type = 1;
        if (strcmp(act, "IN") == 0) action_type = 1;
        else if (strcmp(act, "OUT") == 0) action_type = 2;
        else if (strcmp(act, "OVERTIME_IN") == 0) action_type = 3;
        else if (strcmp(act, "OVERTIME_OUT") == 0) action_type = 4;
        
        liveLogs[liveLogCount++] = AttendanceLog{
            doc["name"]   | "",
            doc["ts"]     | "",
            action_type,
            doc["synced"] | false,
            doc["conf"]   | 0,
            doc["slot"]   | 0
        };
    }
    f.close();
}

void DataManager::saveAttendanceLogs() {
    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    File f = LittleFS.open("/attendance.jsonl", "w");
    if (!f) { if (s_logMutex) xSemaphoreGive(s_logMutex); return; }
    for (int i = 0; i < liveLogCount; i++) {
        StaticJsonDocument<384> doc;
        doc["name"]   = liveLogs[i].name;
        doc["ts"]     = liveLogs[i].time_str;
        if (liveLogs[i].action_type == 1) doc["action"] = "IN";
        else if (liveLogs[i].action_type == 2) doc["action"] = "OUT";
        else if (liveLogs[i].action_type == 3) doc["action"] = "OVERTIME_IN";
        else if (liveLogs[i].action_type == 4) doc["action"] = "OVERTIME_OUT";
        doc["synced"] = liveLogs[i].synced;
        doc["conf"]   = liveLogs[i].confidence;
        doc["slot"]   = liveLogs[i].slot;
        serializeJson(doc, f);
        f.println();
    }
    f.close();
    if (s_logMutex) xSemaphoreGive(s_logMutex);
}

static TaskHandle_t uploadTaskHandle = NULL;

static void asyncUploadTask(void* param) {
    if (WiFi.status() != WL_CONNECTED || DataManager::getActivationCode().length() == 0) {
        uploadTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    String url = String(API_BASE_URL) + "/api/attendance/log";
    bool anyUploaded = false;
    int uploadedCount = 0;

    // Snapshot the count while holding the mutex, then release.
    // We iterate up to this count; new logs added after this point
    // will be picked up on the next uploadPendingLogs() call.
    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    int count = liveLogCount;
    if (s_logMutex) xSemaphoreGive(s_logMutex);

    for (int i = 0; i < count; i++) {
        // Read entry under lock (brief hold: just a struct copy)
        if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
        if (liveLogs[i].synced) { if (s_logMutex) xSemaphoreGive(s_logMutex); continue; }
        String name      = liveLogs[i].name;
        String time_str  = liveLogs[i].time_str;
        uint8_t action_type = liveLogs[i].action_type;
        int  confidence  = liveLogs[i].confidence;
        int  slot        = liveLogs[i].slot;
        if (s_logMutex) xSemaphoreGive(s_logMutex);

        StaticJsonDocument<384> body;
        body["employee_name"] = name;
        
        if (action_type == 1) body["action"] = "IN";
        else if (action_type == 2) body["action"] = "OUT";
        else if (action_type == 3) body["action"] = "OVERTIME_IN";
        else if (action_type == 4) body["action"] = "OVERTIME_OUT";
        body["timestamp"]     = time_str;
        body["confidence"]    = confidence;
        body["slot"]          = slot;
        body["device_id"]     = DataManager::getDeviceId();

        String bodyStr;
        serializeJson(body, bodyStr);

        // HTTP POST without holding the mutex (long-running I/O)
        WiFiClientSecure client;
        client.setCACert(GTS_ROOT_R4);
        HTTPClient http;
        http.begin(client, url);
        http.setTimeout(8000);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + DataManager::getActivationCode());
        int code = http.POST(bodyStr);
        Serial.printf("[ATTENDANCE] POST %s -> HTTP %d\n", url.c_str(), code);
        http.end();

        if (code >= 200 && code < 300) {
            // Re-find the entry by name+time_str (index may have shifted due to memmove)
            if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
            for (int j = 0; j < liveLogCount; j++) {
                if (!liveLogs[j].synced &&
                    liveLogs[j].name == name &&
                    liveLogs[j].time_str == time_str) {
                    liveLogs[j].synced = true;
                    anyUploaded = true;
                    uploadedCount++;
                    break;
                }
            }
            if (s_logMutex) xSemaphoreGive(s_logMutex);
        }
    }

    if (anyUploaded) {
        DataManager::saveAttendanceLogs();
        DataManager::addSyncLog("Uploaded " + String(uploadedCount) + " attendance records");
    }

    uploadTaskHandle = NULL;
    vTaskDelete(NULL);
}

void DataManager::uploadPendingLogs() {
    if (uploadTaskHandle == NULL) {
        xTaskCreate(asyncUploadTask, "UploadLogs", 6144, NULL, 1, &uploadTaskHandle);
    } else {
        Serial.println("[ATTENDANCE] Upload already in progress, skipping.");
    }
}

int DataManager::getEnrolledFingerprintCount() {
    int count = 0;
    for (int i = 0; i < empCount; i++) {
        if (empDB[i].fp_enrolled) count++;
    }
    return count;
}

bool DataManager::_isWifiConfigured = false;
bool DataManager::_isActivated = false;
String DataManager::_hwCode = "";
int DataManager::_failedAttempts = 0;
unsigned long DataManager::_lockoutStartTime = 0;
String DataManager::_wifiSsid[5];
String DataManager::_wifiPass[5];
int DataManager::_wifiCount = 0;
String DataManager::_activationCode = "";
String DataManager::_deviceName = "ManPro Biometric";
int    DataManager::_brightness = 200;
int    DataManager::_screenTimeout = 30;
bool   DataManager::_wifiConnected = false;

void DataManager::begin() {
    if (!LittleFS.begin(true)) {
        return;
    }
    
    if (Serial) Serial.println("[FS] Loaded device configuration.");

    // Ensure templates directory exists on LittleFS
    if (!LittleFS.exists("/templates")) {
        // LittleFS will automatically create directories if the file is written with a path,
        // but we can create a dummy file to ensure it's mapped if needed. Not strictly required.
    }
    
    // Create the attendance mutex before loading logs (task-safe from here on)
    if (!s_logMutex) s_logMutex = xSemaphoreCreateMutex();
    
    uint32_t mac32 = (uint32_t)ESP.getEfuseMac();
    char hw[10];
    snprintf(hw, sizeof(hw), "%04X-%04X", (mac32 >> 16) & 0xFFFF, mac32 & 0xFFFF);
    _hwCode = String(hw);
    
    createInitialFilesIfMissing();
    loadConfig();
    loadEmployees();
    loadWifiCredentials();
    loadAttendanceLogs();
    loadSyncLogs();

    xTaskCreatePinnedToCore(
        attendanceUploadTask,
        "AttendUpload",
        4096,
        NULL,
        1,
        NULL,
        0 // Core 0
    );
}

void DataManager::createInitialFilesIfMissing() {
    if (!LittleFS.exists("/employees.jsonl")) {
        File f = LittleFS.open("/employees.jsonl", "w");
        if (f) {
            f.println("{\"id\":1,\"name\":\"Admin\",\"dept\":\"Admin\",\"job_title\":\"System Admin\",\"branch\":\"Main\",\"fp_enrolled\":false,\"enrolled_finger\":-1}");
            f.close();
        }
    }
    
    if (!LittleFS.exists("/config.json")) {
        File f = LittleFS.open("/config.json", "w");
        if (f) {
            f.print("{\"wifiConfigured\":false,\"activated\":false}");
            f.close();
        }
    }
}

void DataManager::loadConfig() {
    File f = LittleFS.open("/config.json", "r");
    if (!f) return;
    
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        _isWifiConfigured = doc["wifiConfigured"] | false;
        _isActivated = doc["activated"] | false;
        _activationCode = doc["activationCode"] | "";
        _deviceName = doc["deviceName"] | "ManPro Biometric";
        _brightness = doc["brightness"] | 200;
        _screenTimeout = doc["screenTimeout"] | 30;
        _adminPin = doc["admin_pin"] | "0000";
        
        if (_brightness < 50) _brightness = 50;
    }
    f.close();
}

void DataManager::saveConfig() {
    File f = LittleFS.open("/config.json", "w");
    if (!f) return;
    
    StaticJsonDocument<256> doc;
    doc["wifiConfigured"] = _isWifiConfigured;
    doc["activated"] = _isActivated;
    doc["activationCode"] = _activationCode;
    doc["deviceName"] = _deviceName;
    doc["brightness"] = _brightness;
    doc["screenTimeout"] = _screenTimeout;
    doc["admin_pin"] = _adminPin;
    serializeJson(doc, f);
    f.close();
}

void DataManager::loadEmployees() {
    File f = LittleFS.open("/employees.jsonl", "r");
    if (!f) {
        f = LittleFS.open("/employees.json", "r");
        if (!f) return;
        
        StaticJsonDocument<4096> doc;
        if (deserializeJson(doc, f) == DeserializationError::Ok) {
            empCount = 0;
            for (JsonObject e : doc["employees"].as<JsonArray>()) {
                if (empCount >= 150) break;
                empDB[empCount].id = e["id"].as<String>();
                empDB[empCount].name = e.containsKey("name") ? e["name"].as<String>() : "";
                if (empDB[empCount].name.length() == 0) {
                    String first = e.containsKey("first_name") ? e["first_name"].as<String>() : "";
                    String last  = e.containsKey("last_name") ? e["last_name"].as<String>() : "";
                    empDB[empCount].name = first + " " + last;
                }
                empDB[empCount].dept = e.containsKey("department_name") ? e["department_name"].as<String>() : (e.containsKey("dept") ? e["dept"].as<String>() : "");
                empDB[empCount].job_title = e.containsKey("role_name") ? e["role_name"].as<String>() : (e.containsKey("job_title") ? e["job_title"].as<String>() : "");
                empDB[empCount].branch = e.containsKey("branch_name") ? e["branch_name"].as<String>() : (e.containsKey("branch") ? e["branch"].as<String>() : "");
                empDB[empCount].fp_enrolled = e["fp_enrolled"] | false;
                empDB[empCount].enrolled_fingers = (uint16_t)(e["enrolled_fingers"] | 0);
                empCount++;
            }
        }
        f.close();
        saveEmployees();
        return;
    }

    empCount = 0;
    while (f.available() && empCount < 150) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, line) == DeserializationError::Ok) {
            empDB[empCount].id = doc["id"].as<String>();
            empDB[empCount].name = doc["name"] | "";
            empDB[empCount].dept = doc["dept"] | "";
            empDB[empCount].job_title = doc["job_title"] | "";
            empDB[empCount].branch = doc["branch"] | "";
            empDB[empCount].fp_enrolled = doc["fp_enrolled"] | false;
            empDB[empCount].enrolled_fingers = (uint16_t)(doc["enrolled_fingers"] | 0);
            empCount++;
        }
    }
    f.close();

    loadFpState(); // re-apply slot-keyed fp_enrolled after reading employees from disk
}

const Employee* DataManager::getEmployees() { return empDB; }
int DataManager::getEmployeeCount() { return empCount; }

static void writeFpStateEntry(const String& emp_id, int finger_index, bool enrolled);

void DataManager::saveEmployees() {
    File f = LittleFS.open("/employees.jsonl", "w");
    if (!f) return;
    for (int i = 0; i < empCount; i++) {
        StaticJsonDocument<512> doc;
        doc["id"]          = empDB[i].id;
        doc["name"]        = empDB[i].name;
        doc["dept"]        = empDB[i].dept;
        doc["job_title"]   = empDB[i].job_title;
        doc["branch"]      = empDB[i].branch;
        doc["fp_enrolled"] = empDB[i].fp_enrolled;
        doc["enrolled_fingers"] = empDB[i].enrolled_fingers;
        serializeJson(doc, f);
        f.println();
        if (i % 10 == 0) yield(); // Prevent watchdog timeout on large DB saves
    }
    f.close();
}

void DataManager::updateEmployeeFpEnrolled(const String& emp_id, bool enrolled, int finger_index) {
    // "ADMIN" is a synthetic identity — it has no empDB entry.
    // Only write to fp_state.json. Never touch empDB for admin.
    if (emp_id == "ADMIN") {
        writeFpStateEntry(emp_id, finger_index, enrolled);
        return;
    }

    for (int i = 0; i < empCount; i++) {
        if (empDB[i].id == emp_id) {
            portENTER_CRITICAL(&s_empMutex);
            if (enrolled && finger_index >= 0 && finger_index < 10) {
                empDB[i].enrolled_fingers |= (uint16_t)(1 << finger_index);  // set bit
            } else if (!enrolled && finger_index >= 0 && finger_index < 10) {
                empDB[i].enrolled_fingers &= (uint16_t)~(1 << finger_index); // clear bit
            } else if (!enrolled) {
                empDB[i].enrolled_fingers = 0; // finger_index == -1: clear all
            }
            empDB[i].fp_enrolled = (empDB[i].enrolled_fingers != 0);
            portEXIT_CRITICAL(&s_empMutex);
            // Persist ONLY to fp_state.json. It's stable across payroll syncs and
            // loadEmployees() merges it on boot. Skipping saveEmployees() here avoids
            // rewriting the whole JSONL database (100-400ms), which starves the LCD 
            // DMA of PSRAM and causes massive screen tearing.
            writeFpStateEntry(emp_id, finger_index, enrolled);
            return;
        }
    }

    // emp_id not found in empDB — could be a standalone admin slot that was
    // never part of the synced employee list. Still persist to fp_state.json
    // so the enrollment state survives a reboot.
    writeFpStateEntry(emp_id, finger_index, enrolled);
}

bool DataManager::isWifiConfigured() { return _isWifiConfigured; }
void DataManager::setWifiConfigured(bool state) { 
    _isWifiConfigured = state; 
    saveConfig(); 
}

extern void uiSyncStatusOnSyncResult(bool ok);
void uiSyncStatusRefreshLogs();

// ── RTC helpers ──────────────────────────────────────────────────────────────
// Sets the ESP32's system clock from the WROOM NTP timestamp string.
// Format expected: "YYYY-MM-DD HH:MM:SS AM" or "YYYY-MM-DD HH:MM:SS PM" (UTC+8).
void DataManager::setNtpTime(const String& ntpStr) {
    int yr, mo, dy, hr, mn, sc;
    char ampm[3] = "AM";
    // Try 12-hour format first, then fall back to 24-hour
    int matched = sscanf(ntpStr.c_str(), "%d-%d-%d %d:%d:%d %2s",
                         &yr, &mo, &dy, &hr, &mn, &sc, ampm);
    if (matched < 6) return; // parse failed
    if (ampm[0] == 'P' && hr != 12) hr += 12;
    else if (ampm[0] == 'A' && hr == 12) hr = 0;

    struct tm t = {};
    t.tm_year  = yr - 1900;
    t.tm_mon   = mo - 1;
    t.tm_mday  = dy;
    t.tm_hour  = hr;
    t.tm_min   = mn;
    t.tm_sec   = sc;
    t.tm_isdst = -1;
    time_t epoch = mktime(&t); // treat as local (UTC+8) — consistent with WROOM output
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, NULL);
    if (Serial) Serial.printf("[RTC] CrowPanel clock set to %s\n", ntpStr.c_str());
}

// Returns "MM-DD HH:MM" if RTC is set (time > year 2020), else empty string.
String DataManager::getCurrentTimeStr() {
    time_t now = time(NULL);
    if (now < 1577836800UL) return ""; // before 2020-01-01 → NTP not set yet
    struct tm *t = localtime(&now);
    char buf[12];
    strftime(buf, sizeof(buf), "%m-%d %H:%M", t);
    return String(buf);
}

// ── Sync activity log persistence ────────────────────────────────────────────
static const char* SYNC_LOG_FILE = "/sync_log.jsonl";

void DataManager::saveSyncLogs() {
    File f = LittleFS.open(SYNC_LOG_FILE, "w");
    if (!f) return;
    for (int i = 0; i < _syncLogCount; i++) {
        StaticJsonDocument<256> doc;
        doc["msg"] = _syncLogs[i].message;
        doc["ts"]  = _syncLogs[i].timeStr;
        serializeJson(doc, f);
        f.println();
    }
    f.close();
}

void DataManager::loadSyncLogs() {
    File f = LittleFS.open(SYNC_LOG_FILE, "r");
    if (!f) return;
    _syncLogCount = 0;
    while (f.available() && _syncLogCount < MAX_SYNC_LOGS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, line) != DeserializationError::Ok) continue;
        String msg = doc["msg"] | "";
        if (msg.length() == 0) continue;
        SyncLogEntry& e = _syncLogs[_syncLogCount++];
        e.message   = msg;
        e.timeStr   = doc["ts"] | "";
        e.timestamp = 0; // 0 = historical — UI will show timeStr instead of relative time
    }
    f.close();
    if (Serial) Serial.printf("[FS] Loaded %d sync log entries.\n", _syncLogCount);
}

void DataManager::addSyncLog(const String& message) {
    String ts = getCurrentTimeStr(); // "MM-DD HH:MM" or "" if RTC not set

    // If the same message already exists, move it to the end with a refreshed timestamp.
    for (int i = 0; i < _syncLogCount; i++) {
        if (_syncLogs[i].message == message) {
            for (int j = i; j < _syncLogCount - 1; j++) _syncLogs[j] = _syncLogs[j + 1];
            SyncLogEntry& e = _syncLogs[_syncLogCount - 1];
            e.message   = message;
            e.timestamp = millis();
            e.timeStr   = ts;
            saveSyncLogs();
            uiSyncStatusRefreshLogs();
            return;
        }
    }

    SyncLogEntry entry;
    entry.message   = message;
    entry.timestamp = millis();
    entry.timeStr   = ts;

    if (_syncLogCount < MAX_SYNC_LOGS) {
        _syncLogs[_syncLogCount++] = entry;
    } else {
        memmove(&_syncLogs[0], &_syncLogs[1], sizeof(SyncLogEntry) * (MAX_SYNC_LOGS - 1));
        _syncLogs[MAX_SYNC_LOGS - 1] = entry;
    }
    saveSyncLogs();
    uiSyncStatusRefreshLogs();
}

const DataManager::SyncLogEntry* DataManager::getSyncLogs() {
    return _syncLogs;
}

int DataManager::getSyncLogCount() {
    return _syncLogCount;
}

unsigned long DataManager::getWifiDropTime() {
    return _wifiDropTime;
}

unsigned long DataManager::getLastSyncTimestamp() {
    return _lastSyncTimestamp;
}

bool DataManager::isDataStale() {
    if (_lastSyncTimestamp == 0) return true;
    return (millis() - _lastSyncTimestamp > 7200000);
}

void DataManager::applySyncBuffer(const uint8_t* buffer, size_t len) {
    if (len % sizeof(EmployeeSync) != 0) {
        if (Serial) Serial.println("[DATA] applySyncBuffer: buffer length not a multiple of EmployeeSync — likely corruption, aborting.");
        return;
    }

    // If a JSON-based sync is mid-flight, abort it cleanly before we overwrite empDB.
    if (s_empSyncActive) {
        Serial.println("[DATA] applySyncBuffer: aborting in-flight JSON sync to apply binary buffer.");
        syncAbort(); // clears s_empSyncActive and restores oldDB
    }

    int count = len / sizeof(EmployeeSync);
    if (count > MAX_EMP_RECORDS) count = MAX_EMP_RECORDS;

    const EmployeeSync* incoming = (const EmployeeSync*)buffer;

    bool changed = false;
    if (count != empCount) changed = true;
    else {
        for (int i = 0; i < count; i++) {
            if (empDB[i].name != String(incoming[i].name) ||
                empDB[i].dept != String(incoming[i].department) ||
                empDB[i].job_title != String(incoming[i].role) ||
                empDB[i].branch != String(incoming[i].branch)) {
                changed = true;
                break;
            }
        }
    }

    // Full atomic replace: overwrite empDB with the incoming records.
    // fp_enrolled / enrolled_finger are NOT carried from the old DB here —
    // those fields live in fp_state.json (a separate slot-keyed side-table) and
    // are re-applied by loadFpState() after this call.  See updateEmployeeFpEnrolled()
    // and the comment at the top of this file for the rationale.
    empCount = count;
    for (int i = 0; i < count; i++) {
        // IMPORTANT: this id is positional (array index + 1), NOT a stable server ID.
        // It is used only for UI row identification within a single session.
        // It will shift under employees if the API changes employee ordering between
        // syncs — nothing outside this session should treat it as stable.
        // The only stable key CrowPanel owns is the AS608 slot number, stored in
        // enrolled_finger and in fp_state.json.
        empDB[i].id            = String(i + 1);
        empDB[i].name          = String(incoming[i].name);
        empDB[i].dept          = String(incoming[i].department);
        empDB[i].job_title     = String(incoming[i].role);
        empDB[i].branch        = String(incoming[i].branch);
        empDB[i].fp_enrolled   = false;    // cleared; loadFpState() will re-populate
        empDB[i].enrolled_fingers = 0;
    }

    _lastSyncTimestamp = millis();
    saveEmployees();
    loadFpState(); // re-apply fingerprint enrollment status from the stable side-table

    if (!changed) {
        addSyncLog("Employee data is latest");
    } else {
        addSyncLog("Synced " + String(count) + " employees");
    }

    if (Serial) Serial.printf("[DATA] applySyncBuffer: replaced %d records.\n", count);
}

// ── Fingerprint-state side-table ──────────────────────────────────────────────
// fp_state.json stores fingerprint enrollment status keyed by AS608 slot number.
// It is written only by updateEmployeeFpEnrolled() and never touched by payroll
// sync, so it survives full employee-list replacements intact.
//
// Format: { "slots": [ {"emp_id":"1","finger_index":9,"enrolled":true}, ... ] }
//
// At load time loadFpState() scans empDB for an emp_id match and sets fp_enrolled.
// If an emp_id was removed between syncs, the entry is simply ignored.

static const char* FP_STATE_FILE = "/fp_state.json";

// Writes/updates an entry into fp_state.json for a given employee.
// finger_index == -1 with enrolled == false clears all bits (wipe).
static void writeFpStateEntry(const String& emp_id, int finger_index, bool enrolled) {
    DynamicJsonDocument doc(8192); // 150 employees × ~2 fields each needs ~5-6 KB

    if (LittleFS.exists(FP_STATE_FILE)) {
        File f = LittleFS.open(FP_STATE_FILE, "r");
        if (f) { deserializeJson(doc, f); f.close(); }
    }

    if (!doc.containsKey("slots")) doc.createNestedArray("slots");
    JsonArray arr = doc["slots"].as<JsonArray>();

    // Find existing entry and update bitmask in-place
    for (JsonObject entry : arr) {
        if (entry["emp_id"] == emp_id) {
            uint16_t bits = (uint16_t)(entry["enrolled_fingers"] | 0);
            if (enrolled && finger_index >= 0 && finger_index < 10) {
                bits |= (uint16_t)(1 << finger_index);
            } else if (!enrolled && finger_index >= 0 && finger_index < 10) {
                bits &= (uint16_t)~(1 << finger_index);
            } else if (!enrolled) {
                bits = 0; // wipe all
            }
            entry["enrolled_fingers"] = bits;
            File f = LittleFS.open(FP_STATE_FILE, "w");
            if (f) { serializeJson(doc, f); f.close(); }
            return;
        }
    }

    // New entry — build initial bitmask
    uint16_t bits = 0;
    if (enrolled && finger_index >= 0 && finger_index < 10)
        bits = (uint16_t)(1 << finger_index);
    JsonObject entry = arr.createNestedObject();
    entry["emp_id"]           = emp_id;
    entry["enrolled_fingers"] = bits;

    File f = LittleFS.open(FP_STATE_FILE, "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

// Re-applies enrolled_fingers bitmask to empDB from fp_state.json after a sync.
// The match key is emp_id, which is stable across payroll syncs.
void DataManager::loadFpState() {
    if (!LittleFS.exists(FP_STATE_FILE)) return;
    File f = LittleFS.open(FP_STATE_FILE, "r");
    if (!f) return;

    DynamicJsonDocument doc(8192); // 150 employees × ~2 fields each needs ~5-6 KB
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();

    JsonArray arr = doc["slots"].as<JsonArray>();
    for (JsonObject entry : arr) {
        String   emp_id   = entry["emp_id"] | "";
        uint16_t bits     = (uint16_t)(entry["enrolled_fingers"] | 0);
        if (emp_id.length() == 0) continue;

        for (int i = 0; i < empCount; i++) {
            if (empDB[i].id == emp_id) {
                empDB[i].enrolled_fingers = bits;
                empDB[i].fp_enrolled      = (bits != 0);
                break;
            }
        }
    }
}


void DataManager::loadWifiCredentials() {
    _wifiCount = 0;
    if (!LittleFS.exists("/wifi_creds.json")) return;
    File f = LittleFS.open("/wifi_creds.json", "r");
    if (!f) return;
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        if (doc.containsKey("networks")) {
            JsonArray arr = doc["networks"].as<JsonArray>();
            for (JsonObject net : arr) {
                if (_wifiCount >= 5) break;
                _wifiSsid[_wifiCount] = net["ssid"] | "";
                _wifiPass[_wifiCount] = net["pass"] | "";
                _wifiCount++;
            }
        } else {
            _wifiSsid[0] = doc["ssid"] | "";
            _wifiPass[0] = doc["pass"] | "";
            if (_wifiSsid[0].length() > 0) _wifiCount = 1;
        }
    }
    f.close();
}

void DataManager::saveWifiCredentialsToFs() {
    File f = LittleFS.open("/wifi_creds.json", "w");
    if (!f) return;
    StaticJsonDocument<1024> doc;
    JsonArray arr = doc.createNestedArray("networks");
    for (int i = 0; i < _wifiCount; i++) {
        JsonObject net = arr.createNestedObject();
        net["ssid"] = _wifiSsid[i];
        net["pass"] = _wifiPass[i];
    }
    serializeJson(doc, f);
    f.close();
}

void DataManager::saveWifiCredentials(const String& ssid, const String& pass) {
    if (ssid.length() == 0) return;

    // VERY IMPORTANT FIX:
    // If the network is already at the top of the list and the password matches, do nothing.
    // This prevents LittleFS from triggering a Flash Write on every Wi-Fi status update,
    // which disables the CPU cache, blocks the PSRAM bus, and violently starves the RGB DMA!
    if (_wifiCount > 0 && _wifiSsid[0] == ssid && _wifiPass[0] == pass) {
        return; 
    }
    int existing_idx = -1;
    for (int i = 0; i < _wifiCount; i++) {
        if (_wifiSsid[i] == ssid) { existing_idx = i; break; }
    }

    if (existing_idx != -1) {
        // Existing network: rotate it to front, preserving order of others.
        for (int i = existing_idx; i > 0; i--) {
            _wifiSsid[i] = _wifiSsid[i - 1];
            _wifiPass[i] = _wifiPass[i - 1];
        }
    } else {
        // New network: shift everything down, dropping the oldest if full.
        int top = (_wifiCount < 5) ? _wifiCount : 4;
        for (int i = top; i > 0; i--) {
            _wifiSsid[i] = _wifiSsid[i - 1];
            _wifiPass[i] = _wifiPass[i - 1];
        }
        if (_wifiCount < 5) _wifiCount++;
    }

    _wifiSsid[0] = ssid;
    _wifiPass[0] = pass;
    saveWifiCredentialsToFs();
}

void DataManager::clearWifiCredentials() {
    _wifiCount = 0;
    if (LittleFS.exists("/wifi_creds.json")) {
        LittleFS.remove("/wifi_creds.json");
    }
}

String DataManager::getWifiSsid(int index) { return (index >= 0 && index < _wifiCount) ? _wifiSsid[index] : ""; }
String DataManager::getWifiPass(int index) { return (index >= 0 && index < _wifiCount) ? _wifiPass[index] : ""; }
int DataManager::getSavedWifiCount() { return _wifiCount; }
bool   DataManager::hasSavedWifi() { return _wifiCount > 0; }

void DataManager::setWifiConnected(bool connected) { 
    if (_wifiConnected && !connected) {
        _wifiDropTime = millis();
        addSyncLog("Connection lost");
    } else if (!_wifiConnected && connected) {
        _wifiDropTime = 0; // reset
        addSyncLog("Connection restored");
    }
    _wifiConnected = connected; 
}
bool DataManager::isWifiConnected() { return _wifiConnected; }

bool DataManager::isActivated() { return _isActivated; }
String DataManager::getHardwareCode() { return _hwCode; }

String DataManager::getDeviceId() {
    return String(DEVICE_ID_HARDCODED);
}

void DataManager::setActivatedByServer(bool state) {
    _isActivated = state;
    saveConfig();
}

void DataManager::setDeviceToken(const String& token) {
    _activationCode = token;
    saveConfig();
}

bool DataManager::isLockedOut() {
    if (_lockoutStartTime > 0) {
        if (millis() - _lockoutStartTime >= 600000) {
            _lockoutStartTime = 0;
            _failedAttempts = 0;
            return false;
        }
        return true;
    }
    return false;
}

int DataManager::getFailedAttempts() { return _failedAttempts; }
unsigned long DataManager::getLockoutStartTime() { return _lockoutStartTime; }

bool DataManager::activate(const String& code) {
    if (isLockedOut()) return false;

    bool valid = (code.length() == 12);
    for (int i = 0; i < 12 && valid; i++) {
        if (code[i] < 'A' || code[i] > 'Z') valid = false;
    }
    
    if (valid) {
        // BUG-12 fix: do NOT set _isActivated = true here.
        // Only store the code so it can be forwarded to the server.
        // _isActivated is set to true exclusively by setActivatedByServer(true)
        // when the server returns a successful ACTIVATION_RESULT.
        _activationCode = code;
        _failedAttempts = 0;
        return true;
    } else {
        _failedAttempts++;
        if (_failedAttempts >= 5) {
            _lockoutStartTime = millis();
        }
        return false;
    }
}

void DataManager::factoryReset() {
    _isActivated      = false;
    _isWifiConfigured = false;
    _activationCode   = "";
    _adminPin         = "0000"; // Reset admin PIN to default
    saveConfig();
    clearWifiCredentials();

    // Clear employee data
    nukeDatabase();

    // Clear attendance logs locally
    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    liveLogCount = 0;
    LittleFS.remove("/attendance.jsonl");
    if (s_logMutex) xSemaphoreGive(s_logMutex);
}

String DataManager::getActivationCode() {
    return _activationCode;
}

String DataManager::getDeviceName() {
    return _deviceName;
}

void DataManager::setDeviceName(const String& name) {
    _deviceName = name;
    saveConfig();
}

int DataManager::getBrightness() {
    return _brightness;
}

void DataManager::setBrightness(int val) {
    if (val < 50) val = 50;
    if (val > 255) val = 255;
    _brightness = val;
    saveConfig();
}

int DataManager::getScreenTimeout() {
    return _screenTimeout;
}

void DataManager::setScreenTimeout(int val) {
    if (val < 0) val = 0;
    _screenTimeout = val;
    saveConfig();
}

String DataManager::getAdminPin() {
    return _adminPin;
}

void DataManager::setAdminPin(const String& pin) {
    _adminPin = pin;
    saveConfig();
}

static Employee oldDB[150];
static int oldEmpCount = 0;



void DataManager::syncStart() {
    if (s_empSyncActive) {
        Serial.println("[DATA] syncStart: JSON sync already in progress — ignoring duplicate.");
        return;
    }
    s_empSyncActive = true;
    oldEmpCount = empCount;
    for (int i = 0; i < empCount; i++) {
        oldDB[i] = empDB[i];
    }
    empCount = 0;
}

static String sanitizeUTF8(const String& input) {
    String out = "";
    out.reserve(input.length());
    for (int i = 0; i < (int)input.length(); i++) {
        uint8_t c = (uint8_t)input[i];
        if (c < 128) {
            out += (char)c;
        } else if (c == 0xC3) {
            // Guard: only consume the next byte if it exists (EDGE-10 fix)
            if (i + 1 < (int)input.length()) {
                uint8_t next = (uint8_t)input[++i];
                if      (next == 0xB1) out += "n";       // ñ
                else if (next == 0x91) out += "N";       // Ñ
                else if (next == 0xA1) out += "a";       // á
                else if (next == 0x81) out += "A";       // Á
                else if (next == 0xA9) out += "e";       // é
                else if (next == 0x89) out += "E";       // É
                else if (next == 0xAD) out += "i";       // í
                else if (next == 0x8D) out += "I";       // Í
                else if (next == 0xB3) out += "o";       // ó
                else if (next == 0x93) out += "O";       // Ó
                else if (next == 0xBA) out += "u";       // ú
                else if (next == 0x9A) out += "U";       // Ú
                // else: unknown 0xC3+X combo — skip both bytes
            }
            // else: lone 0xC3 at end of string — skip it
        } else if ((c & 0xE0) == 0xC0) {
            i++; // skip unknown 2-byte
        } else if ((c & 0xF0) == 0xE0) {
            i += 2; // skip unknown 3-byte
        } else if ((c & 0xF8) == 0xF0) {
            i += 3; // skip unknown 4-byte
        }
    }
    return out;
}

void DataManager::syncAddEmployee(const String& id, const String& name, const String& dept, const String& job, const String& branch) {
    if (empCount >= 150) return;
    empDB[empCount].id = id;
    empDB[empCount].name = sanitizeUTF8(name);
    empDB[empCount].dept = sanitizeUTF8(dept);
    empDB[empCount].job_title = sanitizeUTF8(job);
    empDB[empCount].branch = sanitizeUTF8(branch);
    
    empDB[empCount].fp_enrolled = false;
    empDB[empCount].enrolled_fingers = 0;
    
    for (int j = 0; j < oldEmpCount; j++) {
        if (oldDB[j].id == id) {
            empDB[empCount].fp_enrolled = oldDB[j].fp_enrolled;
            empDB[empCount].enrolled_fingers = oldDB[j].enrolled_fingers;
            break;
        }
    }
    empCount++;
}

void DataManager::syncDone() {
    s_empSyncActive = false;
    saveEmployees();
}

void DataManager::syncAbort() {
    s_empSyncActive = false;
    empCount = oldEmpCount;
    for (int i = 0; i < empCount; i++) {
        empDB[i] = oldDB[i];
    }
    loadFpState(); // re-apply fp enrollment state that was lost during the aborted sync
}

void DataManager::nukeDatabase() {
    empCount = 0;
    oldEmpCount = 0;
    s_empSyncActive = false;
    for (int i = 0; i < 150; i++) {
        empDB[i].id = "";
        empDB[i].name = "";
        empDB[i].dept = "";
        empDB[i].job_title = "";
        empDB[i].branch = "";
        empDB[i].fp_enrolled = false;
        empDB[i].enrolled_fingers = 0;
    }
    LittleFS.remove("/employees.jsonl");
    
    // Preserve Admin slots (1-5) in fp_state.json, wipe the rest
    if (LittleFS.exists(FP_STATE_FILE)) {
        File f = LittleFS.open(FP_STATE_FILE, "r");
        DynamicJsonDocument doc(4096);
        DynamicJsonDocument newDoc(4096);
        JsonArray newArr = newDoc.createNestedArray("slots");
        
        if (f && deserializeJson(doc, f) == DeserializationError::Ok) {
            JsonArray arr = doc["slots"].as<JsonArray>();
            for (JsonObject entry : arr) {
                int slot = entry["slot"] | -1;
                if (slot >= 1 && slot <= 5) {
                    JsonObject newEntry = newArr.createNestedObject();
                    newEntry["slot"] = entry["slot"];
                    newEntry["name"] = entry["name"];
                    newEntry["enrolled"] = entry["enrolled"];
                }
            }
        }
        if (f) f.close();
        
        File fOut = LittleFS.open(FP_STATE_FILE, "w");
        if (fOut) {
            serializeJson(newDoc, fOut);
            fOut.close();
        }
    }
}

// ── Internal Flash Deep Storage ──────────────────────────────────────────────────────

bool DataManager::saveTemplate(const String& empId, int fingerIndex, const uint8_t* data, size_t len) {
    String path = "/templates/" + empId + "_" + String(fingerIndex) + ".bin";
    File f = LittleFS.open(path, "w");
    if (!f) {
        if (Serial) Serial.println("[FS] Failed to open " + path + " for writing");
        return false;
    }
    size_t written = f.write(data, len);
    f.close();
    if (written == len) {
        if (Serial) Serial.println("[FS] Saved template: " + path);
        return true;
    } else {
        if (Serial) Serial.println("[FS] Write failed for " + path);
        return false;
    }
}

bool DataManager::loadTemplate(const String& empId, int fingerIndex, uint8_t* outData, size_t maxLen, size_t* outLen) {
    String path = "/templates/" + empId + "_" + String(fingerIndex) + ".bin";
    File f = LittleFS.open(path, "r");
    if (!f) {
        return false;
    }
    size_t len = f.size();
    if (len > maxLen) {
        f.close();
        return false;
    }
    size_t bytesRead = f.read(outData, len);
    f.close();
    if (bytesRead == len) {
        if (outLen) *outLen = bytesRead;
        return true;
    }
    return false;
}

bool DataManager::templateExists(const String& empId, int fingerIndex) {
    String path = "/templates/" + empId + "_" + String(fingerIndex) + ".bin";
    return LittleFS.exists(path);
}

bool DataManager::deleteTemplate(const String& empId, int fingerIndex) {
    String path = "/templates/" + empId + "_" + String(fingerIndex) + ".bin";
    if (LittleFS.exists(path)) {
        return LittleFS.remove(path);
    }
    return false;
}

bool DataManager::adminTemplateExists() {
    // Admin can enroll any finger index 0-9. Check all of them.
    for (int f = 0; f < 10; f++) {
        if (templateExists("ADMIN", f)) return true;
    }
    return false;
}

uint16_t DataManager::getEnrolledMask(const String& empId) {
    // Fast path: regular employees are in RAM.
    for (int i = 0; i < empCount; i++) {
        if (empDB[i].id == empId) return empDB[i].enrolled_fingers;
    }
    // Slow path: "ADMIN" (and any future out-of-empDB identity) live in fp_state.json.
    if (!LittleFS.exists(FP_STATE_FILE)) return 0;
    File f = LittleFS.open(FP_STATE_FILE, "r");
    if (!f) return 0;
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return 0; }
    f.close();
    JsonArray arr = doc["slots"].as<JsonArray>();
    for (JsonObject entry : arr) {
        if (String(entry["emp_id"] | "") == empId) {
            return (uint16_t)(entry["enrolled_fingers"] | 0);
        }
    }
    return 0;
}
