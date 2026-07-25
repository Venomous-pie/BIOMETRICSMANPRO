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
 *   - DFRobotDFPlayerMini                  (DFRobot)
 *
 * Hardware Wiring (Audio):
 *   - DFPlayer TX   -> WROOM GPIO16 (RX2)
 *   - DFPlayer RX   -> WROOM GPIO17 (TX2)
 *   - DFPlayer VCC  -> 5V or 3.3V (depends on module/power)
 *   - DFPlayer GND  -> GND
 *   - DFPlayer SPK+ -> Speaker +
 *   - DFPlayer SPK- -> Speaker -
 *   - Buzzer +      -> WROOM GPIO13
 *   - Buzzer GND    -> GND
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
#include "src/sync_manager.h"
#include "src/audio_manager.h"

// Initialization

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("[BOOT] Reset reason: %d\n", esp_reset_reason());
  Serial.println("\n=== Biometrics WROOM Controller ===");

  employeeDbInit();          // Load employee database
  wifiManagerInit();         // Initialize WiFi
  fingerprintManagerInit();  // Initialize fingerprint sensor
  espNowInit();              // Initialize ESP-NOW
  timeManagerInit();         // Initialize RTC
  SyncManager::init();       // Initialize binary sync manager
  audioManagerInit();        // Initialize DFPlayer and Buzzer

  pinMode(PIN_FACTORY_RESET, INPUT_PULLDOWN);

  Serial.println("\nReady. Serial commands:");
  Serial.println("  ENROLL:<emp_id>:<finger_index>   e.g. ENROLL:1:0");
  Serial.println("  DELETE:<emp_id>:<finger_index>   e.g. DELETE:1:0");
  Serial.println("  RESET");

  // Notify CrowPanel of boot to sync state
  send("{\"type\":\"WROOM_BOOT\"}");
}

// Main Loop

static unsigned long lastTimeBcast = 0;

void loop() {
  handleFactoryResetButton();

  // Broadcast time to CrowPanel every second
  if (millis() - lastTimeBcast >= 1000) {
    lastTimeBcast = millis();
    sendQuiet("{\"type\":\"TIME\",\"ts\":\"" + getTimestamp() + "\"}");
  }

  wifiProcess();  // Handle WiFi background tasks
  ntpProcess();   // Check NTP sync status
  SyncManager::loop(); // Drive background sync machine

  // Dispatch buffered binary sync packets (queued from the RX callback)
  // Must be called BEFORE fingerprintPoll() so ACKs go out promptly.
  syncQueueProcess();

  // Handle Serial commands
  if (Serial.available()) {
    handleCmd(Serial.readStringUntil('\n'));
  }

  // Process incoming ESP-NOW messages
  while (!cpQueueEmpty()) {
    String msg = cpQueuePop();
    if (msg.length() > 0) {
      if (msg.indexOf("\"PING\"") == -1 && msg.indexOf("\"PONG\"") == -1)
        Serial.println("[<-CP] " + msg);
      handleCmd(msg);
    }
  }

  fingerprintPoll();  // Scan for fingerprints
}
