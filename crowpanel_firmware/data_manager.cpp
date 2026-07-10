#include "data_manager.h"
#include <LittleFS.h>

Employee DataManager::empDB[50];
int DataManager::empCount = 0;
bool DataManager::_isWifiConfigured = false;
bool DataManager::_isActivated = false;
String DataManager::_hwCode = "";

void DataManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS Mount Failed. Formatting...");
        return;
    }
    
    // Generate Hardware Code from MAC (8 chars)
    uint32_t mac32 = (uint32_t)ESP.getEfuseMac();
    char hw[9];
    snprintf(hw, sizeof(hw), "%08X", mac32);
    _hwCode = String(hw);
    
    createInitialFilesIfMissing();
    loadConfig();
    loadEmployees();
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
    }
    f.close();
}

void DataManager::saveConfig() {
    File f = LittleFS.open("/config.json", "w");
    if (!f) return;
    
    StaticJsonDocument<256> doc;
    doc["wifiConfigured"] = _isWifiConfigured;
    doc["activated"] = _isActivated;
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

bool DataManager::isActivated() { return _isActivated; }
String DataManager::getHardwareCode() { return _hwCode; }

bool DataManager::activate(const String& code) {
    // Basic mock logic: 12 uppercase characters
    // E.g., any 12 char uppercase string is valid for mock
    if (code.length() == 12) {
        for (int i = 0; i < 12; i++) {
            if (code[i] < 'A' || code[i] > 'Z') return false; // Must be uppercase alpha
        }
        _isActivated = true;
        saveConfig();
        return true;
    }
    return false;
}
