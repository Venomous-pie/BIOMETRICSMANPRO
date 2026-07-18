/**
 * wroom_firmware.ino
 * Biometrics Employee Time-In/Time-Out System — Controller Node
 *
 * Board     : ESP32-WROOM-32
 * AS608     : UART1  RX=GPIO27, TX=GPIO26, TOUCH=GPIO34
 * DS3231    : I2C    SDA=GPIO21, SCL=GPIO22
 * CrowPanel : ESP-NOW wireless link (no UART wire)
 *
 * Libraries (install via Arduino Library Manager):
 *   - Adafruit Fingerprint Sensor Library  (Adafruit)
 *   - RTClib                               (Adafruit)
 *   - ArduinoJson                          (Benoit Blanchon)
 *
 * Source layout:
 *   src/config.h                   pin definitions, device ID, ESP-NOW constants
 *   src/employee_db.h/.cpp         employee records and slot lookup
 *   src/comms.h/.cpp               ESP-NOW transport (send, ring buffer, channel sync)
 *   src/wifi_manager.h/.cpp        WiFi connection, scan, auto-reconnect
 *   src/time_manager.h/.cpp        RTC, NTP sync, timestamp formatter
 *   src/fingerprint_manager.h/.cpp AS608 sensor, match, enroll
 *   src/activation.h/.cpp          backend API call for device registration
 *   src/command_handler.h/.cpp     command dispatcher, factory reset, fingerprint poll
 *
 * USB Serial commands (connect at 115200 baud):
 *   ENROLL:<emp_id>:<finger_index>   enroll a finger  (e.g. ENROLL:1:0)
 *   DELETE:<emp_id>:<finger_index>   erase a template  (e.g. DELETE:1:0)
 *   RESET                            reboot this board
 *   GHOST_LOGIN / NUKE_USERS / DEBUG_COMMS   dev backdoors (forwarded to CrowPanel)
 */

#include <esp_system.h>
#include "src/config.h"
#include "src/employee_db.h"
#include "src/comms.h"
#include "src/wifi_manager.h"
#include "src/time_manager.h"
#include "src/fingerprint_manager.h"
#include "src/activation.h"
#include "src/command_handler.h"

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("[BOOT] Reset reason: %d\n", esp_reset_reason());
  Serial.println("\n=== Biometrics WROOM Controller ===");

  employeeDbInit();         // parse employee JSON into empDB[]
  wifiManagerInit();        // load saved credentials, register WiFi event handler
  fingerprintManagerInit(); // start UART1 and verify the AS608 sensor
  espNowInit();             // init ESP-NOW, register CrowPanel as unicast peer
  timeManagerInit();        // start I2C and initialize the DS3231 RTC

  pinMode(PIN_FACTORY_RESET, INPUT_PULLDOWN);

  Serial.println("\nReady. Serial commands:");
  Serial.println("  ENROLL:<emp_id>:<finger_index>   e.g. ENROLL:1:0");
  Serial.println("  DELETE:<emp_id>:<finger_index>   e.g. DELETE:1:0");
  Serial.println("  RESET");

  // Notify the CrowPanel that this board has just booted. If the CrowPanel is
  // already in the activated idle state, it will re-send DEVICE_ACTIVATED so
  // the fingerprint scanner re-enables automatically without a manual reboot.
  send("{\"type\":\"WROOM_BOOT\"}");
}

// ── Loop ──────────────────────────────────────────────────────────────────────

static unsigned long lastTimeBcast = 0;

void loop() {
  handleFactoryResetButton();

  // Broadcast the current time to the CrowPanel every second.
  // sendQuiet() skips the Serial log to avoid flooding the monitor.
  if (millis() - lastTimeBcast >= 1000) {
    lastTimeBcast = millis();
    sendQuiet("{\"type\":\"TIME\",\"ts\":\"" + getTimestamp() + "\"}");
  }

  wifiProcess(); // connection timeout, scan result collection, auto-reconnect backoff
  ntpProcess();  // NTP sync completion check

  // Commands typed in the USB Serial monitor (developer / debug use).
  if (Serial.available()) {
    handleCmd(Serial.readStringUntil('\n'));
  }

  // Commands from the CrowPanel — drained from the lock-free ESP-NOW ring buffer.
  // onDataRecvFromCP() (Core 0, WiFi task) writes; this loop (Core 1) reads.
  while (!cpQueueEmpty()) {
    String msg = cpQueuePop();
    if (msg.length() > 0) {
      if (msg.indexOf("\"PING\"") == -1 && msg.indexOf("\"PONG\"") == -1)
        Serial.println("[<-CP] " + msg);
      handleCmd(msg);
    }
  }

  fingerprintPoll(); // scan for a finger touch and send MATCH/NOMATCH when found
}
