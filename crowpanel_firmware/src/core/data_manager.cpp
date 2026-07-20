#include "data_manager.h"
#include "comm_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

Employee DataManager::empDB[150];
int DataManager::empCount = 0;

static AttendanceLog mockLogs[] = {
    {"Christopher G. Francisco", "6/30/2026 7:50 AM", true, true},
    {"Reden Lamosa", "6/30/2026 7:55 AM", true, true},
    {"Jean Erica Velasco", "6/30/2026 8:01 AM", true, true},
    {"Claire Jem Dedicatoria", "6/30/2026 8:05 AM", true, true},
    {"Jhonnalyn Belano", "6/30/2026 8:15 AM", true, true},
    {"Christopher G. Francisco", "6/30/2026 5:00 PM", false, true},
    {"Reden Lamosa", "6/30/2026 5:05 PM", false, true},
    {"Jean Erica Velasco", "6/30/2026 5:10 PM", false, false},
    
    {"Christopher G. Francisco", "7/1/2026 7:45 AM", true, true},
    {"Reden Lamosa", "7/1/2026 7:50 AM", true, true},
    {"Maria Alaine Jeanne A. Terante", "7/1/2026 8:00 AM", true, true},
    {"Kenneth Simbolas", "7/1/2026 8:02 AM", true, true},
    {"John Rustom Reginio", "7/1/2026 8:10 AM", true, true},
    {"Sharlene Loria", "7/1/2026 8:15 AM", true, true},
    {"Mark Jaestin Cabañelis", "7/1/2026 8:30 AM", true, true},
    {"Jhonnalyn Belano", "7/1/2026 12:15 PM", false, false}
};

const AttendanceLog* DataManager::getAttendanceLogs() { return mockLogs; }
int DataManager::getAttendanceLogCount() { return sizeof(mockLogs) / sizeof(mockLogs[0]); }

int DataManager::getUnsyncedAttendanceCount() {
    int count = 0;
    for (int i = 0; i < getAttendanceLogCount(); i++) {
        if (!mockLogs[i].synced) count++;
    }
    return count;
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
        // Serial.println("[FS] LittleFS Mount Failed. Formatting...");
        return;
    }
    
    // Generate Hardware Code from MAC (XXXX-XXXX format)
    uint32_t mac32 = (uint32_t)ESP.getEfuseMac();
    char hw[10];
    snprintf(hw, sizeof(hw), "%04X-%04X", (mac32 >> 16) & 0xFFFF, mac32 & 0xFFFF);
    _hwCode = String(hw);
    
    createInitialFilesIfMissing();
    loadConfig();
    loadEmployees();
    loadWifiCredentials();
}

void DataManager::createInitialFilesIfMissing() {
    if (!LittleFS.exists("/employees.jsonl")) {
        File f = LittleFS.open("/employees.jsonl", "w");
        if (f) {
            // No default employees needed for API sync, just an empty file or basic admin
            f.println("{\"id\":1,\"name\":\"Admin\",\"dept\":\"Admin\",\"job_title\":\"System Admin\",\"branch\":\"Main\",\"fp_enrolled\":false,\"enrolled_finger\":-1}");
            f.close();
        }
    }
    
    if (!LittleFS.exists("/config.json")) {
        // Serial.println("[FS] Creating initial config.json...");
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
        
        if (_brightness < 50) _brightness = 50; // enforce minimum limit
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
        // Fallback to old file if jsonl doesn't exist yet
        f = LittleFS.open("/employees.json", "r");
        if (!f) return;
        
        StaticJsonDocument<4096> doc; // Try to parse if it's small enough
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
        saveEmployees(); // Upgrade to JSONL
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
}

const Employee* DataManager::getEmployees() { return empDB; }
int DataManager::getEmployeeCount() { return empCount; }

// Serialise the current in-RAM empDB back to /employees.jsonl on LittleFS.
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

// Update a single employee's fp_enrolled flag in RAM and persist to flash.
void DataManager::updateEmployeeFpEnrolled(const String& emp_id, bool enrolled, int finger_index) {
    for (int i = 0; i < empCount; i++) {
        if (empDB[i].id == emp_id) {
            empDB[i].fp_enrolled = enrolled;
            empDB[i].enrolled_finger = finger_index;
            saveEmployees();
            return;
        }
    }
}

bool DataManager::isWifiConfigured() { return _isWifiConfigured; }
void DataManager::setWifiConfigured(bool state) { 
    _isWifiConfigured = state; 
    saveConfig(); 
}

// ── WiFi credential persistence ────────────────────────────────────────────
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
            // Fallback for old format
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
    // Check if it already exists, remove it if it does
    int existing_idx = -1;
    for (int i = 0; i < _wifiCount; i++) {
        if (_wifiSsid[i] == ssid) {
            existing_idx = i;
            break;
        }
    }
    
    // Shift elements to make room at the front
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

// Returns the full device ID shown on the register page and sent to the API.
String DataManager::getDeviceId() {
    return String(DEVICE_ID_HARDCODED);
}

// Called when WROOM receives ACTIVATION_STATUS:activated=true from the server.
// Persists activated state so it survives reboot, then notifies WROOM to unlock.
void DataManager::setActivatedByServer(bool state) {
    _isActivated = state;
    saveConfig();
}

// Stores the real device_token received from the server
void DataManager::setDeviceToken(const String& token) {
    _activationCode = token;
    saveConfig();
}


bool DataManager::isLockedOut() {
    if (_lockoutStartTime > 0) {
        if (millis() - _lockoutStartTime >= 600000) { // 10 minutes passed
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

    // Basic mock logic: 12 uppercase characters
    bool valid = true;
    if (code.length() != 12) valid = false;
    for (int i = 0; i < 12; i++) {
        if (code[i] < 'A' || code[i] > 'Z') valid = false; // Must be uppercase alpha
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
            // In a real system, the server would invalidate the old code here.
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
    // Serial.println("[FS] Factory reset: config and WiFi credentials cleared.");
}

String DataManager::getActivationCode() {
    return _activationCode;
}

// String DataManager::getDeviceId() is implemented at the top using DEVICE_ID_HARDCODED

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
    if (val < 50) val = 50; // enforce minimum limit
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

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

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


