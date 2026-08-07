#include "data_manager.h"
#include "comm_manager.h"
#include "display_driver.h"
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
#include <atomic>

extern SemaphoreHandle_t g_lvglMutex;

Employee DataManager::empDB[150];
int DataManager::empCount = 0;

String DataManager::_adminPin = "0000";
unsigned long DataManager::_lastSyncTimestamp = 0;
DataManager::SyncLogEntry DataManager::_syncLogs[MAX_SYNC_LOGS];
int DataManager::_syncLogCount = 0;
unsigned long DataManager::_wifiDropTime = 0;

static SemaphoreHandle_t s_logMutex = nullptr;

static bool s_empSyncActive = false;

static SemaphoreHandle_t s_empDataMutex = nullptr;
static SemaphoreHandle_t s_syncLogMutex = nullptr;

static constexpr int MAX_LOGS = 200;
static const char* SYNC_LOG_FILE = "/sync_log.jsonl";
static AttendanceLog liveLogs[MAX_LOGS];
static int           liveLogCount = 0;

static std::atomic<bool> s_uploadInProgress(false);

static bool s_syncLogsDirty = false;
static unsigned long s_syncLogsDirtyTime = 0;

static bool s_employeesDirty = false;
static unsigned long s_employeesDirtyTime = 0;

void attendanceUploadTask(void *pvParameters) {
    while (true) {
        if (!s_empSyncActive) {
            if (s_syncLogsDirty && (millis() - s_syncLogsDirtyTime > 5000)) {
                s_syncLogsDirty = false;
                
                extern LGFX lcd;
                int currentBright = DataManager::getBrightness();
                lcd.setBrightness(0);
                delay(20);
                
                File f = LittleFS.open(SYNC_LOG_FILE, "w");
                if (f) {
                    int count = DataManager::getSyncLogCount();
                    const DataManager::SyncLogEntry* logs = DataManager::getSyncLogs();
                    for (int i = 0; i < count; i++) {
                        StaticJsonDocument<256> doc;
                        doc["msg"] = logs[i].message;
                        doc["ts"]  = logs[i].timeStr;
                        serializeJson(doc, f);
                        f.println();
                    }
                    f.close();
                }
                lcd.setBrightness(currentBright);
            }

            if (s_employeesDirty && (millis() - s_employeesDirtyTime > 5000)) {
                s_employeesDirty = false;
                
                if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
                if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
                
                extern LGFX lcd;
                int currentBright = DataManager::getBrightness();
                lcd.setBrightness(0);
                delay(20);
                
                File f = LittleFS.open("/employees.jsonl", "w");
                if (f) {
                    const Employee* emps = DataManager::getEmployees();
                    int count = DataManager::getEmployeeCount();
                    for (int i = 0; i < count; i++) {
                        StaticJsonDocument<512> doc;
                        doc["id"]          = emps[i].id;
                        doc["name"]        = emps[i].name;
                        doc["dept"]        = emps[i].dept;
                        doc["job_title"]   = emps[i].job_title;
                        doc["branch"]      = emps[i].branch;
                        doc["fp_enrolled"] = emps[i].fp_enrolled;
                        doc["enrolled_fingers"] = emps[i].enrolled_fingers;
                        doc["last_action_type"] = emps[i].last_action_type;
                        serializeJson(doc, f);
                        f.println();
                    }
                    f.close();
                }
                
                lcd.setBrightness(currentBright);
                
                if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);
                if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
            }
        } else {
            // Push back the dirty times so they wait until sync is fully done
            if (s_syncLogsDirty) s_syncLogsDirtyTime = millis();
            if (s_employeesDirty) s_employeesDirtyTime = millis();
        }

        if (WiFi.status() == WL_CONNECTED && DataManager::isActivated()) {
            AttendanceLog logToSync;
            bool found = false;
            
            if (s_uploadInProgress) {
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                continue;
            }

            if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
            for (int i = 0; i < liveLogCount; i++) {
                if (!liveLogs[i].synced) {
                    logToSync = liveLogs[i];
                    found = true;
                    break;
                }
            }
            if (s_logMutex) xSemaphoreGive(s_logMutex);
            
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

                WiFiClientSecure client;
                client.setCACert(GTS_ROOT_R4);
                HTTPClient http;
                String url = String(API_BASE_URL) + "/api/attendance/log";
                http.begin(client, url);
                http.setTimeout(8000);
                http.addHeader("Content-Type", "application/json");
                http.addHeader("Authorization", "Bearer " + DataManager::getActivationCode());
                int code = http.POST(bodyStr);
                http.end();

                if (code >= 200 && code < 300) {
                    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
                    for (int i = 0; i < liveLogCount; i++) {
                        if (!liveLogs[i].synced && 
                            liveLogs[i].time_str == logToSync.time_str && 
                            liveLogs[i].name == logToSync.name) {
                            liveLogs[i].synced = true;
                            break;
                        }
                    }
                    if (s_logMutex) xSemaphoreGive(s_logMutex);
                    DataManager::saveAttendanceLogs();
                    DataManager::addSyncLog("Uploaded 1 attendance record");
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

bool DataManager::isActionAllowed(int empId_int, uint8_t action_type) {
    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
    uint8_t last_action = 0;
    bool found = false;
    for (int i = 0; i < empCount; i++) {
        if (empDB[i].id.toInt() == empId_int) {
            last_action = empDB[i].last_action_type;
            found = true;
            break;
        }
    }
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);

    bool is_time_in = (action_type == 1 || action_type == 3);

    if (!found || last_action == 0) {
        return is_time_in;
    }

    bool last_was_in = (last_action == 1 || last_action == 3);
    return (last_was_in != is_time_in);
}

static void appendAttendanceLog(const AttendanceLog& log) {
    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    File f = LittleFS.open("/attendance.jsonl", "a");
    if (f) {
        StaticJsonDocument<384> doc;
        doc["name"]   = log.name;
        doc["ts"]     = log.time_str;
        if (log.action_type == 1) doc["action"] = "IN";
        else if (log.action_type == 2) doc["action"] = "OUT";
        else if (log.action_type == 3) doc["action"] = "OVERTIME_IN";
        else if (log.action_type == 4) doc["action"] = "OVERTIME_OUT";
        doc["synced"] = log.synced;
        doc["conf"]   = log.confidence;
        doc["slot"]   = log.slot;
        serializeJson(doc, f);
        f.println();
        f.close();
    }
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
}

void DataManager::addLog(const String& name, const String& time_str,
                         uint8_t action_type, int confidence, int slot) {
    bool needsFullRewrite = false;
    AttendanceLog newLog = {name, time_str, action_type, false, confidence, slot};
    
    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    if (liveLogCount < MAX_LOGS) {
        liveLogs[liveLogCount++] = newLog;
    } else {
        memmove(&liveLogs[0], &liveLogs[1], sizeof(AttendanceLog) * (MAX_LOGS - 1));
        liveLogs[MAX_LOGS - 1] = newLog;
        needsFullRewrite = true;
    }
    if (s_logMutex) xSemaphoreGive(s_logMutex);
    
    // Append instead of full rewrite if possible
    if (needsFullRewrite) {
        saveAttendanceLogs();
    } else {
        if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
        appendAttendanceLog(newLog);
        if (s_logMutex) xSemaphoreGive(s_logMutex);
    }
    
    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
    for (int i = 0; i < empCount; i++) {
        if (empDB[i].id.toInt() == slot) {
            empDB[i].last_action_type = action_type;
            break;
        }
    }
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);
}

void DataManager::loadAttendanceLogs() {
    liveLogCount = 0;
    File f = LittleFS.open("/attendance.jsonl", "r");
    if (!f) return;
    while (f.available() && liveLogCount < MAX_LOGS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        StaticJsonDocument<384> doc;
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
    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    File f = LittleFS.open("/attendance.jsonl", "w");
    if (!f) { 
        if (s_logMutex) xSemaphoreGive(s_logMutex);
        if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
        return; 
    }
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
        
        // Yield every 5 lines so the GDMA has time to fetch the next display frame
        // This prevents the screen from tearing during a full file rewrite.
        if (i > 0 && (i % 5 == 0)) {
            if (s_logMutex) xSemaphoreGive(s_logMutex);
            if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
            vTaskDelay(pdMS_TO_TICKS(10));
            if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
            if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
        }
    }
    f.close();
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
    if (s_logMutex) xSemaphoreGive(s_logMutex);
}

static TaskHandle_t uploadTaskHandle = NULL;

static void asyncUploadTask(void* param) {
    // Delay upload by 6 seconds to let the LVGL UI animation finish entirely.
    // The success screen stays up for 4 seconds. Deferring to 6s ensures the screen
    // is completely static, so TLS handshakes won't cause visible display tearing.
    vTaskDelay(pdMS_TO_TICKS(6000));

    if (WiFi.status() != WL_CONNECTED || DataManager::getActivationCode().length() == 0) {
        uploadTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    String url = String(API_BASE_URL) + "/api/attendance/log";
    bool anyUploaded = false;
    int uploadedCount = 0;

    s_uploadInProgress = true;

    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    int count = liveLogCount;
    if (s_logMutex) xSemaphoreGive(s_logMutex);

    for (int i = 0; i < count; i++) {
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

    s_uploadInProgress = false;
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
int    DataManager::_volume = 20;
bool   DataManager::_wifiConnected = false;

void DataManager::begin() {
    if (!LittleFS.begin(true)) {
        return;
    }
    
    if (!s_logMutex) s_logMutex = xSemaphoreCreateMutex();
    if (!s_empDataMutex) s_empDataMutex = xSemaphoreCreateMutex();
    if (!s_syncLogMutex) s_syncLogMutex = xSemaphoreCreateMutex();
    if (Serial) Serial.println("[FS] Loaded device configuration.");

    if (!LittleFS.exists("/templates")) {
    }
    
    
    _hwCode = String(DEVICE_ID_HARDCODED);
    
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
        _volume = doc["volume"] | 20;
        _adminPin = doc["admin_pin"] | "0000";
        
        if (_brightness < 50) _brightness = 50;
    }
    f.close();
}

void DataManager::saveConfig() {
    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    File f = LittleFS.open("/config.json", "w");
    if (!f) {
        if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
        return;
    }
    
    StaticJsonDocument<256> doc;
    doc["wifiConfigured"] = _isWifiConfigured;
    doc["activated"] = _isActivated;
    doc["activationCode"] = _activationCode;
    doc["deviceName"] = _deviceName;
    doc["brightness"] = _brightness;
    doc["screenTimeout"] = _screenTimeout;
    doc["volume"] = _volume;
    doc["admin_pin"] = _adminPin;
    serializeJson(doc, f);
    f.close();
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
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
                empDB[empCount].last_action_type = (uint8_t)(e["last_action_type"] | 0);
                empCount++;
            }
        }
        f.close();
        saveEmployees();
        loadFpState();
        LittleFS.remove("/employees.json");
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
            empDB[empCount].last_action_type = (uint8_t)(doc["last_action_type"] | 0);
            empCount++;
        }
    }
    f.close();

    loadFpState(); 
}

const Employee* DataManager::getEmployees() { return empDB; }
int DataManager::getEmployeeCount() { return empCount; }

static void writeFpStateEntry(const String& emp_id, int finger_index, bool enrolled, uint16_t updated_bits);

void DataManager::saveEmployees() {
    s_employeesDirty = true;
    s_employeesDirtyTime = millis();
}

void DataManager::updateEmployeeFpEnrolled(const String& emp_id, bool enrolled, int finger_index) {
    uint16_t updated_bits = 0;
    if (emp_id == "ADMIN") {
        if (enrolled && finger_index >= 0 && finger_index < 10) {
            updated_bits = (uint16_t)(1 << finger_index);
        }
        writeFpStateEntry(emp_id, finger_index, enrolled, updated_bits);
        return;
    }

    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
    bool found = false;
    for (int i = 0; i < empCount; i++) {
        if (empDB[i].id == emp_id) {
            if (enrolled && finger_index >= 0 && finger_index < 10) {
                empDB[i].enrolled_fingers |= (uint16_t)(1 << finger_index);
            } else if (!enrolled && finger_index >= 0 && finger_index < 10) {
                empDB[i].enrolled_fingers &= (uint16_t)~(1 << finger_index);
            } else if (!enrolled) {
                empDB[i].enrolled_fingers = 0;
            }
            empDB[i].fp_enrolled = (empDB[i].enrolled_fingers != 0);
            updated_bits = empDB[i].enrolled_fingers;
            found = true;
            break;
        }
    }
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);

    if (found) {
        writeFpStateEntry(emp_id, finger_index, enrolled, updated_bits);
        return;
    }

    // Fallback if not in empDB
    if (enrolled && finger_index >= 0 && finger_index < 10) {
        updated_bits = (uint16_t)(1 << finger_index);
    }
    writeFpStateEntry(emp_id, finger_index, enrolled, updated_bits);
}

bool DataManager::isWifiConfigured() { return _isWifiConfigured; }
void DataManager::setWifiConfigured(bool state) { 
    if (_isWifiConfigured == state) return;
    _isWifiConfigured = state; 
    saveConfig(); 
}

extern void uiSyncStatusOnSyncResult(bool ok);
void uiSyncStatusRefreshLogs();

void DataManager::setNtpTime(const String& ntpStr) {
    int yr, mo, dy, hr, mn, sc;
    char ampm[3] = "";
    int matched = sscanf(ntpStr.c_str(), "%d-%d-%d %d:%d:%d %2s",
                         &yr, &mo, &dy, &hr, &mn, &sc, ampm);
    if (matched < 6) return;
    
    if (matched == 7) {
        if (ampm[0] == 'P' && hr != 12) hr += 12;
        else if (ampm[0] == 'A' && hr == 12) hr = 0;
    }  struct tm t = {};
    t.tm_year  = yr - 1900;
    t.tm_mon   = mo - 1;
    t.tm_mday  = dy;
    t.tm_hour  = hr;
    t.tm_min   = mn;
    t.tm_sec   = sc;
    t.tm_isdst = -1;
    time_t epoch = mktime(&t);
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, NULL);
    if (Serial) Serial.printf("[RTC] CrowPanel clock set to %s\n", ntpStr.c_str());
}

String DataManager::getCurrentTimeStr() {
    time_t now = time(NULL);
    if (now < 1577836800UL) return "";
    struct tm *t = localtime(&now);
    char buf[12];
    strftime(buf, sizeof(buf), "%m-%d %H:%M", t);
    return String(buf);
}

void DataManager::saveSyncLogs() {
    s_syncLogsDirty = true;
    s_syncLogsDirtyTime = millis();
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
        e.timestamp = 0;
    }
    f.close();
    if (Serial) Serial.printf("[FS] Loaded %d sync log entries.\n", _syncLogCount);
}

void DataManager::addSyncLog(const String& message) {
    String ts = getCurrentTimeStr(); // "MM-DD HH:MM" or "" if RTC not set

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
    if (s_syncLogMutex) xSemaphoreTake(s_syncLogMutex, portMAX_DELAY);
    if (_syncLogCount < MAX_SYNC_LOGS) {
        _syncLogs[_syncLogCount].message = message;
        _syncLogs[_syncLogCount].timestamp = millis();
        _syncLogs[_syncLogCount].timeStr = getCurrentTimeStr();
        _syncLogCount++;
    } else {
        for (int i = 0; i < MAX_SYNC_LOGS - 1; i++) {
            _syncLogs[i] = _syncLogs[i + 1];
        }
        _syncLogs[MAX_SYNC_LOGS - 1].message = message;
        _syncLogs[MAX_SYNC_LOGS - 1].timestamp = millis();
        _syncLogs[MAX_SYNC_LOGS - 1].timeStr = getCurrentTimeStr();
    }
    if (s_syncLogMutex) xSemaphoreGive(s_syncLogMutex);
    saveSyncLogs();
    uiSyncStatusRefreshLogs();
}

const DataManager::SyncLogEntry* DataManager::getSyncLogs() { return _syncLogs; }
int DataManager::getSyncLogCount() { return _syncLogCount; }

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

    if (s_empSyncActive) {
        Serial.println("[DATA] applySyncBuffer: aborting in-flight JSON sync to apply binary buffer.");
        syncAbort();
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

    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
    empCount = count;
    for (int i = 0; i < count; i++) {
        empDB[i].id            = String(i + 1);
        empDB[i].name          = String(incoming[i].name);
        empDB[i].dept          = String(incoming[i].department);
        empDB[i].job_title     = String(incoming[i].role);
        empDB[i].branch        = String(incoming[i].branch);
        empDB[i].fp_enrolled   = false;
        empDB[i].enrolled_fingers = 0;
        empDB[i].last_action_type = 0;
    }
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);

    _lastSyncTimestamp = millis();
    saveEmployees();
    loadFpState();

    if (!changed) {
        addSyncLog("Employee data is latest");
    } else {
        addSyncLog("Synced " + String(count) + " employees");
    }

    if (Serial) Serial.printf("[DATA] applySyncBuffer: replaced %d records.\n", count);
}

static const char* FP_STATE_FILE = "/fp_state.jsonl";

static void writeFpStateEntry(const String& emp_id, int finger_index, bool enrolled, uint16_t updated_bits) {
    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    File f = LittleFS.open(FP_STATE_FILE, "a");
    if (f) {
        StaticJsonDocument<128> doc;
        doc["emp_id"] = emp_id;
        doc["enrolled_fingers"] = updated_bits;
        serializeJson(doc, f);
        f.println();
        f.close();
    }
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
}

void DataManager::loadFpState() {
    if (LittleFS.exists("/fp_state.json")) {
        File oldF = LittleFS.open("/fp_state.json", "r");
        if (oldF) {
            DynamicJsonDocument doc(8192);
            if (deserializeJson(doc, oldF) == DeserializationError::Ok) {
                File newF = LittleFS.open(FP_STATE_FILE, "a");
                if (newF) {
                    JsonArray arr = doc["slots"].as<JsonArray>();
                    for (JsonObject entry : arr) {
                        StaticJsonDocument<128> ldoc;
                        ldoc["emp_id"] = entry["emp_id"];
                        ldoc["enrolled_fingers"] = entry["enrolled_fingers"];
                        serializeJson(ldoc, newF);
                        newF.println();
                    }
                    newF.close();
                }
            }
            oldF.close();
            LittleFS.remove("/fp_state.json");
        }
    }

    if (!LittleFS.exists(FP_STATE_FILE)) return;
    File f = LittleFS.open(FP_STATE_FILE, "r");
    if (!f) return;

    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
    int lineCount = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, line) != DeserializationError::Ok) continue;
        
        String emp_id = doc["emp_id"] | "";
        uint16_t bits = (uint16_t)(doc["enrolled_fingers"] | 0);
        if (emp_id.length() == 0) continue;

        for (int i = 0; i < empCount; i++) {
            if (empDB[i].id == emp_id) {
                empDB[i].enrolled_fingers = bits;
                empDB[i].fp_enrolled      = (bits != 0);
                break;
            }
        }
        
        lineCount++;
        if (lineCount % 2 == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);
    f.close();

    if (lineCount > empCount + 10) {
        File fOut = LittleFS.open("/fp_state_tmp.jsonl", "w");
        if (fOut) {
            if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
            for (int i = 0; i < empCount; i++) {
                if (empDB[i].enrolled_fingers != 0) {
                    StaticJsonDocument<128> ldoc;
                    ldoc["emp_id"] = empDB[i].id;
                    ldoc["enrolled_fingers"] = empDB[i].enrolled_fingers;
                    serializeJson(ldoc, fOut);
                    fOut.println();
                }
            }
            if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);
            
            uint16_t admin_bits = getEnrolledMask("ADMIN");
            if (admin_bits != 0) {
                StaticJsonDocument<128> ldoc;
                ldoc["emp_id"] = "ADMIN";
                ldoc["enrolled_fingers"] = admin_bits;
                serializeJson(ldoc, fOut);
                fOut.println();
            }
            
            fOut.close();
            LittleFS.remove(FP_STATE_FILE);
            LittleFS.rename("/fp_state_tmp.jsonl", FP_STATE_FILE);
            if (Serial) Serial.println("[FS] fp_state.jsonl compacted.");
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
    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    File f = LittleFS.open("/wifi_creds.json", "w");
    if (!f) {
        if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
        return;
    }
    StaticJsonDocument<1024> doc;
    JsonArray arr = doc.createNestedArray("networks");
    for (int i = 0; i < _wifiCount; i++) {
        JsonObject net = arr.createNestedObject();
        net["ssid"] = _wifiSsid[i];
        net["pass"] = _wifiPass[i];
    }
    serializeJson(doc, f);
    f.close();
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
}

void DataManager::saveWifiCredentials(const String& ssid, const String& pass) {
    if (ssid.length() == 0) return;

    if (_wifiCount > 0 && _wifiSsid[0] == ssid && _wifiPass[0] == pass) {
        return; 
    }
    int existing_idx = -1;
    for (int i = 0; i < _wifiCount; i++) {
        if (_wifiSsid[i] == ssid) { existing_idx = i; break; }
    }

    if (existing_idx != -1) {
        for (int i = existing_idx; i > 0; i--) {
            _wifiSsid[i] = _wifiSsid[i - 1];
            _wifiPass[i] = _wifiPass[i - 1];
        }
    } else {
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
    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    if (LittleFS.exists("/wifi_creds.json")) {
        LittleFS.remove("/wifi_creds.json");
    }
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
}

String DataManager::getWifiSsid(int index) { return (index >= 0 && index < _wifiCount) ? _wifiSsid[index] : ""; }
String DataManager::getWifiPass(int index) { return (index >= 0 && index < _wifiCount) ? _wifiPass[index] : ""; }
int DataManager::getSavedWifiCount() { return _wifiCount; }
bool   DataManager::hasSavedWifi() { return _wifiCount > 0; }

void DataManager::setWifiConnected(bool connected) { 
    if (_wifiConnected && !connected) {
        _wifiDropTime = millis();
        if (isActivated()) addSyncLog("Connection lost");
    } else if (!_wifiConnected && connected) {
        _wifiDropTime = 0;
        if (isActivated()) addSyncLog("Connection restored");
    }
    _wifiConnected = connected; 
}
bool DataManager::isWifiConnected() { return _wifiConnected; }

bool DataManager::isActivated() { return _isActivated; }
String DataManager::getHardwareCode() { return _hwCode; }

String DataManager::getDeviceId() {
    if (_hwCode.length() > 0) return _hwCode;
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

void DataManager::setFailedAttempts(int attempts) {
    _failedAttempts = attempts;
}

void DataManager::setLockoutStartTime(unsigned long time) {
    _lockoutStartTime = time;
}

bool DataManager::activate(const String& code) {
    if (isLockedOut()) return false;

    bool valid = (code.length() == 12);
    for (int i = 0; i < 12 && valid; i++) {
        if (code[i] < 'A' || code[i] > 'Z') valid = false;
    }
    
    if (valid) {
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
    _adminPin         = "0000";
    saveConfig();
    clearWifiCredentials();

    nukeDatabase();

    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    if (s_logMutex) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    liveLogCount = 0;
    LittleFS.remove("/attendance.jsonl");
    if (s_logMutex) xSemaphoreGive(s_logMutex);
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
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

int DataManager::getVolume() {
    return _volume;
}

void DataManager::setVolume(int val) {
    if (val < 0) val = 0;
    if (val > 30) val = 30;
    _volume = val;
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
    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
    s_empSyncActive = true;
    for (int i = 0; i < empCount; i++) {
        oldDB[i] = empDB[i];
    }
    oldEmpCount = empCount;
    empCount = 0;
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);
}

static String sanitizeUTF8(const String& input) {
    String out = "";
    out.reserve(input.length());
    for (int i = 0; i < (int)input.length(); i++) {
        uint8_t c = (uint8_t)input[i];
        if (c < 128) {
            out += (char)c;
        }
    }
    return out;
}

void DataManager::syncAddEmployee(const String& id, const String& name, const String& dept, const String& job, const String& branch) {
    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
    if (empCount < 150) {
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
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);
}

void DataManager::syncDone() {
    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
    s_empSyncActive = false;
    _lastSyncTimestamp = millis();
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);
    
    saveEmployees();
    loadFpState();
}

void DataManager::syncAbort() {
    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
    s_empSyncActive = false;
    empCount = oldEmpCount;
    for (int i = 0; i < empCount; i++) {
        empDB[i] = oldDB[i];
    }
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);
    
    loadFpState();
}

void DataManager::nukeDatabase() {
    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    if (s_empDataMutex) xSemaphoreTake(s_empDataMutex, portMAX_DELAY);
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
        empDB[i].last_action_type = 0;
    }

    LittleFS.remove("/employees.jsonl");
    
    if (LittleFS.exists(FP_STATE_FILE)) {
        File f = LittleFS.open(FP_STATE_FILE, "r");
        uint16_t admin_bits = 0;
        bool has_admin = false;
        
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) continue;
                StaticJsonDocument<128> doc;
                if (deserializeJson(doc, line) != DeserializationError::Ok) continue;
                if (String(doc["emp_id"] | "") == "ADMIN") {
                    admin_bits = (uint16_t)(doc["enrolled_fingers"] | 0);
                    has_admin = true;
                }
            }
            f.close();
        }
        
        LittleFS.remove(FP_STATE_FILE);
        if (has_admin && admin_bits != 0) {
            writeFpStateEntry("ADMIN", -1, true, admin_bits);
        }
    }
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
    if (s_empDataMutex) xSemaphoreGive(s_empDataMutex);
}

bool DataManager::saveTemplate(const String& empId, int fingerIndex, const uint8_t* data, size_t len) {
    String path = "/templates/" + empId + "_" + String(fingerIndex) + ".bin";
    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    File f = LittleFS.open(path, "w");
    if (!f) {
        if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
        if (Serial) Serial.println("[FS] Failed to open " + path + " for writing");
        return false;
    }
    size_t written = f.write(data, len);
    f.close();
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
    
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
    if (g_lvglMutex) xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY);
    bool exists = LittleFS.exists(path);
    bool success = false;
    if (exists) {
        success = LittleFS.remove(path);
    }
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
    return success;
}

bool DataManager::adminTemplateExists() {
    for (int f = 0; f < 10; f++) {
        if (templateExists("ADMIN", f)) return true;
    }
    return false;
}

uint16_t DataManager::getEnrolledMask(const String& empId) {
    for (int i = 0; i < empCount; i++) {
        if (empDB[i].id == empId) return empDB[i].enrolled_fingers;
    }
    if (!LittleFS.exists(FP_STATE_FILE)) return 0;
    File f = LittleFS.open(FP_STATE_FILE, "r");
    if (!f) return 0;
    
    uint16_t latest_bits = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, line) != DeserializationError::Ok) continue;
        if (String(doc["emp_id"] | "") == empId) {
            latest_bits = (uint16_t)(doc["enrolled_fingers"] | 0);
        }
    }
    f.close();
    return latest_bits;
}
