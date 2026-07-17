#include "data_manager.h"
#include <LittleFS.h>

Employee DataManager::empDB[50];
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
String DataManager::_wifiSsid = "";
String DataManager::_wifiPass = "";
String DataManager::_activationCode = "";
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
    if (!LittleFS.exists("/employees.json")) {  // Only create when file is absent
        // Serial.println("[FS] Creating initial employees.json...");
        File f = LittleFS.open("/employees.json", "w");
        if (f) {
            f.print(R"([
  {"id":1,"name":"Admin","dept":"Admin","job_title":"System Admin","branch":"Main","fp_enrolled":false},
  {"id":2,"name":"Christopher G. Francisco","dept":"Executive","job_title":"CIO","branch":"Main","fp_enrolled":false},
  {"id":3,"name":"Reden Lamosa","dept":"IT","job_title":"Senior Developer","branch":"Main","fp_enrolled":false},
  {"id":4,"name":"Jean Erica Velasco","dept":"IT","job_title":"Intern Lead","branch":"Main","fp_enrolled":false},
  {"id":5,"name":"Claire Jem Dedicatoria","dept":"IT","job_title":"Intern Technical Team Lead","branch":"Main","fp_enrolled":false},
  {"id":6,"name":"Maria Alaine Jeanne A. Terante","dept":"IT","job_title":"Assistant to Technical Team Lead","branch":"Main","fp_enrolled":false},
  {"id":7,"name":"Jhonnalyn Belano","dept":"IT","job_title":"QA","branch":"Main","fp_enrolled":false},
  {"id":8,"name":"Kenneth Simbolas","dept":"Hardware","job_title":"PCB Board Designer","branch":"Main","fp_enrolled":false},
  {"id":9,"name":"John Rustom Reginio","dept":"Hardware","job_title":"PCB Board Designer","branch":"Main","fp_enrolled":false},
  {"id":10,"name":"Sharlene Loria","dept":"Hardware","job_title":"Hardware","branch":"Main","fp_enrolled":false},
  {"id":11,"name":"Mark Jaestin Cabañelis","dept":"Design","job_title":"Figma / UI Design","branch":"Main","fp_enrolled":false}
])");
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
    serializeJson(doc, f);
    f.close();
}

void DataManager::loadEmployees() {
    File f = LittleFS.open("/employees.json", "r");
    if (!f) return;
    
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        empCount = 0;
        for (JsonObject e : doc.as<JsonArray>()) {
            if (empCount >= 50) break;
            empDB[empCount].id          = e["id"].as<int>();
            empDB[empCount].name        = e["name"].as<String>();
            empDB[empCount].dept        = e["dept"].as<String>();
            empDB[empCount].job_title   = e.containsKey("job_title") ? e["job_title"].as<String>() : "";
            empDB[empCount].branch      = e.containsKey("branch") ? e["branch"].as<String>() : "";
            empDB[empCount].fp_enrolled = e.containsKey("fp_enrolled") ? e["fp_enrolled"].as<bool>() : false;
            empDB[empCount].enrolled_finger = e.containsKey("enrolled_finger") ? e["enrolled_finger"].as<int>() : -1;
            empCount++;
        }
        // Serial.printf("[DB] %d employees loaded from LittleFS\n", empCount);
    } else {
        // Serial.println("[DB] ERROR: JSON parse failed for employees");
    }
    f.close();
}

const Employee* DataManager::getEmployees() { return empDB; }
int DataManager::getEmployeeCount() { return empCount; }

// Serialise the current in-RAM empDB back to /employees.json on LittleFS.
void DataManager::saveEmployees() {
    File f = LittleFS.open("/employees.json", "w");
    if (!f) return;
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < empCount; i++) {
        JsonObject e = arr.createNestedObject();
        e["id"]          = empDB[i].id;
        e["name"]        = empDB[i].name;
        e["dept"]        = empDB[i].dept;
        e["job_title"]   = empDB[i].job_title;
        e["branch"]      = empDB[i].branch;
        e["fp_enrolled"] = empDB[i].fp_enrolled;
        e["enrolled_finger"] = empDB[i].enrolled_finger;
    }
    serializeJson(doc, f);
    f.close();
}

// Update a single employee's fp_enrolled flag in RAM and persist to flash.
void DataManager::updateEmployeeFpEnrolled(int emp_id, bool enrolled, int finger_index) {
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
    if (!LittleFS.exists("/wifi_creds.json")) return;
    File f = LittleFS.open("/wifi_creds.json", "r");
    if (!f) return;
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        _wifiSsid = doc["ssid"] | "";
        _wifiPass = doc["pass"] | "";
    }
    f.close();
    // Serial.printf("[WiFi] Loaded saved credentials for SSID: %s\n", _wifiSsid.c_str());
}

void DataManager::saveWifiCredentialsToFs() {
    File f = LittleFS.open("/wifi_creds.json", "w");
    if (!f) return;
    StaticJsonDocument<256> doc;
    doc["ssid"] = _wifiSsid;
    doc["pass"] = _wifiPass;
    serializeJson(doc, f);
    f.close();
}

void DataManager::saveWifiCredentials(const String& ssid, const String& pass) {
    _wifiSsid = ssid;
    _wifiPass = pass;
    saveWifiCredentialsToFs();
    // Serial.printf("[WiFi] Credentials saved for SSID: %s\n", ssid.c_str());
}

void DataManager::clearWifiCredentials() {
    _wifiSsid = "";
    _wifiPass = "";
    if (LittleFS.exists("/wifi_creds.json")) {
        LittleFS.remove("/wifi_creds.json");
    }
    // Serial.println("[WiFi] Saved credentials cleared (user switched network)");
}

String DataManager::getWifiSsid() { return _wifiSsid; }
String DataManager::getWifiPass() { return _wifiPass; }
bool   DataManager::hasSavedWifi() { return _wifiSsid.length() > 0; }

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
    return "ManPro Biometric";
}
