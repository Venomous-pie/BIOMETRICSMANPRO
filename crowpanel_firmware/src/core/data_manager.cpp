#include "data_manager.h"
#include "comm_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "sync_protocol.h"

Employee DataManager::empDB[150];
int DataManager::empCount = 0;

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
                body["action"]        = logToSync.is_time_in ? "IN" : "OUT";
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

void DataManager::addLog(const String& name, const String& time_str,
                         bool is_time_in, int confidence, int slot) {
    portENTER_CRITICAL(&logMutex);
    if (liveLogCount < MAX_LOGS) {
        liveLogs[liveLogCount++] = AttendanceLog{name, time_str, is_time_in, false, confidence, slot};
    } else {
        // Ring: shift everything left, drop oldest
        memmove(&liveLogs[0], &liveLogs[1], sizeof(AttendanceLog) * (MAX_LOGS - 1));
        liveLogs[MAX_LOGS - 1] = AttendanceLog{name, time_str, is_time_in, false, confidence, slot};
    }
    portEXIT_CRITICAL(&logMutex);
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
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, line) != DeserializationError::Ok) continue;
        const char* act = doc["action"] | "IN";
        liveLogs[liveLogCount++] = AttendanceLog{
            doc["name"]   | "",
            doc["ts"]     | "",
            (strcmp(act, "IN") == 0),
            doc["synced"] | false,
            doc["conf"]   | 0,
            doc["slot"]   | 0
        };
    }
    f.close();
}

void DataManager::saveAttendanceLogs() {
    File f = LittleFS.open("/attendance.jsonl", "w");
    if (!f) return;
    for (int i = 0; i < liveLogCount; i++) {
        StaticJsonDocument<256> doc;
        doc["name"]   = liveLogs[i].name;
        doc["ts"]     = liveLogs[i].time_str;
        doc["action"] = liveLogs[i].is_time_in ? "IN" : "OUT";
        doc["synced"] = liveLogs[i].synced;
        doc["conf"]   = liveLogs[i].confidence;
        doc["slot"]   = liveLogs[i].slot;
        serializeJson(doc, f);
        f.println();
    }
    f.close();
}

void DataManager::uploadPendingLogs() {
    // Deprecated: Uploads are now handled automatically by the background attendanceUploadTask.
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

unsigned long DataManager::_lastSyncTimestamp = 0;

void DataManager::begin() {
    if (!LittleFS.begin(true)) {
        return;
    }
    
    uint32_t mac32 = (uint32_t)ESP.getEfuseMac();
    char hw[10];
    snprintf(hw, sizeof(hw), "%04X-%04X", (mac32 >> 16) & 0xFFFF, mac32 & 0xFFFF);
    _hwCode = String(hw);
    
    createInitialFilesIfMissing();
    loadConfig();
    loadEmployees();
    loadWifiCredentials();
    loadAttendanceLogs();

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
                empDB[empCount].enrolled_finger = e["enrolled_finger"] | -1;
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
            empDB[empCount].enrolled_finger = doc["enrolled_finger"] | -1;
            empCount++;
        }
    }
    f.close();
    loadFpState(); // re-apply slot-keyed fp_enrolled after reading employees from disk
}

const Employee* DataManager::getEmployees() { return empDB; }
int DataManager::getEmployeeCount() { return empCount; }

static void writeFpStateEntry(int slot, const String& name, bool enrolled);

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
        doc["enrolled_finger"] = empDB[i].enrolled_finger;
        serializeJson(doc, f);
        f.println();
    }
    f.close();
}

void DataManager::updateEmployeeFpEnrolled(const String& emp_id, bool enrolled, int finger_index) {
    for (int i = 0; i < empCount; i++) {
        if (empDB[i].id == emp_id) {
            empDB[i].fp_enrolled = enrolled;
            empDB[i].enrolled_finger = finger_index;
            // Persist to both JSONL (for fast boot re-load) and fp_state.json
            // (stable across payroll syncs, keyed by slot not by positional id).
            saveEmployees();
            writeFpStateEntry(finger_index, empDB[i].name, enrolled);
            return;
        }
    }
}

bool DataManager::isWifiConfigured() { return _isWifiConfigured; }
void DataManager::setWifiConfigured(bool state) { 
    _isWifiConfigured = state; 
    saveConfig(); 
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

    int count = len / sizeof(EmployeeSync);
    if (count > MAX_EMP_RECORDS) count = MAX_EMP_RECORDS;

    const EmployeeSync* incoming = (const EmployeeSync*)buffer;

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
        empDB[i].enrolled_finger = -1;
    }

    _lastSyncTimestamp = millis();
    saveEmployees();
    loadFpState(); // re-apply fingerprint enrollment status from the stable side-table

    if (Serial) Serial.printf("[DATA] applySyncBuffer: replaced %d records.\n", count);
}

// ── Fingerprint-state side-table ──────────────────────────────────────────────
// fp_state.json stores fingerprint enrollment status keyed by AS608 slot number.
// It is written only by updateEmployeeFpEnrolled() and never touched by payroll
// sync, so it survives full employee-list replacements intact.
//
// Format: { "slots": [ {"slot":1,"name":"Maria Alaine..."}, ... ] }
//   - slot   : AS608 physical slot number (stable — assigned at enroll time)
//   - name   : snapshot of the employee name at enroll time (informational only)
//
// At load time loadFpState() scans empDB for a name match and sets fp_enrolled.
// If a name was corrected between syncs and no match is found, the slot is simply
// not flagged — the template still exists on the AS608 sensor and will still match
// on scan.  The UI badge will clear until the next enroll flow updates the record.

static const char* FP_STATE_FILE = "/fp_state.json";

// Writes an entry into fp_state.json for a newly enrolled slot.
// name is stored as a snapshot so the record is self-describing, but it is NOT
// used as a matching key at load time — slot is the key.
static void writeFpStateEntry(int slot, const String& name, bool enrolled) {
    StaticJsonDocument<4096> doc;

    if (LittleFS.exists(FP_STATE_FILE)) {
        File f = LittleFS.open(FP_STATE_FILE, "r");
        if (f) {
            deserializeJson(doc, f);
            f.close();
        }
    }

    if (!doc.containsKey("slots")) {
        doc.createNestedArray("slots");
    }

    JsonArray arr = doc["slots"].as<JsonArray>();

    // Update existing entry if slot already present
    for (JsonObject entry : arr) {
        if ((entry["slot"] | -1) == slot) {
            entry["name"]     = name;
            entry["enrolled"] = enrolled;
            File f = LittleFS.open(FP_STATE_FILE, "w");
            if (f) { serializeJson(doc, f); f.close(); }
            return;
        }
    }

    // New slot
    JsonObject entry = arr.createNestedObject();
    entry["slot"]     = slot;
    entry["name"]     = name;
    entry["enrolled"] = enrolled;

    File f = LittleFS.open(FP_STATE_FILE, "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

// Re-applies fp_enrolled to empDB from fp_state.json after a sync.
// The match key is slot number: we set fp_enrolled on whichever empDB entry has
// enrolled_finger == slot (set during enroll), or if enrolled_finger is not yet
// known, we leave it for the next updateEmployeeFpEnrolled() call.
void DataManager::loadFpState() {
    if (!LittleFS.exists(FP_STATE_FILE)) return;
    File f = LittleFS.open(FP_STATE_FILE, "r");
    if (!f) return;

    StaticJsonDocument<4096> doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();

    JsonArray arr = doc["slots"].as<JsonArray>();
    for (JsonObject entry : arr) {
        int  slot     = entry["slot"]     | -1;
        bool enrolled = entry["enrolled"] | false;
        if (slot < 0) continue;

        const char* name = entry["name"] | "";

        // Find the employee whose name matches this slot
        for (int i = 0; i < empCount; i++) {
            if (empDB[i].name == name) {
                empDB[i].fp_enrolled = enrolled;
                empDB[i].enrolled_finger = slot;
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
    int existing_idx = -1;
    for (int i = 0; i < _wifiCount; i++) {
        if (_wifiSsid[i] == ssid) {
            existing_idx = i;
            break;
        }
    }
    
    int shift_start = (existing_idx != -1) ? existing_idx : ((_wifiCount < 5) ? _wifiCount : 4);
    for (int i = shift_start; i > 0; i--) {
        _wifiSsid[i] = _wifiSsid[i - 1];
        _wifiPass[i] = _wifiPass[i - 1];
    }
    
    _wifiSsid[0] = ssid;
    _wifiPass[0] = pass;
    if (existing_idx == -1 && _wifiCount < 5) {
        _wifiCount++;
    }
    
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

void DataManager::setWifiConnected(bool connected) { _wifiConnected = connected; }
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

    bool valid = true;
    if (code.length() != 12) valid = false;
    for (int i = 0; i < 12; i++) {
        if (code[i] < 'A' || code[i] > 'Z') valid = false;
    }
    
    if (valid) {
        _isActivated = true;
        _activationCode = code;
        _failedAttempts = 0;
        saveConfig();
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
    saveConfig();
    clearWifiCredentials();
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

static Employee oldDB[150];
static int oldEmpCount = 0;

void DataManager::syncStart() {
    oldEmpCount = empCount;
    for (int i = 0; i < empCount; i++) {
        oldDB[i] = empDB[i];
    }
    empCount = 0;
}

void DataManager::syncAddEmployee(const String& id, const String& name, const String& dept, const String& job, const String& branch) {
    if (empCount >= 150) return;
    empDB[empCount].id = id;
    empDB[empCount].name = name;
    empDB[empCount].dept = dept;
    empDB[empCount].job_title = job;
    empDB[empCount].branch = branch;
    
    empDB[empCount].fp_enrolled = false;
    empDB[empCount].enrolled_finger = -1;
    
    for (int j = 0; j < oldEmpCount; j++) {
        if (oldDB[j].id == id) {
            empDB[empCount].fp_enrolled = oldDB[j].fp_enrolled;
            empDB[empCount].enrolled_finger = oldDB[j].enrolled_finger;
            break;
        }
    }
    empCount++;
}

void DataManager::syncDone() {
    saveEmployees();
}

void DataManager::syncAbort() {
    empCount = oldEmpCount;
    for (int i = 0; i < empCount; i++) {
        empDB[i] = oldDB[i];
    }
}

void DataManager::nukeDatabase() {
    empCount = 0;
    oldEmpCount = 0;
    for (int i = 0; i < 150; i++) {
        empDB[i].id = "";
        empDB[i].name = "";
        empDB[i].dept = "";
        empDB[i].job_title = "";
        empDB[i].branch = "";
        empDB[i].fp_enrolled = false;
        empDB[i].enrolled_finger = -1;
    }
    LittleFS.remove("/employees.jsonl");
}
