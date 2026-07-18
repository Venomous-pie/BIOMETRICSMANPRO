#pragma once
#include <Arduino.h>

// Posts the device ID and registration code to the backend API and sends the
// result back to the CrowPanel as an ACTIVATION_RESULT JSON message.
// Requires an active WiFi connection.
void validateActivationWithServer(const String &registrationCode);
