#pragma once
#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

extern HardwareSerial       fpSerial;
extern Adafruit_Fingerprint finger;

// Starts UART1 and verifies the AS608 sensor is connected.
// Call once from setup().
void fingerprintManagerInit();

// Converts the captured image, searches the database, toggles IN/OUT state,
// and sends a MATCH or NOMATCH event to the CrowPanel.
// Call after finger.getImage() == FINGERPRINT_OK.
void doMatch();

// Runs the 2-scan enrollment sequence for the given slot (1–127).
// Blocks until complete or a scan times out (15 s per scan step).
// Returns true on success, false on failure or timeout.
bool doEnroll(int slot);
