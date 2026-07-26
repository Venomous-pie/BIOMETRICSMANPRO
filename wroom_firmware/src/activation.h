#pragma once
#include <Arduino.h>

// Posts the device ID and registration code to the backend API and sends the
// result back to the CrowPanel as an ACTIVATION_RESULT JSON message.
// Requires an active WiFi connection.
void validateActivationWithServer(const String &registrationCode);

// Pings the API_BASE_URL to test connectivity
void testApiConnection();

// Posts a completed fingerprint enrollment to the server.
// Call after doEnroll() succeeds, before returning ENROLL_OK to CrowPanel.
// Returns true if the server accepted it, false otherwise, with outError populated.
bool uploadEnrollment(const String& deviceToken, const String& empName,
                      int fingerIndex, int slot,
                      const uint8_t* templateBytes, int templateLen,
                      String& outError);
