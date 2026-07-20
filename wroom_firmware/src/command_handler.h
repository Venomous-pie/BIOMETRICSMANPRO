#pragma once
#include <Arduino.h>

// ── System state flags ────────────────────────────────────────────────────────
// Shared between the command handler and the main loop.

// Set to true when the CrowPanel sends DEVICE_ACTIVATED.
// The fingerprint scanner is disabled until this flag is set.
extern bool activated;

// Set to true while a doEnroll() sequence is in progress.
// Prevents the normal match loop from running during enrollment.
extern bool enrolling;

// Set to true when the CrowPanel is showing the idle/ready screen.
// Fingerprint scanning only runs when this flag is true.
extern bool idle_screen_active;

// ── Entry points ──────────────────────────────────────────────────────────────

// Dispatches a command string from USB Serial or the CrowPanel.
// Handles: RESET, ENROLL:, DELETE:, backdoor commands, and all JSON commands.
void handleCmd(String cmd);

// Polls the hardware factory reset button. Triggers a full wipe and reboot
// if the button is held for 5 seconds. Call every loop() iteration.
void handleFactoryResetButton();

// Checks if 1 hour has elapsed and triggers a background employee sync.
// Call every loop() iteration.
void autoSyncEmployees();

// Polls the fingerprint sensor when the device is activated and the idle
// screen is active. Sends MATCH or NOMATCH to the CrowPanel on a touch event.
// Call every loop() iteration.
void fingerprintPoll();
