#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── Hardcoded Device ID ───────────────────────────────────────────
// This must match the DEVICE_ID defined in wroom_firmware.ino.
// It is sent to the backend API to verify if this unit is activated.
#define DEVICE_ID_HARDCODED "P001-2607-6AEC-Z2GD"

struct Employee {
    int id;
    String name;
    String dept;
    String job_title;
    String branch;
    bool fp_enrolled;
    int enrolled_finger;
};

struct AttendanceLog {
    String name;
    String time_str;
    bool is_time_in;
    bool synced;
};

class DataManager {
public:
    static void begin();
    
    // Employee Data
    static const Employee* getEmployees();
    static int getEmployeeCount();
    static int getEnrolledFingerprintCount();
    static void saveEmployees();                              // Persist empDB to LittleFS
    static void updateEmployeeFpEnrolled(int emp_id, bool enrolled, int finger_index = -1); // Update flag + save

    // Attendance Log Data
    static const AttendanceLog* getAttendanceLogs();
    static int getAttendanceLogCount();
    static int getUnsyncedAttendanceCount();
    
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

    // WiFi credential persistence
    static void saveWifiCredentials(const String& ssid, const String& pass);
    static void clearWifiCredentials();
    static String getWifiSsid();
    static String getWifiPass();
    static bool hasSavedWifi();

    // Live runtime Wi-Fi state (not persisted — updated by CommManager on every WIFI_STATUS event)
    static void setWifiConnected(bool connected);
    static bool isWifiConnected();

    static int getFailedAttempts();
    static unsigned long getLockoutStartTime();
    static bool isLockedOut();

private:
    static void createInitialFilesIfMissing();
    static void loadEmployees();
    static void loadConfig();
    static void saveConfig();
    
    static Employee empDB[50];
    static int empCount;
    static bool _isWifiConfigured;
    static bool _isActivated;
    static String _hwCode;
    static int _failedAttempts;
    static unsigned long _lockoutStartTime;
    static String _wifiSsid;
    static String _wifiPass;
    static String _activationCode;
    static bool _wifiConnected;
    static void loadWifiCredentials();
    static void saveWifiCredentialsToFs();
};

#endif // DATA_MANAGER_H
