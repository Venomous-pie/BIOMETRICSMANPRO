#pragma once
#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

extern HardwareSerial       fpSerial;
extern Adafruit_Fingerprint finger;

// Starts UART1 and verifies the AS608 sensor is connected.
// Call once from setup().
void fingerprintManagerInit();

int getL1SlotFor(int empId, int fingerIdx);
int assignL1Slot(int empId, int fingerIdx);

// Converts the captured image, searches the database, toggles IN/OUT state,
// and sends a MATCH or NOMATCH event to the CrowPanel.
// Call after finger.getImage() == FINGERPRINT_OK.
void doMatch();

// Simulates a highly-confident match for testing without hardware
void doMockMatch(int slot);

// Runs the 2-scan enrollment sequence for the given slot (1–127).
// Blocks until complete or a scan times out (15 s per scan step).
// Returns true on success, false on failure or timeout.
// Can be aborted mid-scan by calling cancelEnroll().
bool doEnroll(int slot);

// Requests an in-progress enroll to abort at the next safe polling point.
void cancelEnroll();

// True while doEnroll() is actively waiting for a finger scan.
extern volatile bool enrollCancelled;

// Reads the raw template bytes for the given slot from the AS608 sensor.
// Must be called AFTER a successful storeModel(slot) — the template is still
// in the sensor's internal buffer so no reload is required.
int getTemplateBytes(int slot, uint8_t* buf, size_t bufSize);

// Installs a 512-byte template directly to the AS608 flash at the specified slot.
// Bypasses the Adafruit library's high-level functions to send raw packets.
bool installTemplateBytes(int slot, const uint8_t* data, size_t len);

// Deletes the given slot from the AS608 and clears it from the L1 Cache.
void deleteL1Slot(int slot);
