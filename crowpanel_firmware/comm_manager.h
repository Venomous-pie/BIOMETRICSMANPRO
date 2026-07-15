#ifndef COMM_MANAGER_H
#define COMM_MANAGER_H

#include <Arduino.h>
#include <esp_now.h>

// ============================================================
// ESP-NOW configuration  (Option A — fixed channel)
// Must match ESPNOW_CHANNEL and WROOM_MAC on the WROOM side.
// ============================================================
#define ESPNOW_CHANNEL 1   // Must match your router's fixed channel

// The WROOM's station MAC address.
// PLACEHOLDER: Flash the WROOM first and read its "[BOOT] WROOM MAC:" line,
// then paste those 6 hex bytes here before flashing the CrowPanel.
static const uint8_t WROOM_MAC[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};

class CommManager {
public:
    static void begin();    // Inits ESP-NOW, registers callbacks, adds WROOM as peer
    static void process();  // Call from loop() — drains ring buffer, dispatches JSON
    static void sendCommand(const String& cmd);

    // ESP-NOW receive callback — runs in WiFi task (Core 0).
    // Copies payload into the ring buffer; all parsing is done in process().
    static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len);

private:
    static void dispatchJson(const String& line);
    static String serialBuf; // USB Serial forwarder accumulator
};

#endif // COMM_MANAGER_H
