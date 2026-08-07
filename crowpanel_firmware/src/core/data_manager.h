#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── Hardcoded Device ID ───────────────────────────────────────────
// This must match the DEVICE_ID defined in wroom_firmware.ino.
// It is sent to the backend API to verify if this unit is activated.
#define DEVICE_ID_HARDCODED "F001-2608-6AEC-ON92" // Make this dynamic, get the versioning pattern from senior the dev

struct Employee {
    String id;
    String name;
    String dept;
    String job_title;
    String branch;
    bool     fp_enrolled;       // true if ANY finger is enrolled
    uint16_t enrolled_fingers;  // bitmask: bit i set = finger index i is enrolled
    uint8_t  last_action_type;  // 0=NONE, 1=IN, 2=OUT, 3=OT_IN, 4=OT_OUT
};

struct AttendanceLog {
    String name;
    String time_str;    // ISO-style: "YYYY-MM-DD HH:MM:SS"
    uint8_t action_type; // 1=IN, 2=OUT, 3=OT_IN, 4=OT_OUT
    bool synced;
    int  confidence;    // AS608 match confidence score
    int  slot;          // AS608 physical template slot
};

class DataManager {
public:
    static void begin();
    
    // Employee Data
    static const Employee* getEmployees();
    static int getEmployeeCount();
    static int getEnrolledFingerprintCount();
    static void saveEmployees();                              // Persist empDB to LittleFS
    static void updateEmployeeFpEnrolled(const String& emp_id, bool enrolled, int finger_index = -1); // Update flag + save
    static void syncStart();
    static void syncAddEmployee(const String& id, const String& name, const String& dept, const String& job, const String& branch);
    static void syncDone();
    static void syncAbort();
    static void applySyncBuffer(const uint8_t* buffer, size_t len);
    static void loadFpState();   // Re-applies fp_enrolled from fp_state.json after a sync
    static void nukeDatabase();

    // Attendance Log Data
    static const AttendanceLog* getAttendanceLogs();
    static int getAttendanceLogCount();
    static int getUnsyncedAttendanceCount();
    static bool isActionAllowed(int slot, uint8_t action_type);
    static void addLog(const String& name, const String& time_str, uint8_t action_type, int confidence, int slot);
    static void uploadPendingLogs();              // POST unsynced logs via async task
    static void saveAttendanceLogs(); // Persist to LittleFS
    
    static bool isWifiConfigured();
    static void setWifiConfigured(bool state);
    static bool isActivated();
    static void setActivated(bool state);
    static void setActivatedByServer(bool state);  // Called when ACTIVATION_STATUS arrives from WROOM
    static void setDeviceToken(const String& token); // Save token from real API response
    static String getHardwareCode();               // Short MAC-derived code (XXXX-XXXX)
    static String getDeviceId();                   // Full device ID shown on register screen
    static bool activate(const String& code);
    static void factoryReset();

    static String getActivationCode();
    static String getDeviceName();
    static void setDeviceName(const String& name);

    static int getBrightness();
    static void setBrightness(int val);
    static int getScreenTimeout();
    static void setScreenTimeout(int val);
    static int getVolume();
    static void setVolume(int val);

    static String getAdminPin();
    static void setAdminPin(const String& pin);

    // Stale data tracking
    static unsigned long getLastSyncTimestamp();
    static bool isDataStale(); // true if > 2 hours since last sync

    // Sync Log tracking
    struct SyncLogEntry {
        String message;
        unsigned long timestamp; // millis() for in-session relative display; 0 = loaded from disk
        String timeStr;          // "MM-DD HH:MM" wall-clock string, persisted across reboots
    };
    static const int MAX_SYNC_LOGS = 5;
    static void addSyncLog(const String& message);
    static const SyncLogEntry* getSyncLogs();
    static int getSyncLogCount();
    // RTC helpers — called by CommManager when NTP_STATUS arrives from WROOM
    static void setNtpTime(const String& ntpStr); // parse WROOM timestamp, set ESP32 RTC
    static String getCurrentTimeStr();             // "MM-DD HH:MM" from system clock
    static unsigned long getWifiDropTime();

    // WiFi credential persistence
    static void saveWifiCredentials(const String& ssid, const String& pass);
    static void clearWifiCredentials(); // Only clears currently connected one if called? Wait, I will just leave it. Or maybe clear all? Let's just clear all.
    static String getWifiSsid(int index = 0);
    static String getWifiPass(int index = 0);
    static int getSavedWifiCount();
    static bool hasSavedWifi();

    // Live runtime Wi-Fi state (not persisted — updated by CommManager on every WIFI_STATUS event)
    static void setWifiConnected(bool connected);
    static bool isWifiConnected();

    static int getFailedAttempts();
    static unsigned long getLockoutStartTime();
    static bool isLockedOut();
    static void setFailedAttempts(int attempts);
    static void setLockoutStartTime(unsigned long time);
    
    // SD Card Deep Storage
    static bool saveTemplate(const String& empId, int fingerIndex, const uint8_t* data, size_t len);
    static bool loadTemplate(const String& empId, int fingerIndex, uint8_t* outData, size_t maxLen, size_t* outLen);
    static bool templateExists(const String& empId, int fingerIndex);
    static bool deleteTemplate(const String& empId, int fingerIndex);
    static bool adminTemplateExists(); // true if any admin finger is stored
    // Returns the enrolled_fingers bitmask for any emp_id.
    // For regular employees: from empDB (fast, RAM). For "ADMIN" and other
    // out-of-empDB identities: reads fp_state.json as fallback.
    static uint16_t getEnrolledMask(const String& empId);

private:
    static void createInitialFilesIfMissing();
    static void loadEmployees();
    static void loadConfig();
    static void saveConfig();
    
    static Employee empDB[150];
    static int empCount;
    static constexpr int MAX_EMP_RECORDS = 150;
    static bool _isWifiConfigured;
    static bool _isActivated;
    static String _hwCode;
    static int _failedAttempts;
    static unsigned long _lockoutStartTime;
    static String _wifiSsid[5];
    static String _wifiPass[5];
    static int _wifiCount;
    static String _activationCode;
    static String _deviceName;
    static int _brightness;
    static int _screenTimeout;
    static int _volume;
    static String _adminPin;
    static bool _wifiConnected;
    static unsigned long _lastSyncTimestamp;
    
    static SyncLogEntry _syncLogs[MAX_SYNC_LOGS];
    static int _syncLogCount;
    static unsigned long _wifiDropTime;
    
    static void loadWifiCredentials();
    static void saveWifiCredentialsToFs();
    static void loadAttendanceLogs();
    static void saveSyncLogs();  // persist _syncLogs[] to /sync_log.jsonl
    static void loadSyncLogs();  // load /sync_log.jsonl on boot
};

#endif // DATA_MANAGER_H
