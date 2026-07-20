#pragma once
#include <Arduino.h>

// Posts the device ID and registration code to the backend API and sends the
// result back to the CrowPanel as an ACTIVATION_RESULT JSON message.
// Requires an active WiFi connection.
void validateActivationWithServer(const String &registrationCode);

// Pings the API_BASE_URL to test connectivity
void testApiConnection();

// Fetches the employee list and streams it to CrowPanel via ESP-NOW
void syncEmployeesFromServer(const String &token);
