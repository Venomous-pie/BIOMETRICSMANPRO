#pragma once

// ── Development / Testing ─────────────────────────────────────────────────────
// Uncomment to simulate fingerprint scans and bypass the physical AS608 hardware.
// #define MOCK_SENSOR 1  // Uncomment ONLY for hardware-less dev testing

// ── Fingerprint Sensor (AS608) ────────────────────────────────────────────────
#define PIN_FP_RX    27   // AS608 TX  → WROOM UART1 RX
#define PIN_FP_TX    26   // AS608 RX  ← WROOM UART1 TX
#define PIN_FP_TOUCH 34   // AS608 T-OUT: pulled HIGH when a finger is present
#define MAX_SLOTS    127  // Maximum AS608 template slots (sensor-rated capacity)

// ── Audio (DFPlayer Mini & Buzzer) ────────────────────────────────────────────
#define PIN_DFP_RX   16   // DFPlayer TX → WROOM UART2 RX
#define PIN_DFP_TX   17   // DFPlayer RX ← WROOM UART2 TX
#define PIN_BUZZER   13   // Active Buzzer GPIO

// MP3 Folder Track Numbers
#define TRACK_TIME_IN         1
#define TRACK_TIME_OUT        2
#define TRACK_NOT_RECOGNIZED  3
#define TRACK_ALREADY_LOGGED  4
#define TRACK_ENROLLED        5
#define TRACK_ENROLL_FAILED   6
// TRACK_RESET_CONFIRM 7 is skipped as per user request (buzzer instead)

#define PIN_FACTORY_RESET 14

// ── Device Identity ───────────────────────────────────────────────────────────
// Sent to the backend to identify and verify this unit's registration.
// Must match DEVICE_ID_HARDCODED in the CrowPanel firmware.
#define DEVICE_ID    "F001-2608-6AEC-ON92"

// Backend API base URL.
// Change to your server's LAN IP (e.g. http://192.168.1.50:8000) for local dev.
#define API_BASE_URL "https://demo.manpromanagement.com"

// ── ESP-NOW ───────────────────────────────────────────────────────────────────
// The router must be locked to a fixed channel (e.g. 1).
// Both WROOM and CrowPanel must use the same value.
#define ESPNOW_CHANNEL     1
#define ESPNOW_PAYLOAD_MAX 251
