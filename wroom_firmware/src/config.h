#pragma once

// ── Fingerprint Sensor (AS608) ────────────────────────────────────────────────
#define PIN_FP_RX    27   // AS608 TX  → WROOM UART1 RX
#define PIN_FP_TX    26   // AS608 RX  ← WROOM UART1 TX
#define PIN_FP_TOUCH 34   // AS608 T-OUT: pulled HIGH when a finger is present
#define MAX_SLOTS    127  // Maximum AS608 template slots (sensor-rated capacity)

// Hardware factory reset button (GPIO14, active HIGH via internal pull-down).
// Hold for 5 seconds to wipe WiFi credentials and all fingerprint templates.
#define PIN_FACTORY_RESET 14

// ── Device Identity ───────────────────────────────────────────────────────────
// Sent to the backend to identify and verify this unit's registration.
// Must match DEVICE_ID_HARDCODED in the CrowPanel firmware.
#define DEVICE_ID    "P001-2607-6AEC-Z2GD"

// Backend API base URL.
// Change to your server's LAN IP (e.g. http://192.168.1.50:8000) for local dev.
#define API_BASE_URL "https://demo.manpromanagement.com"

// ── ESP-NOW ───────────────────────────────────────────────────────────────────
// The router must be locked to a fixed channel (e.g. 1).
// Both WROOM and CrowPanel must use the same value.
#define ESPNOW_CHANNEL     1
#define ESPNOW_PAYLOAD_MAX 251
