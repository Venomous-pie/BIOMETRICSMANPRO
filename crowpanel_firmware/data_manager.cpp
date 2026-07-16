#include "data_manager.h"
#include <LittleFS.h>

Employee DataManager::empDB[50];
int DataManager::empCount = 0;
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
    if (true) { // Force overwrite for testing
        // Serial.println("[FS] Creating initial employees.json...");
        File f = LittleFS.open("/employees.json", "w");
        if (f) {
            f.print(R"([
  {"id":1,"name":"Admin","dept":"Admin","job_title":"System Admin","branch":"Main","fp_enrolled":false},
  {"id":2,"name":"Claire Jem Dedicatoria","dept":"HR","job_title":"Intern Tech Lead","branch":"Nasya","fp_enrolled":false},
  {"id":3,"name":"Alice Santos","dept":"HR","job_title":"HR Manager","branch":"Main","fp_enrolled":false},
  {"id":4,"name":"Bob Cruz","dept":"IT","job_title":"Developer","branch":"Main","fp_enrolled":false},
  {"id":5,"name":"Carol Reyes","dept":"Finance","job_title":"Accountant","branch":"Main","fp_enrolled":false},
  {"id":6,"name":"Dave Lim","dept":"Security","job_title":"Guard","branch":"Main","fp_enrolled":false},
  {"id":7,"name":"Eve Tan","dept":"Admin","job_title":"Clerk","branch":"Main","fp_enrolled":false}
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
