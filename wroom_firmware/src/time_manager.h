#pragma once
#include <Arduino.h>
#include <RTClib.h>

extern RTC_DS3231    rtc;
extern bool          rtcValid;
extern bool          ntpSyncPending;
extern unsigned long ntpSyncStart;

// Starts I2C, initializes the DS3231 RTC, and adjusts time if power was lost.
// Call once from setup().
void timeManagerInit();

// Returns the current time as "YYYY-MM-DD HH:MM:SS".
// Source priority: NTP system time → DS3231 RTC → software fallback (compile time + millis).
String getTimestamp();

// Starts a non-blocking NTP sync (calls configTime(); result is polled by ntpProcess()).
// Requires an active WiFi connection.
void syncNTP();

// Checks whether a pending NTP sync has completed or timed out.
// On success, updates the DS3231 RTC and sends NTP_STATUS to the CrowPanel.
// Call every loop() iteration.
void ntpProcess();

// Sets the hardware RTC and system time manually.
void setManualTime(int y, int m, int d, int h, int min);
