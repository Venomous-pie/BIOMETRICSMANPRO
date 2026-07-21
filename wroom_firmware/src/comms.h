#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// CrowPanel station MAC address.
// Flash the CrowPanel first, read its "[BOOT] CP MAC:" line from Serial,
// then paste those 6 hex bytes here before flashing the WROOM.
extern uint8_t CROWPANEL_MAC[6];

// The WiFi channel the radio is currently locked to.
// Updated whenever the AP channel changes so ESP-NOW stays in sync with
// wherever the CrowPanel is listening.
extern uint8_t lastKnownChannel;

// ── ESP-NOW Transport ─────────────────────────────────────────────────────────

// Sends a JSON string via ESP-NOW and logs it to Serial.
// Use for all important one-shot events (match, enroll results, status changes).
void send(const String &json);

// Sends a JSON string via ESP-NOW without a Serial log entry.
// Use for high-frequency broadcasts (e.g. TIME) that would otherwise
// flood the Serial monitor and make it hard to read.
void sendQuiet(const String &json);

// Serializes a JsonDocument to a string and calls send().
void sendDoc(JsonDocument &doc);

// Sends a raw binary packet for the sync protocol.
void sendSyncPacket(const uint8_t* payload, size_t len);

// ── Channel Sync ──────────────────────────────────────────────────────────────

// Checks whether the radio channel has drifted from lastKnownChannel.
// If it has, notifies the CrowPanel to hop to the new channel before switching,
// then re-registers the ESP-NOW peer on the correct channel.
// Call with force=true after any WiFi operation that may have changed the channel
// (e.g. scan, failed connect attempt).
void resyncEspNow(bool force = false);

// ── Initialization ────────────────────────────────────────────────────────────

// Initializes ESP-NOW in STA mode, registers send/receive callbacks, and adds
// the CrowPanel as a unicast peer. Safe to call multiple times — handles
// re-init after a full WiFi radio teardown.
void espNowInit();

// ── CrowPanel Inbound Queue ───────────────────────────────────────────────────
// ESP-NOW messages from the CrowPanel are pushed here by the WiFi-task receive
// callback (Core 0) and drained by loop() (Core 1) using a lock-free ring buffer.

// Returns true when the queue has no pending messages.
bool   cpQueueEmpty();

// Removes and returns the next message from the queue.
// Returns an empty string if the queue is empty.
String cpQueuePop();
