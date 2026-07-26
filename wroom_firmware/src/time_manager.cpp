#include "time_manager.h"
#include "comms.h"
#include <Wire.h>
#include <WiFi.h>
#include <time.h>

RTC_DS3231    rtc;
bool          rtcValid       = false;
bool          ntpSyncPending = false;
unsigned long ntpSyncStart   = 0;

void timeManagerInit() {
  Wire.begin(21, 22); // SDA=GPIO21, SCL=GPIO22
  if (rtc.begin()) {
    rtcValid = true;
    if (rtc.lostPower()) {
      // RTC battery ran out — seed with firmware compile time as a starting point.
      // NTP will correct it once WiFi connects.
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.println("[RTC] Power lost — synced to compile time");
    }
    Serial.println("[RTC] Ready: " + getTimestamp());
  } else {
    rtcValid = false;
    Serial.println("[RTC] NOT FOUND — using software fallback clock");
  }
}

String getTimestamp() {
  struct tm t = {};

  // NTP is the most accurate source. getLocalTime() succeeds once configTime()
  // has synced and the ESP32 system clock is set.
  if (getLocalTime(&t, 0) && (t.tm_year + 1900) >= 2020) {
    char buf[30];
    int h12 = t.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (t.tm_hour >= 12) ? "PM" : "AM";
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d %s",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             h12, t.tm_min, t.tm_sec, ampm);
    return String(buf);
  }

  // Fall back to the DS3231 hardware clock if NTP has not synced yet.
  DateTime now;
  if (rtcValid) {
    now = rtc.now();
  } else {
    // Last resort: use compile time plus elapsed millis. This drifts over time
    // but provides a reasonable timestamp until NTP can sync.
    now = DateTime(F(__DATE__), F(__TIME__)) + TimeSpan(millis() / 1000);
  }

  char buf[30];
  int h12 = now.hour() % 12;
  if (h12 == 0) h12 = 12;
  const char* ampm = (now.hour() >= 12) ? "PM" : "AM";
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d %s",
           now.year(), now.month(), now.day(),
           h12, now.minute(), now.second(), ampm);
  return String(buf);
}

void syncNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NTP] Skipped — WiFi not connected.");
    send("{\"type\":\"NTP_STATUS\",\"ok\":false,\"err\":\"WiFi not connected\"}");
    return;
  }
  Serial.println("[NTP] Sync initiated...");
  // UTC+8 (Philippine Standard Time). Primary and fallback NTP servers.
  configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
  ntpSyncPending = true;
  ntpSyncStart   = millis();
}

void ntpProcess() {
  if (!ntpSyncPending) return;

  struct tm t = {};
  if (getLocalTime(&t, 0)) {
    ntpSyncPending = false;

    char syncTs[30];
    int h12 = t.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (t.tm_hour >= 12) ? "PM" : "AM";
    snprintf(syncTs, sizeof(syncTs), "%04d-%02d-%02d %02d:%02d:%02d %s",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday,
             h12, t.tm_min, t.tm_sec, ampm);
    Serial.printf("[NTP] Synced: %s (UTC+8)\n", syncTs);

    if (rtcValid) {
      rtc.adjust(DateTime(t.tm_year+1900, t.tm_mon+1, t.tm_mday,
                          t.tm_hour, t.tm_min, t.tm_sec));
      Serial.println("[NTP] RTC updated from NTP");
    }

    StaticJsonDocument<128> ntpDoc;
    ntpDoc["type"] = "NTP_STATUS";
    ntpDoc["ok"]   = true;
    ntpDoc["ts"]   = syncTs;
    sendDoc(ntpDoc);

  } else if (millis() - ntpSyncStart > 15000) {
    ntpSyncPending = false;
    Serial.println("[NTP] Sync timed out — using existing time source");
    send("{\"type\":\"NTP_STATUS\",\"ok\":false,\"err\":\"Sync timed out\"}");
  }
}

void setManualTime(int y, int m, int d, int h, int min) {
  if (rtcValid) {
    rtc.adjust(DateTime(y, m, d, h, min, 0));
    Serial.println("[TIME] RTC manually updated.");
  }
  
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min = min;
  t.tm_sec = 0;
  time_t t_of_day = mktime(&t);
  
  struct timeval tv;
  tv.tv_sec = t_of_day;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  
  Serial.println("[TIME] System clock manually updated.");
  
  // Optionally, trigger a broadcast to the display so it immediately reflects the new time
  char syncTs[30];
  int h12 = h % 12;
  if (h12 == 0) h12 = 12;
  const char* ampm = (h >= 12) ? "PM" : "AM";
  snprintf(syncTs, sizeof(syncTs), "%04d-%02d-%02d %02d:%02d:%02d %s", y, m, d, h12, min, 0, ampm);
  StaticJsonDocument<128> doc;
  doc["type"] = "NTP_STATUS";
  doc["ok"]   = true;
  doc["ts"]   = syncTs;
  sendDoc(doc);
}
