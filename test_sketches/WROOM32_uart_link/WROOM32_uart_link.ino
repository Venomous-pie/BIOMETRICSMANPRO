/*
  ESP32-WROOM-32  <-->  CrowPanel 5.0" (ESP32-S3)
  Bidirectional UART link - WROOM-32 side

  Wiring:
    WROOM-32 GPIO33 (TX) -> CrowPanel IO38 (RX)
    WROOM-32 GPIO32 (RX) <- CrowPanel IO43 (TX)
    WROOM-32 GND          -- CrowPanel GND
*/

#include <Arduino.h>

#define LINK_RX 32    // WROOM-32 RX2 (connected to CP IO43)
#define LINK_TX 33    // WROOM-32 TX2 (connected to CP IO38)
#define LINK_BAUD 115200

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 2000;
int pingCount = 0;
int pongCount = 0;

void setup() {
  Serial.begin(115200);
  // Initialize Serial2 for communication with CrowPanel
  Serial2.begin(LINK_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);

  Serial.println("\r\n=== WROOM PING-PONG TEST ===");
  Serial.printf("UART2 to CrowPanel initialized at %d baud (RX=%d, TX=%d).\n", LINK_BAUD, LINK_RX, LINK_TX);
}

void loop() {
  // Send PING every 2 seconds
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    pingCount++;
    Serial2.print("{\"type\":\"PING\"}\r\n");
    Serial.printf("[WROOM] Sent PING #%d (awaiting PONG)\n", pingCount);
  }

  // Listen for replies coming back from the CrowPanel
  if (Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) {
      Serial.print("<- received from CrowPanel: ");
      Serial.println(msg);

      if (msg.indexOf("\"PONG\"") != -1) {
        pongCount++;
        Serial.printf("[WROOM] -> Valid PONG matched! (total pongs=%d)\n", pongCount);
      }
    }
  }
}
