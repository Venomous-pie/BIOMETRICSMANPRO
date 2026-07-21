# Biometrics Time-In/Out System

An ESP32-based biometric attendance system. A fingerprint scanner node (WROOM) communicates wirelessly via ESP-NOW to a touchscreen display node (CrowPanel), which connects to the backend over WiFi.

---

## Architecture Overview

Understanding the data flow before touching code will save you hours. The system has three tiers:

`
+----------------------------------+        +----------------------------------+
|      WROOM Node (ESP32-WROOM)    |        |   CrowPanel Node (ESP32-S3)      |
|                                  |        |                                  |
|  AS608 Fingerprint Sensor        |        |  5 800x480 Touchscreen (LVGL) |
| DS3231 RTC | | Employee list and logs UI |
| WiFi (for NTP + backend calls) | | WiFi (for backend sync) |
| | | |
| Polls finger -> match -> sends |<------>| Receives match/enroll events |
| attendance event over ESP-NOW |ESP-NOW | Shows result screen |
| | | |
| Handles: activation, NTP sync, | | Handles: employee list sync, |
| fingerprint enroll/delete, | | attendance log display, |
| factory reset | | WiFi setup UI, settings |
+---+------------------------------+ +---+------------------------------+
 | HTTPS (REST API) | HTTPS (REST API)
 +------------------------------+------------+
 |
 +-----------v-----------+
 | ManPro Backend API |
 | (demo.manpromanage |
 | ment.com or local |
 | mock_server/) |
 +-----------------------+
`

**How a time-in works, end to end:**
1. Employee places finger on the AS608 sensor on the WROOM.
2. WROOM matches the fingerprint to a stored template and resolves the employee ID.
3. WROOM sends a {type:MATCH, emp_id:..., ts:...} JSON packet to the CrowPanel via ESP-NOW.
4. CrowPanel displays the result screen (welcome / access denied) and logs the attendance locally.
5. CrowPanel uploads the log entry to the backend API over WiFi on the next sync cycle.

**ESP-NOW channel management:** Both nodes must use the same WiFi channel. The WROOM monitors the channel continuously; if the AP hops channels, the WROOM sends a CHANNEL_HOP notification so the CrowPanel resyncs without a reboot.

---

## Hardware

| Node | Board | Role |
|---|---|---|
| CrowPanel | ESP32-S3 (5 800x480) | Touchscreen UI, WiFi, backend comms |
| WROOM | ESP32-WROOM-32 | Fingerprint scanner, RTC, ESP-NOW bridge |

---

## Power Requirements

### Supply Voltages

| Component | Voltage | Typical Current | Peak Current |
|---|---|---|---|
| ESP32-WROOM-32 (module) | 3.3 V | ~80 mA idle | ~350 mA WiFi TX burst |
| CrowPanel ESP32-S3 board | 5 V (USB-C) or 3.3 V regulated | ~150 mA idle | ~500 mA with LCD backlight + WiFi |
| AS608 Fingerprint Sensor | 3.3 V | ~120 mA scanning | ~140 mA |
| DS3231 RTC module | 3.3 V | < 1 mA (uses onboard coin cell for timekeeping) | |

### Power Strategy

**During development:**
- Power the **CrowPanel** via its USB-C port from a PC or USB charger. The board has an onboard 3.3 V LDO — you do not need a separate regulator.
- Power the **WROOM dev board** via its USB-micro/USB-C port. Most WROOM dev boards also have a 3.3 V LDO.
- The **AS608** and **DS3231** can both be powered from the WROOM board's 3.3 V pin (labelled 3V3). They share the same supply without issue — their combined draw (~120-141 mA) is within the AMS1117 LDO's 800 mA rating.

**In the final enclosure:**
- The WROOM + AS608 + DS3231 sub-assembly should be powered from a regulated **5 V 1 A** supply (e.g. USB wall adapter) feeding the WROOM dev board's USB connector, or via VIN if using a bare module with an external 3.3 V regulator.
- The CrowPanel should be powered independently via its USB-C connector from a **5 V 2 A** source. Its LCD backlight alone draws ~200 mA.
- **Do not share a USB port between the two boards** during development — each board needs its own Serial port for log monitoring and flashing.

### Wire Gauge
- For power rails: 26 AWG stranded is sufficient for the current levels involved.
- For I2C/UART signal lines: 28 AWG or 30 AWG.
- Keep wire runs under 30 cm to avoid voltage drop on the 3.3 V rail.

---

## Full Wiring & Pinout

### WROOM — Complete Pin Map

| Pin | GPIO | Function | Notes |
|---|---|---|---|
| 3V3 | — | 3.3 V output | Powers AS608 and DS3231 |
| GND | — | Ground | Common ground for all peripherals |
| **UART1 RX** | **GPIO27** | AS608 TX -> | Connect to AS608 TXD pin |
| **UART1 TX** | **GPIO26** | AS608 RX <- | Connect to AS608 RXD pin |
| **GPIO34** | Input-only | AS608 T-OUT (touch detect) | Pulled HIGH by sensor when finger present |
| **GPIO21** | SDA | DS3231 I2C Data | 4.7k pull-up to 3.3 V (often built into module) |
| **GPIO22** | SCL | DS3231 I2C Clock | 4.7k pull-up to 3.3 V (often built into module) |
| **GPIO14** | Factory Reset | Active HIGH button | Uses internal pull-down; hold 5 s to wipe |

**Pins to avoid on WROOM-32:**

| GPIO | Reason |
|---|---|
| GPIO0 | Strapping pin — LOW at boot = download mode; do not pull low with external hardware |
| GPIO2 | Strapping pin — must be LOW during flash; avoid driving it during boot |
| GPIO12 | Strapping pin — sets flash voltage; keep floating or LOW |
| GPIO15 | Strapping pin — controls boot logging; keep floating for normal use |
| GPIO34-39 | Input-only — no internal pull-up/pull-down; cannot be driven as outputs |
| GPIO6-11 | Connected to internal SPI flash — do not use |

**Connector type:** JST-PH 2.0 mm 2-pin or standard 2.54 mm DuPont headers are both fine for a prototype enclosure.

---

### AS608 Fingerprint Sensor Wiring

| AS608 Pin | Connects to |
|---|---|
| VCC | WROOM 3V3 |
| GND | WROOM GND |
| TXD | WROOM GPIO27 (UART1 RX) |
| RXD | WROOM GPIO26 (UART1 TX) |
| T-OUT | WROOM GPIO34 |
| WAKEUP | Leave floating or tie to GND |

> The AS608 communicates at 57600 baud by default. The firmware initialises UART1 at this speed. Do not change it unless you also update ingerprint_manager.cpp.

---

### DS3231 RTC Module Wiring

| DS3231 Pin | Connects to |
|---|---|
| VCC | WROOM 3V3 |
| GND | WROOM GND |
| SDA | WROOM GPIO21 |
| SCL | WROOM GPIO22 |
| SQW | Not connected |
| 32K | Not connected |

> Install a CR2032 coin cell in the DS3231 module's battery holder. Without it, the RTC loses time on power cycle and the firmware falls back to NTP sync (which works, but adds a brief delay after boot).

---

### CrowPanel — Key Pins

The CrowPanel 5 board is a self-contained unit. You do not need to wire any peripherals to it — the display, touch controller, and SD card are all onboard. The only external connection is USB-C for power and flashing.

| Function | Notes |
|---|---|
| USB-C | Power + Serial (CDC) for flashing and Serial Monitor |
| ESP-NOW radio | Uses onboard antenna; no external connection needed |
| WiFi | Uses onboard antenna |

**Pins to avoid on ESP32-S3 (if you ever need to expand):**

| GPIO | Reason |
|---|---|
| GPIO0 | Strapping pin — boot mode selection |
| GPIO3 | Strapping pin |
| GPIO19, GPIO20 | USB D+/D- — used by CDC USB port |
| GPIO26-32 | Connected to OPI PSRAM — do not use |
| GPIO33-37 | Connected to onboard flash — do not use |

---

## Arduino IDE Setup

**Version:** Arduino IDE 2.3.10

> Since the IDE's Tools menu settings are **per-board-selection** and do not persist per-sketch, always double-check the **Board** dropdown before flashing. Flashing the wrong sketch to the wrong board (or with the wrong partition scheme) will produce a silent bad build.

### Step 1 — Install Arduino IDE

Download the installer from https://www.arduino.cc/en/software — choose **Arduino IDE 2.3.10** (Windows Installer). Run with default options.

### Step 2 — Add the ESP32 Board Package

1. Open Arduino IDE -> **File -> Preferences**.
2. In **Additional boards manager URLs**, paste:
 https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
3. Click **OK**.
4. Go to **Tools -> Board -> Boards Manager**, search for esp32, and install **esp32 by Espressif Systems**.
 - **Required version: 3.2.0** (or the latest 3.x release). Version 2.x is not compatible with the ESP32-S3 PSRAM driver used by the CrowPanel.

### Step 3 — Install Libraries

Open **Tools -> Manage Libraries** and install each of the following by searching the Library Manager:

**For CrowPanel:**

| Search Term | Library to Install | Pin Version To |
|---|---|---|
| Adafruit BusIO | Adafruit BusIO | 1.17.4 |
| DFRobot DFPlayer | DFRobotDFPlayerMini | 1.0.6 |
| LovyanGFX | LovyanGFX (by lovyan03) | 1.2.25 |
| RTClib | RTClib (by Adafruit) | 2.1.4 |
| lvgl | lvgl (by kisvegabor) | **8.3.11** |

> ⚠️ **LVGL is pinned to 8.3.11.** Version 9.x contains breaking API changes affecting lv_obj_t styling and display driver registration. Do **not** allow Library Manager to auto-update this library.

**For WROOM:**

| Search Term | Library to Install | Version |
|---|---|---|
| Adafruit BusIO | Adafruit BusIO | 1.17.4 |
| Adafruit Fingerprint | Adafruit Fingerprint Sensor Library | 2.1.4 |
| ArduinoJson | ArduinoJson (by Benoit Blanchon) | 7.4.3 |
| RTClib | RTClib (by Adafruit) | 2.1.4 |

The following are used directly but bundled with the ESP32 board package — no separate install needed: WiFi, WiFiUdp, HTTPClient, esp_now, esp_system, esp_wifi, Preferences, Wire, ime.h.

---

### 1. CrowPanel — ESP32-S3

#### Board Settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| CPU Frequency | 240MHz (WiFi) |
| Core Debug Level | None |
| USB DFU On Boot | Disabled |
| Erase All Flash Before Sketch Upload | Disabled |
| Events Run On | Core 1 |
| Flash Mode | QIO 80MHz |
| Flash Size | 4MB (32Mb) |
| JTAG Adapter | Disabled |
| Arduino Runs On | Core 1 |
| USB Firmware MSC On Boot | Disabled |
| **Partition Scheme** | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| **PSRAM** | **OPI PSRAM** |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | 921600 |
| USB Mode | Hardware CDC and JTAG |
| Zigbee Mode | Disabled |

#### Required Libraries

| Library | Author | Version |
|---|---|---|
| Adafruit BusIO | Adafruit | 1.17.4 |
| DFRobotDFPlayerMini | DFRobot | 1.0.6 |
| LovyanGFX | lovyan03 | 1.2.25 |
| RTClib | Adafruit | 2.1.4 |
| lvgl | kisvegabor | **8.3.11** |

> ⚠️ **LVGL is pinned to 8.3.11.** Version 9.x contains breaking API changes affecting lv_obj_t styling and display driver registration. Do **not** allow Library Manager to auto-update this library.

---

### 2. WROOM Controller — ESP32

#### Board Settings

| Setting | Value |
|---|---|
| Board | ESP32 Dev Module |
| CPU Frequency | 240MHz (WiFi/BT) |
| Core Debug Level | None |
| **Erase All Flash Before Sketch Upload** | **Enabled** |
| Events Run On | Core 1 |
| Flash Frequency | 80MHz |
| Flash Mode | QIO |
| Flash Size | 4MB (32Mb) |
| JTAG Adapter | Disabled |
| Arduino Runs On | Core 1 |
| **Partition Scheme** | **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)** |
| **PSRAM** | **Disabled** |
| Upload Speed | 921600 |
| Zigbee Mode | Disabled |

#### Required Libraries

| Library | Author | Version |
|---|---|---|
| Adafruit BusIO | Adafruit | 1.17.4 |
| Adafruit Fingerprint Sensor Library | Adafruit | 2.1.4 |
| ArduinoJson | Benoit Blanchon | 7.4.3 |
| RTClib | Adafruit | 2.1.4 |

The following are used directly but bundled with the ESP32 board package — no separate install needed: WiFi, WiFiUdp, HTTPClient, esp_now, esp_system, esp_wifi, Preferences, Wire, ime.h.

---

## Configuration Reference

All runtime-tunable values are defined in two files. Edit these before flashing to match your environment.

### wroom_firmware/src/config.h

| Constant | Default | What it controls |
|---|---|---|
| DEVICE_ID | P001-2607-6AEC-Z2GD | Unique device identifier sent to backend during activation. Must match DEVICE_ID_HARDCODED in data_manager.h. |
| API_BASE_URL | https://demo.manpromanagement.com | Backend base URL. Change to http://192.168.x.x:8000 for local mock server. |
| ESPNOW_CHANNEL | 1 | Fixed WiFi channel for ESP-NOW. Your router must be locked to this channel. |
| ESPNOW_PAYLOAD_MAX | 251 | Maximum ESP-NOW packet size in bytes. Do not increase beyond 250. |
| PIN_FP_RX | 27 | UART1 RX — receives data from AS608 TXD pin. |
| PIN_FP_TX | 26 | UART1 TX — sends data to AS608 RXD pin. |
| PIN_FP_TOUCH | 34 | Input from AS608 T-OUT (finger present detect). |
| PIN_FACTORY_RESET | 14 | Active-HIGH button. Hold for 5 s to factory reset. |
| MAX_SLOTS | 127 | AS608 fingerprint template slot limit. Do not increase. |

### wroom_firmware/src/comms.cpp

| Variable | Default | What it controls |
|---|---|---|
| CROWPANEL_MAC[6] | {0x30, 0xED, ...} | Station MAC address of the CrowPanel. **You must update this** before flashing the WROOM (see First-Time Flash Order). |

### crowpanel_firmware/src/core/data_manager.h

| Constant | Default | What it controls |
|---|---|---|
| DEVICE_ID_HARDCODED | P001-2607-6AEC-Z2GD | Must match DEVICE_ID in WROOM config.h. Both boards send this to the backend to prove they belong to the same registered unit. |

### Time / NTP (wroom_firmware/src/time_manager.cpp)

| Value | Location | Default | What it controls |
|---|---|---|---|
| UTC offset | configTime(8 * 3600, 0, ...) | 8 * 3600 (UTC+8 / PST) | Change the multiplier for your timezone, e.g. 7 * 3600 for ICT (UTC+7). |
| Primary NTP server | second arg to configTime | pool.ntp.org | Standard global NTP pool. |
| Fallback NTP server | third arg to configTime | ime.google.com | Used if the primary is unreachable. |

### WiFi Credentials

WiFi credentials are **not** hardcoded — they are entered at runtime via the CrowPanel Settings -> WiFi screen and stored in LittleFS. There is no file to edit for WiFi; just use the on-device UI.

---

## First-Time Flash Order — Full Walkthrough

> Always flash the CrowPanel first. The WROOM needs the CrowPanel's MAC address baked in before it can pair.

### Step 1 — Flash CrowPanel and get its MAC

1. Open crowpanel_firmware/crowpanel_firmware.ino in Arduino IDE.
2. Select **Tools -> Board -> ESP32S3 Dev Module** and apply all settings from the CrowPanel board settings table above.
3. Connect the CrowPanel via USB-C. Select its port under **Tools -> Port**.
4. Click **Upload** (arrow button). Wait for Done uploading.
5. Open **Tools -> Serial Monitor**. Set baud rate to **115200** and line ending to **Newline**.
6. Press the reset button on the CrowPanel (or power-cycle it).
7. Wait for the boot log. Within ~3 seconds you should see:
 `
 [BOOT] CP MAC: 30:ED:A0:31:70:EC
 [DISPLAY] Init OK
 [LVGL] Ready
 `
8. **Copy the full MAC address** from the [BOOT] CP MAC: line.

> **If nothing appears in Serial Monitor:** Make sure USB CDC On Boot is set to **Enabled** in board settings, and that you have selected the correct COM port. On Windows, open Device Manager and look for USB Serial Device (COMx) under Ports.

---

### Step 2 — Update WROOM firmware with CrowPanel MAC

1. Open wroom_firmware/src/comms.cpp.
2. Find the line:
 `cpp
 uint8_t CROWPANEL_MAC[6] = {0x30, 0xED, 0xA0, 0x31, 0x70, 0xEC};
 `
3. Replace the 6 hex bytes with the MAC you copied in Step 1. For example, if the MAC was A4:CF:12:AB:CD:EF:
 `cpp
 uint8_t CROWPANEL_MAC[6] = {0xA4, 0xCF, 0x12, 0xAB, 0xCD, 0xEF};
 `
4. Save the file.

---

### Step 3 — Flash WROOM

1. Open wroom_firmware/wroom_firmware.ino in Arduino IDE.
2. Select **Tools -> Board -> ESP32 Dev Module** and apply all settings from the WROOM board settings table.
3. Connect the WROOM via USB. Select its port under **Tools -> Port**.
4. Click **Upload**. Because Erase All Flash Before Sketch Upload is enabled, this takes ~30 seconds on the first flash.
5. Open **Tools -> Serial Monitor** at **115200 baud**.
6. Press reset on the WROOM. Expected boot sequence:
 `
 === Biometrics WROOM Controller ===
 [BOOT] Reset reason: 1
 [WIFI] Attempting to connect to saved network...
 [AS608] Found! Templates stored: 3
 [RTC] Ready: 2025-07-21 14:03:22
 [ESPNOW] Peer registered: 30:ed:a0:31:70:ec
 Ready. Serial commands:
 ENROLL:<emp_id>:<finger_index> e.g. ENROLL:1:0
 DELETE:<emp_id>:<finger_index> e.g. DELETE:1:0
 RESET
 `
7. On the CrowPanel, you should briefly see a WROOM online toast confirming the link is live.

---

### Step 4 — WiFi Setup and Activation

1. On the CrowPanel, navigate to **Settings -> WiFi**.
2. Scan for networks, select your AP, and enter the password using the on-screen keyboard.
3. Once connected, navigate to **Settings -> Activation** and enter your ManPro registration code.
4. The WROOM calls the backend API to validate, and both boards mark themselves as activated.

---

### Pairing Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| [ESPNOW] Send fail repeated in WROOM Serial | Wrong MAC address in comms.cpp | Re-read CrowPanel MAC and update comms.cpp. |
| No WROOM online toast on CrowPanel | Channel mismatch | Lock router to a fixed channel and match ESPNOW_CHANNEL in config.h. |
| [AS608] NOT FOUND in WROOM Serial | Wiring error or wrong baud | Check GPIO26/27 connections. Swap RX/TX if reversed. |
| [RTC] NOT FOUND in WROOM Serial | DS3231 not wired or I2C conflict | Verify GPIO21/22 and that 3.3 V is connected to the RTC module. |
| CrowPanel shows blank/white screen after flash | Wrong partition or PSRAM setting | Re-check: Partition = Huge APP, PSRAM = OPI PSRAM. |

---

## Mock Server Usage

The mock_server/ directory contains a lightweight Python server that mimics the ManPro backend API. Use it during development when you do not have access to the live server.

### Prerequisites

- Python 3.9 or later
- pip install flask

### Starting the Mock Server

`ash
cd mock_server
pip install -r requirements.txt
python mock_server.py
`

The server starts on http://0.0.0.0:8000 by default and prints available endpoints on startup.

### Pointing the Firmware at the Mock Server

1. Find your development machine's LAN IP address (e.g. 192.168.1.50).
2. Open wroom_firmware/src/config.h and change:
 `cpp
 #define API_BASE_URL http://192.168.1.50:8000
 `
3. Ensure the WROOM and your development machine are on the same WiFi network, then flash.

> The mock server returns canned responses for all endpoints. Check its console output to see every request the firmware makes — this is useful for debugging sync issues.

---

## WROOM Serial Commands

Connect at **115200 baud**.

| Command | Description |
|---|---|
| ENROLL:<emp_id>:<finger_index> | Enroll a finger (e.g. ENROLL:1:0 = employee 1, first finger) |
| DELETE:<emp_id>:<finger_index> | Erase a stored template (e.g. DELETE:1:0) |
| RESET | Reboot the WROOM |
| GHOST_LOGIN | Dev backdoor — bypasses scanner, jumps to Main Menu on CrowPanel |
| NUKE_USERS | Dev backdoor — erases all stored fingerprints except slot 1 |
| DEBUG_COMMS | Dev backdoor — toggles ESP-NOW ping/pong debug output |

---

## Project Structure

`
BIOMETRICSMANPRO/
+-- crowpanel_firmware/
| +-- crowpanel_firmware.ino Entry point (setup + loop)
| +-- src/
| +-- core/ comm_manager, data_manager, display_driver
| +-- ui/ All LVGL screens (idle, enroll, logs, settings)
| +-- splash/ Boot animation (manpro_splash + manpro_logo)
| +-- assets/ Compiled LVGL icon and font data (.c files)
+-- wroom_firmware/
| +-- wroom_firmware.ino Entry point (setup + loop)
| +-- src/
| +-- config.h Pin definitions, device ID, ESP-NOW constants
| +-- employee_db.h/.cpp Employee records and slot lookup
| +-- comms.h/.cpp ESP-NOW transport, ring buffer, channel sync
| +-- wifi_manager.h/.cpp WiFi connection, scan, auto-reconnect
| +-- time_manager.h/.cpp RTC, NTP sync, timestamp formatter
| +-- fingerprint_manager.h/.cpp AS608 sensor, match, enroll
| +-- activation.h/.cpp Backend API call for device registration
| +-- command_handler.h/.cpp Command dispatcher, factory reset, fingerprint poll
+-- assets/
| +-- logo/ ManPro logo and banner
| +-- icons/ Source icon PNGs (icon_*.png)
| +-- boot_anim/ Boot animation source and GIF reference
+-- mock_server/ Development mock server (Python)
+-- wroom_firmware/tools/ Image conversion scripts
+-- UI_REFERENCE/ Color palette and design references
`

---

## Design Notes

**Partition schemes differ intentionally.** The CrowPanel uses *Huge APP (3MB)* because LVGL + LovyanGFX + compiled icon assets consume significantly more flash than a typical sketch. OTA is not used on that board. The WROOM uses the standard *Default 4MB with spiffs* scheme.

**PSRAM is enabled only on the CrowPanel.** LVGL's double display buffers (~160KB each) are allocated from PSRAM at boot. The WROOM-32 module does not have PSRAM populated.

**ESP-NOW channel management.** Both boards must be on the same WiFi channel for ESP-NOW to work. The WROOM monitors the channel in real time and sends CHANNEL_HOP notifications to the CrowPanel whenever the AP channel changes, so they stay in sync without a reboot.

**Time source priority.** The WROOM uses a three-tier clock fallback: NTP (most accurate, requires WiFi) -> DS3231 hardware RTC (battery-backed, survives power loss) -> compile-time + elapsed millis (last resort, drifts). NTP automatically updates the DS3231 whenever a sync succeeds.

**Employee data lives on the CrowPanel.** The CrowPanel stores the employee list in LittleFS and syncs it from the backend. The WROOM only stores fingerprint template slot-to-employee-ID mappings in its own flash via the employee_db module. A sync operation pushes the mapping table from CrowPanel to WROOM over ESP-NOW using a binary framed protocol (sync_protocol.h).
