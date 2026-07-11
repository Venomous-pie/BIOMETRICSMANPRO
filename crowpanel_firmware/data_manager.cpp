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

void DataManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS Mount Failed. Formatting...");
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
    if (!LittleFS.exists("/employees.json")) {
        Serial.println("[FS] Creating initial employees.json...");
        File f = LittleFS.open("/employees.json", "w");
        if (f) {
            f.print(R"([
  {"id":1,"name":"Alice Santos","dept":"HR"},
  {"id":2,"name":"Bob Cruz","dept":"IT"},
  {"id":3,"name":"Carol Reyes","dept":"Finance"},
  {"id":4,"name":"Dave Lim","dept":"Security"},
  {"id":5,"name":"Eve Tan","dept":"Admin"}
])");
            f.close();
        }
    }
    
    if (!LittleFS.exists("/config.json")) {
        Serial.println("[FS] Creating initial config.json...");
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
            empDB[empCount].id   = e["id"].as<int>();
            empDB[empCount].name = e["name"].as<String>();
            empDB[empCount].dept = e["dept"].as<String>();
            empCount++;
        }
        Serial.printf("[DB] %d employees loaded from LittleFS\n", empCount);
    } else {
        Serial.println("[DB] ERROR: JSON parse failed for employees");
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
    Serial.printf("[WiFi] Loaded saved credentials for SSID: %s\n", _wifiSsid.c_str());
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
    Serial.printf("[WiFi] Credentials saved for SSID: %s\n", ssid.c_str());
}

void DataManager::clearWifiCredentials() {
    _wifiSsid = "";
    _wifiPass = "";
    if (LittleFS.exists("/wifi_creds.json")) {
        LittleFS.remove("/wifi_creds.json");
    }
    Serial.println("[WiFi] Saved credentials cleared (user switched network)");
}

String DataManager::getWifiSsid() { return _wifiSsid; }
String DataManager::getWifiPass() { return _wifiPass; }
bool   DataManager::hasSavedWifi() { return _wifiSsid.length() > 0; }

bool DataManager::isActivated() { return _isActivated; }
String DataManager::getHardwareCode() { return _hwCode; }

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
    Serial.println("[FS] Factory reset: config and WiFi credentials cleared.");
}

String DataManager::getActivationCode() {
    return _activationCode;
}

String DataManager::getDeviceId() {
    return "ManPro_Biometric_" + _activationCode + "_" + _hwCode;
}

String DataManager::getDeviceName() {
    return "ManPro Biometric";
}
