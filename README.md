# Biometrics Time-In/Out System

An ESP32-based biometric attendance system. A fingerprint scanner node (WROOM) communicates wirelessly via ESP-NOW to a touchscreen display node (CrowPanel), which connects to the backend over WiFi.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Hardware](#2-hardware)
3. [Power Requirements](#3-power-requirements)
4. [Wiring & Pinout](#4-wiring--pinout)
5. [Arduino IDE Setup](#5-arduino-ide-setup)
6. [Configuration Reference](#6-configuration-reference)
7. [First-Time Flash Order](#7-first-time-flash-order)
8. [Mock Server Usage](#8-mock-server-usage)
9. [Repository Layout](#9-repository-layout)
10. [Employee Sync Pipeline](#10-employee-sync-pipeline)
11. [ESP-NOW Message Types](#11-esp-now-message-types)
12. [Persistent Storage Map](#12-persistent-storage-map)
13. [Subsystem Deep-Dives](#13-subsystem-deep-dives)
14. [Development Workflow](#14-development-workflow)
15. [WROOM Serial Commands](#15-wroom-serial-commands)
16. [Testing Checklist](#16-testing-checklist)
17. [Backend API Endpoints](#17-backend-api-endpoints)
18. [Open Work & Known Issues](#18-open-work--known-issues)
19. [OTA Firmware Update Roadmap](#19-ota-firmware-update-roadmap)
20. [Design Notes](#20-design-notes)

---

## 1. Architecture Overview

Understanding the data flow before touching code will save you hours. The system has three tiers:

```
+-----------------------------------+        +-----------------------------------+
|      WROOM Node (ESP32-D0WD-V3)   |        |   CrowPanel Node (ESP32-S3 QFN56) |
|                                   |        |                                   |
|  AS608 Fingerprint Sensor         |        |  5" 800x480 Touchscreen (LVGL)    |
|  WiFi (NTP timekeeping — RTC TBD) |        |  Employee list and logs UI        |
|  WiFi (for NTP + backend calls)   |        |  WiFi (for backend sync)          |
|                                   |        |                                   |
|  Polls finger -> match -> sends   |<------>|  Receives match/enroll events     |
|  attendance event over ESP-NOW    | ESP-NOW|  Shows result screen              |
|                                   |        |                                   |
|  Handles: activation, NTP sync,   |        |  Handles: employee list sync,     |
|  fingerprint enroll/delete,       |        |  attendance log display,          |
|  factory reset                    |        |  WiFi setup UI, settings          |
+------------------+----------------+        +------------------+----------------+
                   | HTTPS (REST API)                            | HTTPS (REST API)
                   +---------------------------+-----------------+
                                               |
                                    +----------v-----------+
                                    |    ManPro Backend API |
                                    |  (demo.manpromanage- |
                                    |   ment.com or local  |
                                    |   mock server)        |
                                    +----------------------+
```

**How a time-in works, end to end:**

1. Employee places finger on the AS608 sensor on the WROOM.
2. WROOM matches the fingerprint to a stored template and resolves the employee ID.
3. WROOM sends a `{type: MATCH, emp_id: ..., ts: ...}` JSON packet to the CrowPanel via ESP-NOW.
4. CrowPanel displays the result screen (welcome / access denied) and logs the attendance locally.
5. CrowPanel uploads the log entry to the backend API over WiFi on the next sync cycle.

**ESP-NOW channel management:** Both nodes must use the same WiFi channel. The WROOM monitors the channel continuously; if the AP hops channels, the WROOM sends a `CHANNEL_HOP` notification so the CrowPanel resyncs without a reboot.

---

## 2. Hardware

| Node      | Board                        | Chip                 | Role                                     |
| --------- | ---------------------------- | -------------------- | ---------------------------------------- |
| CrowPanel | Elecrow CrowPanel 5" 800×480 | **ESP32-S3 (QFN56)** | Touchscreen UI, WiFi, backend comms      |
| WROOM     | ESP32-WROOM-32 dev module    | **ESP32-D0WD-V3**    | Fingerprint scanner, ESP-NOW bridge      |

### CrowPanel SD Card Requirement (Deep Storage)

The system uses a **Smart Cache** architecture. The WROOM's AS608 fingerprint sensor acts as a fast L1 cache holding 127 active templates. The CrowPanel's SD Card acts as the primary "Deep Storage", holding an unlimited number of fingerprint templates for offline scale.

- **Requirement:** A MicroSD card (FAT32 formatted) must be inserted into the CrowPanel.
- **Directory Structure:** The firmware expects the directory `/templates/` at the root of the SD card.
- **File Format:** Templates are stored as raw 512-byte binary files (e.g., `/templates/<emp_id>_<finger_index>.bin`).

---

## 3. Power Requirements

### Supply Voltages

| Component                        | Voltage                        | Typical Current                                 | Peak Current                      |
| -------------------------------- | ------------------------------ | ----------------------------------------------- | --------------------------------- |
| ESP32-D0WD-V3 (WROOM-32 module)  | 3.3 V                          | ~80 mA idle                                     | ~350 mA WiFi TX burst             |
| CrowPanel ESP32-S3 (QFN56) board | 5 V (USB-C) or 3.3 V regulated | ~150 mA idle                                    | ~500 mA with LCD backlight + WiFi |
| AS608 Fingerprint Sensor         | 3.3 V                          | ~120 mA scanning                                | ~140 mA                           |
| DFPlayer Mini + Speaker          | 5.0 V                          | ~20 mA idle                                     | ~200 mA playing                   |


### Power Strategy

**During development:**

- Power the **CrowPanel** via its USB-C port from a PC or USB charger. The board has an onboard 3.3 V LDO — you do not need a separate regulator.
- Power the **WROOM dev board** via its USB-micro/USB-C port. Most WROOM dev boards also have a 3.3 V LDO.
- The **AS608** can be powered from the WROOM board's 3.3 V pin (labelled `3V3`). Its draw (~120–140 mA) is well within the AMS1117 LDO's 800 mA rating.

**In the final enclosure:**

- The WROOM + AS608 sub-assembly should be powered from a regulated **5 V 1 A** supply (e.g. USB wall adapter) feeding the WROOM dev board's USB connector, or via VIN if using a bare module with an external 3.3 V regulator.
- The CrowPanel should be powered independently via its USB-C connector from a **5 V 2 A** source. Its LCD backlight alone draws ~200 mA.
- **Do not share a USB port between the two boards** during development — each board needs its own Serial port for log monitoring and flashing.

### Wire Gauge

- For power rails: 26 AWG stranded is sufficient for the current levels involved.
- For UART signal lines: 28 AWG or 30 AWG.
- Keep wire runs under 30 cm to avoid voltage drop on the 3.3 V rail.

---

## 4. Wiring & Pinout

### WROOM — Complete Pin Map

| Pin          | GPIO          | Function                   | Notes                                           |
| ------------ | ------------- | -------------------------- | ----------------------------------------------- |
| 3V3          | —             | 3.3 V output               | Powers AS608                              |
| GND          | —             | Ground                     | Common ground for all peripherals         |
| **UART1 RX** | **GPIO27**    | AS608 TX ->                | Connect to AS608 TXD pin                  |
| **UART1 TX** | **GPIO26**    | AS608 RX <-                | Connect to AS608 RXD pin                  |
| **GPIO34**   | Input-only    | AS608 T-OUT (touch detect) | Pulled HIGH by sensor when finger present |
| **GPIO21**   | SDA           | Reserved — RTC (future)    | Not connected; reserved for DS3231 I2C    |
| **GPIO22**   | SCL           | Reserved — RTC (future)    | Not connected; reserved for DS3231 I2C    |
| **UART2 RX** | **GPIO17**    | DFPlayer TX <-             | Audio RX (Use Logic Level Shifter!)             |
| **UART2 TX** | **GPIO16**    | DFPlayer RX ->             | Audio TX (Use Logic Level Shifter!)             |
| **GPIO13**   | Output        | Reserved — Buzzer (future) | Not connected; reserved for buzzer signal       |
| **GPIO14**   | Factory Reset | Active HIGH button         | Uses internal pull-down; hold 5 s to wipe       |

**Pins to avoid on ESP32-D0WD-V3 (WROOM-32):**

| GPIO      | Reason                                                                              |
| --------- | ----------------------------------------------------------------------------------- |
| GPIO0     | Strapping pin — LOW at boot = download mode; do not pull low with external hardware |
| GPIO2     | Strapping pin — must be LOW during flash; avoid driving it during boot              |
| GPIO12    | Strapping pin — sets flash voltage; keep floating or LOW                            |
| GPIO15    | Strapping pin — controls boot logging; keep floating for normal use                 |
| GPIO34–39 | Input-only — no internal pull-up/pull-down; cannot be driven as outputs             |
| GPIO6–11  | Connected to internal SPI flash — do not use                                        |

**Connector type:** JST-PH 2.0 mm 2-pin or standard 2.54 mm DuPont headers are both fine for a prototype enclosure.

---

### AS608 Fingerprint Sensor Wiring

| AS608 Pin | Connects to                  |
| --------- | ---------------------------- |
| VCC       | WROOM 3V3                    |
| GND       | WROOM GND                    |
| TXD       | WROOM GPIO27 (UART1 RX)      |
| RXD       | WROOM GPIO26 (UART1 TX)      |
| T-OUT     | WROOM GPIO34                 |
| WAKEUP    | Leave floating or tie to GND |

> The AS608 communicates at 57600 baud by default. The firmware initialises UART1 at this speed. Do not change it unless you also update `fingerprint_manager.cpp`.

---

### Audio (DFPlayer Mini) Wiring

> **Hardware Setup Notes:**
>
> - **Logic Level Converter:** The ESP32 is a 3.3V device, but the DFPlayer Mini runs best at 5V. The RX/TX lines are routed through a logic level converter to protect the ESP32 pins.
> - **Active Buzzer (Not Implemented — Future Work):** The active buzzer has not been integrated into the hardware. GPIO13 is physically reserved for it. When implemented, it requires an NPN transistor and a flyback diode to drive safely from the ESP32.

| Audio Pin         | Connects to                                             | Notes                                                                       |
| ----------------- | ------------------------------------------------------- | --------------------------------------------------------------------------- |
| DFPlayer VCC      | 5V Power Rail                                           | Provide 5V for loud/clean audio                                             |
| DFPlayer GND      | Common GND                                              |                                                                             |
| DFPlayer RX       | Logic Level Converter (HV) -> (LV) -> WROOM GPIO16 (TX) | Converts 3.3V TX to 5V                                                      |
| DFPlayer TX       | Logic Level Converter (HV) -> (LV) -> WROOM GPIO17 (RX) | Converts 5V TX to 3.3V                                                      |
| DFPlayer SPK+     | Speaker Positive (+)                                    |                                                                             |
| DFPlayer SPK-     | Speaker Negative (-)                                    |                                                                             |

_Place your audio files (e.g., `0001.mp3`, `0002.mp3`) inside a folder literally named `mp3` on the root of the SD card before inserting it into the DFPlayer._

---

### DS3231 RTC Module (Not Implemented — Future Work)

> **The DS3231 hardware RTC has not been integrated into the firmware.** GPIO21 (SDA) and GPIO22 (SCL) are physically reserved for it, and the `time_manager.cpp` file exists as a stub, but the RTC driver is not initialised at boot. The WROOM currently uses NTP as its sole time source and falls back to `millis()`-based offset if WiFi is unavailable.
>
> Integrating the DS3231 is tracked as a future implementation item. When implemented, refer to the wiring table below:

| DS3231 Pin | Planned Connection |
| ---------- | ------------------ |
| VCC        | WROOM 3V3          |
| GND        | WROOM GND          |
| SDA        | WROOM GPIO21       |
| SCL        | WROOM GPIO22       |
| SQW        | Not connected      |
| 32K        | Not connected      |

---

### CrowPanel — Key Pins

The CrowPanel 5" board is a self-contained unit. You do not need to wire any peripherals to it — the display, touch controller, and SD card are all onboard. The only external connection is USB-C for power and flashing.

| Function      | Notes                                                |
| ------------- | ---------------------------------------------------- |
| USB-C         | Power + Serial (CDC) for flashing and Serial Monitor |
| ESP-NOW radio | Uses onboard antenna; no external connection needed  |
| WiFi          | Uses onboard antenna                                 |

**Pins to avoid on ESP32-S3 (QFN56) (if you ever need to expand):**

| GPIO           | Reason                                  |
| -------------- | --------------------------------------- |
| GPIO0          | Strapping pin — boot mode selection     |
| GPIO3          | Strapping pin                           |
| GPIO19, GPIO20 | USB D+/D− — used by CDC USB port        |
| GPIO26–32      | Connected to OPI PSRAM — do not use     |
| GPIO33–37      | Connected to onboard flash — do not use |

---

## 5. Arduino IDE Setup

**Version:** Arduino IDE 2.3.10

> Since the IDE's Tools menu settings are **per-board-selection** and do not persist per-sketch, always double-check the **Board** dropdown before flashing. Flashing the wrong sketch to the wrong board (or with the wrong partition scheme) will produce a silent bad build.

### Step 1 — Install Arduino IDE

Download the installer from https://www.arduino.cc/en/software — choose **Arduino IDE 2.3.10** (Windows Installer). Run with default options.

### Step 2 — Add the ESP32 Board Package

1. Open Arduino IDE -> **File -> Preferences**.
2. In **Additional boards manager URLs**, paste:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Click **OK**.
4. Go to **Tools -> Board -> Boards Manager**, search for `esp32`, and install **esp32 by Espressif Systems**.
   - **Required version: 3.2.0** (or the latest 3.x release). Version 2.x is not compatible with the ESP32-S3 PSRAM driver used by the CrowPanel.

### Step 3 — Install Libraries

Open **Tools -> Manage Libraries** and install each of the following by searching the Library Manager:

**For CrowPanel:**

| Search Term      | Library to Install      | Pin Version To |
| ---------------- | ----------------------- | -------------- |
| Adafruit BusIO   | Adafruit BusIO          | 1.17.4         |
| DFRobot DFPlayer | DFRobotDFPlayerMini     | 1.0.6          |
| LovyanGFX        | LovyanGFX (by lovyan03) | 1.2.25         |
| lvgl             | lvgl (by kisvegabor)    | **8.3.11**     |

> ⚠️ **LVGL is pinned to 8.3.11.** Version 9.x contains breaking API changes affecting `lv_obj_t` styling and display driver registration. Do **not** allow Library Manager to auto-update this library.

**For WROOM:**

| Search Term          | Library to Install                  | Version |
| -------------------- | ----------------------------------- | ------- |
| Adafruit BusIO       | Adafruit BusIO                      | 1.17.4  |
| Adafruit Fingerprint | Adafruit Fingerprint Sensor Library | 2.1.4   |
| ArduinoJson          | ArduinoJson (by Benoit Blanchon)    | 7.4.3   |

The following are used directly but bundled with the ESP32 board package — no separate install needed: `WiFi`, `WiFiUdp`, `HTTPClient`, `esp_now`, `esp_system`, `esp_wifi`, `Preferences`, `Wire`, `time.h`.

---

### CrowPanel — ESP32-S3 (QFN56) Board Settings

| Setting                              | Value                                |
| ------------------------------------ | ------------------------------------ |
| Board                                | ESP32S3 Dev Module                   |
| USB CDC On Boot                      | Enabled                              |
| CPU Frequency                        | 240MHz (WiFi)                        |
| Core Debug Level                     | None                                 |
| USB DFU On Boot                      | Disabled                             |
| Erase All Flash Before Sketch Upload | Disabled                             |
| Events Run On                        | Core 1                               |
| Flash Mode                           | QIO 80MHz                            |
| Flash Size                           | 4MB (32Mb)                           |
| JTAG Adapter                         | Disabled                             |
| Arduino Runs On                      | Core 1                               |
| USB Firmware MSC On Boot             | Disabled                             |
| **Partition Scheme**                 | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| **PSRAM**                            | **OPI PSRAM**                        |
| Upload Mode                          | UART0 / Hardware CDC                 |
| Upload Speed                         | 921600                               |
| USB Mode                             | Hardware CDC and JTAG                |
| Zigbee Mode                          | Disabled                             |

### WROOM Controller — ESP32-D0WD-V3 Board Settings

| Setting                                  | Value                                                |
| ---------------------------------------- | ---------------------------------------------------- |
| Board                                    | ESP32 Dev Module                                     |
| CPU Frequency                            | 240MHz (WiFi/BT)                                     |
| Core Debug Level                         | None                                                 |
| **Erase All Flash Before Sketch Upload** | **Enabled**                                          |
| Events Run On                            | Core 1                                               |
| Flash Frequency                          | 80MHz                                                |
| Flash Mode                               | QIO                                                  |
| Flash Size                               | 4MB (32Mb)                                           |
| JTAG Adapter                             | Disabled                                             |
| Arduino Runs On                          | Core 1                                               |
| **Partition Scheme**                     | **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)** |
| **PSRAM**                                | **Disabled**                                         |
| Upload Speed                             | 921600                                               |
| Zigbee Mode                              | Disabled                                             |

> **WROOM:** "Erase All Flash Before Sketch Upload" must be Enabled. The Preferences (NVS) partition layout changed during development and stale data causes silent failures on boot.

---

## 6. Configuration Reference

All runtime-tunable values are defined in the files below. Edit these before flashing to match your environment.

### `wroom_firmware/src/config.h`

| Constant             | Default                             | What it controls                                                                                                  |
| -------------------- | ----------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `DEVICE_ID`          | `F001-2608-6AEC-ON92`               | Unique device identifier sent to backend during activation. Must match `DEVICE_ID_HARDCODED` in `data_manager.h`. |
| `API_BASE_URL`       | `https://demo.manpromanagement.com` | Backend base URL. Change to `http://192.168.x.x:8000` for local mock server.                                      |
| `ESPNOW_CHANNEL`     | `1`                                 | Fixed WiFi channel for ESP-NOW. Your router must be locked to this channel.                                       |
| `ESPNOW_PAYLOAD_MAX` | `251`                               | Maximum ESP-NOW packet size in bytes. Do not increase beyond 250.                                                 |
| `PIN_FP_RX`          | `27`                                | UART1 RX — receives data from AS608 TXD pin.                                                                      |
| `PIN_FP_TX`          | `26`                                | UART1 TX — sends data to AS608 RXD pin.                                                                           |
| `PIN_FP_TOUCH`       | `34`                                | Input from AS608 T-OUT (finger present detect).                                                                   |
| `PIN_FACTORY_RESET`  | `14`                                | Active-HIGH button. Hold for 5 s to factory reset.                                                                |
| `MAX_SLOTS`          | `127`                               | AS608 fingerprint template slot limit. Do not increase.                                                           |

### `wroom_firmware/src/comms.cpp`

| Variable           | Default             | What it controls                                                                                          |
| ------------------ | ------------------- | --------------------------------------------------------------------------------------------------------- |
| `CROWPANEL_MAC[6]` | `{0x30, 0xED, ...}` | Station MAC address of the CrowPanel. **You must update this** before flashing the WROOM (see Section 7). |

### `crowpanel_firmware/src/core/comm_manager.h`

| Variable       | Default                             | What it controls                                                                          |
| -------------- | ----------------------------------- | ----------------------------------------------------------------------------------------- |
| `WROOM_MAC[6]` | `{0x30, 0x76, ...}`                 | Station MAC address of the WROOM. **You must update this** before flashing the CrowPanel. |
| `API_BASE_URL` | `https://demo.manpromanagement.com` | Backend base URL for CrowPanel attendance uploads.                                        |

### `crowpanel_firmware/src/core/data_manager.h`

| Constant              | Default               | What it controls                                                                                                                   |
| --------------------- | --------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| `DEVICE_ID_HARDCODED` | `F001-2608-6AEC-ON92` | Must match `DEVICE_ID` in WROOM `config.h`. Both boards send this to the backend to prove they belong to the same registered unit. |

> ⚠️ **The device ID versioning scheme is not yet defined.** `data_manager.h` has a TODO: _"Make this dynamic, get the versioning pattern from senior dev."_ For now, assign IDs manually and keep them in sync across both files.

### Time / NTP (`wroom_firmware/src/time_manager.cpp`)

> **Note:** The DS3231 RTC is not yet implemented. `time_manager.cpp` currently only handles NTP sync and `millis()`-based fallback.

| Value               | Default                  | What it controls                                                           |
| ------------------- | ------------------------ | -------------------------------------------------------------------------- |
| UTC offset          | `8 * 3600` (UTC+8 / PST) | Change the multiplier for your timezone, e.g. `7 * 3600` for ICT (UTC+7). |
| Primary NTP server  | `pool.ntp.org`           | Standard global NTP pool.                                                  |
| Fallback NTP server | `time.google.com`        | Used if the primary is unreachable.                                        |

### WiFi Credentials

WiFi credentials are **not** hardcoded — they are entered at runtime via the CrowPanel **Settings -> WiFi** screen and stored in LittleFS. There is no file to edit for WiFi; just use the on-device UI.

---

## 7. First-Time Flash Order

> Always flash the CrowPanel first. The WROOM needs the CrowPanel's MAC address baked in before it can pair.

### Step 1 — Flash CrowPanel and get its MAC

1. Open `crowpanel_firmware/crowpanel_firmware.ino` in Arduino IDE.
2. Select **Tools -> Board -> ESP32S3 Dev Module** and apply all settings from the CrowPanel board settings table.
3. Connect the CrowPanel via USB-C. Select its port under **Tools -> Port**.
4. Click **Upload** (arrow button). Wait for "Done uploading."
5. Open **Tools -> Serial Monitor**. Set baud rate to **115200** and line ending to **Newline**.
6. Press the reset button on the CrowPanel (or power-cycle it).
7. Wait for the boot log. Within ~3 seconds you should see:
   ```
   [BOOT] CP MAC: 30:ED:A0:31:70:EC
   [DISPLAY] Init OK
   [LVGL] Ready
   ```
8. **Copy the full MAC address** from the `[BOOT] CP MAC:` line.

> **If nothing appears in Serial Monitor:** Make sure `USB CDC On Boot` is set to **Enabled** in board settings, and that you have selected the correct COM port. On Windows, open Device Manager and look for "USB Serial Device (COMx)" under Ports.

---

### Step 2 — Update WROOM firmware with CrowPanel MAC

1. Open `wroom_firmware/src/comms.cpp`.
2. Find the line:
   ```cpp
   uint8_t CROWPANEL_MAC[6] = {0x30, 0xED, 0xA0, 0x31, 0x70, 0xEC};
   ```
3. Replace the 6 hex bytes with the MAC you copied in Step 1. For example, if the MAC was `A4:CF:12:AB:CD:EF`:
   ```cpp
   uint8_t CROWPANEL_MAC[6] = {0xA4, 0xCF, 0x12, 0xAB, 0xCD, 0xEF};
   ```
4. Also update `WROOM_MAC[6]` in `crowpanel_firmware/src/core/comm_manager.h` with the WROOM's own MAC (shown as `[BOOT] WROOM MAC:` when you flash the WROOM in Step 3).
5. Save both files.

---

### Step 3 — Flash WROOM

1. Open `wroom_firmware/wroom_firmware.ino` in Arduino IDE.
2. Select **Tools -> Board -> ESP32 Dev Module** and apply all settings from the WROOM board settings table.
3. Connect the WROOM via USB. Select its port under **Tools -> Port**.
4. Click **Upload**. Because `Erase All Flash Before Sketch Upload` is enabled, this takes ~30 seconds on the first flash.
5. Open **Tools -> Serial Monitor** at **115200 baud**.
6. Press reset on the WROOM. Expected boot sequence:
   ```
   === Biometrics WROOM Controller ===
   [BOOT] Reset reason: 1
   [WIFI] Attempting to connect to saved network...
   [AS608] Found! Templates stored: 3
   [ESPNOW] Peer registered: 30:ed:a0:31:70:ec
   Ready. Serial commands:
     ENROLL:<emp_id>:<finger_index>  e.g. ENROLL:1:0
     DELETE:<emp_id>:<finger_index>  e.g. DELETE:1:0
     RESET
   ```
7. Note the `[BOOT] WROOM MAC:` line. Go back and update `WROOM_MAC[6]` in `comm_manager.h` if not already done, then re-flash the CrowPanel.
8. On the CrowPanel, you should briefly see a "WROOM online" toast confirming the link is live.

---

### Step 4 — WiFi Setup and Activation

1. On the CrowPanel, navigate to **Settings -> WiFi**.
2. Scan for networks, select your AP, and enter the password using the on-screen keyboard.
3. Once connected, navigate to **Settings -> Activation** and enter your ManPro registration code.
4. The WROOM calls the backend API to validate, and both boards mark themselves as activated.

---

### Pairing Troubleshooting

| Symptom                                        | Likely Cause                     | Fix                                                                      |
| ---------------------------------------------- | -------------------------------- | ------------------------------------------------------------------------ |
| `[ESPNOW] Send fail` repeated in WROOM Serial  | Wrong MAC address in `comms.cpp` | Re-read CrowPanel MAC and update `comms.cpp`.                            |
| No "WROOM online" toast on CrowPanel           | Channel mismatch                 | Lock router to a fixed channel and match `ESPNOW_CHANNEL` in `config.h`. |
| `[AS608] NOT FOUND` in WROOM Serial            | Wiring error or wrong baud       | Check GPIO26/27 connections. Swap RX/TX if reversed.                     |

| CrowPanel shows blank/white screen after flash | Wrong partition or PSRAM setting | Re-check: Partition = Huge APP, PSRAM = OPI PSRAM.                       |

---

## 8. Mock Server Usage

Use the mock server during development when you do not have access to the live ManPro backend.

### Prerequisites

- Python 3.9 or later
- `pip install flask`

### Starting the Mock Server

```bash
cd mock_server
pip install -r requirements.txt
python mock_server.py
```

The server starts on `http://0.0.0.0:8000` by default and prints available endpoints on startup.

### Pointing the Firmware at the Mock Server

1. Find your development machine's LAN IP address (e.g. `192.168.1.50`).
2. Open `wroom_firmware/src/config.h` and change:
   ```cpp
   #define API_BASE_URL "http://192.168.1.50:8000"
   ```
3. Also change `API_BASE_URL` in `crowpanel_firmware/src/core/comm_manager.h` to the same value.
4. Ensure both devices and your development machine are on the same WiFi network, then flash.

> The mock server returns canned responses for all endpoints. Check its console output to see every request the firmware makes — this is useful for debugging sync issues.

> `crowpanel_firmware/tools/admin_mock.py` is a separate, lightweight admin mock. It is **incomplete** and does not cover all endpoints — use the main mock server above instead.

---

## 9. Repository Layout

```
BIOMETRICSMANPRO/
├── README.md                             # This file
├── docs/
│   └── admin_manual.md                  # End-user / admin UI guide
├── crowpanel_firmware/
│   ├── crowpanel_firmware.ino            # Entry point: setup() + loop(), LVGL init
│   ├── tools/
│   │   ├── convert_img.py               # Converts PNG -> LVGL C array
│   │   ├── run_conv.py                  # Batch runner for convert_img.py
│   │   └── admin_mock.py               # Partial admin API mock (incomplete)
│   └── src/
│       ├── core/
│       │   ├── display_driver.h         # LovyanGFX LGFX config (pins, resolution, touch)
│       │   ├── data_manager.h/.cpp      # All persistent state: employees, logs, settings
│       │   ├── comm_manager.h/.cpp      # ESP-NOW init, RX ring buffer, JSON dispatch
│       │   ├── sync_receiver.h/.cpp     # Binary protocol handler for employee sync
│       │   ├── sync_protocol.h          # Shared packet structs (MUST match WROOM copy)
│       │   └── certs.h                  # GTS Root R4 CA cert for HTTPS
│       ├── ui/
│       │   ├── ui_manager.h/.cpp        # Screen router, showXxx() navigation
│       │   ├── ui_idle.h/.cpp           # Standby screen (clock, finger prompt)
│       │   ├── ui_result.h/.cpp         # Post-scan feedback (green=OK, red=fail)
│       │   ├── ui_pin.h/.cpp            # Admin PIN entry + lockout logic
│       │   ├── ui_main_menu.h/.cpp      # Admin hub (Enroll / Logs / Settings)
│       │   ├── ui_enroll.h/.cpp         # Fingerprint enrolment wizard
│       │   ├── ui_logs.h/.cpp           # Attendance log viewer
│       │   ├── ui_settings.h/.cpp       # Settings hub
│       │   ├── ui_settings_clock.h/.cpp # Manual time/date picker
│       │   ├── ui_settings_danger.h/.cpp# Factory reset / nuke confirmations
│       │   ├── ui_settings_display.h/.cpp # Brightness + timeout sliders
│       │   ├── ui_settings_server.h/.cpp# API base URL override
│       │   ├── ui_wifi_setup.h/.cpp     # WiFi scan + connect UI
│       │   ├── ui_sync_status.h/.cpp    # Sync log viewer
│       │   └── ui_activation.h/.cpp     # Device registration screen
│       ├── splash/
│       │   └── manpro_splash.h          # Boot animation (calls UIManager::loadInitialScreen)
│       └── assets/                      # Compiled LVGL C arrays (icons, fonts)
├── wroom_firmware/
│   ├── wroom_firmware.ino               # Entry point: setup() + loop()
│   └── src/
│       ├── config.h                     # Pin defs, DEVICE_ID, API_BASE_URL, ESP-NOW constants
│       ├── certs.h                      # GTS Root R4 CA cert for HTTPS
│       ├── employee_db.h/.cpp           # Slot->emp_id mapping (stored in NVS)
│       ├── comms.h/.cpp                 # ESP-NOW send/recv, ring buffer, CROWPANEL_MAC
│       ├── wifi_manager.h/.cpp          # WiFi connect, scan, auto-reconnect
│       ├── time_manager.h/.cpp          # NTP timekeeping + millis fallback (DS3231 RTC: future work)
│       ├── fingerprint_manager.h/.cpp   # AS608 sensor: match, enroll, delete
│       ├── activation.h/.cpp            # POST /api/devices/activate -> store token
│       ├── sync_manager.h/.cpp          # Employee sync state machine
│       ├── sync_protocol.h             # Shared packet structs (MUST match CrowPanel copy)
│       └── command_handler.h/.cpp       # Dispatches Serial + ESP-NOW JSON commands
├── assets/
│   ├── logo/                            # Source ManPro logo PNGs
│   ├── icons/                           # Source icon PNGs (converted by tools/)
│   └── boot_anim/                       # Boot animation frames
└── ui_reference/                        # Color palette & design reference screenshots
```

---

## 10. Employee Sync Pipeline

The employee list lives on the ManPro backend. This is the most complex subsystem.

```
ManPro Backend API
     |
     |  GET /api/devices/employees  (HTTPS, Bearer token)
     v
WROOM: SyncManager::fetchEmployeesFromApi()
     |  Parses JSON -> EmployeeSync[] buffer
     |  Handles 5 wrapper key styles: "employees", "data", "result", "payload", bare array
     |
     |  Binary framed over ESP-NOW (sync_protocol.h)
     |  PING -> PONG -> SYNC_START -> SYNC_DATA[n] -> SYNC_END -> SYNC_RESULT
     v
CrowPanel: SyncReceiver
     |  Assembles chunks, verifies CRC32
     |  DataManager::applySyncBuffer() -> empDB[]
     |  Persists to LittleFS (/employees.json)
     |  DataManager::loadFpState() re-applies enrollment bitmask after overwrite
     v
     SYNC_RESULT (OK or NACK with list of missing chunk indices)
     v
WROOM: SyncManager::handleIncomingPacket()
     |  OK  -> SYNC_STATE_IDLE
     |  NACK -> resend missing chunks -> re-send SYNC_END
```

### SyncManager State Machine (WROOM)

States in order:
`IDLE -> FETCH_WIFI -> SET_ESPNOW_CHANNEL -> SEND_PING -> SEND_SYNC_START -> SEND_CHUNKS -> SEND_SYNC_END -> AWAIT_SYNC_RESULT`

- Failure at any state triggers `failToFastRetry()` — enters `FAST_RETRY_MODE` (retries every 5 minutes).
- Sync triggers: (a) manually via `SYNC_EMP` ESP-NOW command from CrowPanel, or (b) automatically at the top of every hour when NTP time is valid.
- **BUG-10 fix** (applied): `s_syncEndResendCount` is a separate counter from `s_retryCount` because `setState()` resets `s_retryCount` — a unified counter made the 3-retry limit on SYNC_END resends unreachable.
- **EDGE-07 fix** (applied): `sync_id` falls back to a monotonic counter if `time(NULL)` returns a pre-NTP epoch value (< 1,000,000,000), preventing stale-packet acceptance from a prior session.

### sync_protocol.h — The Contract

Both firmwares include a **copy** of `sync_protocol.h`. They must be **byte-for-byte identical**. If you change a struct or add a packet type, update both copies. Struct sizes are enforced at compile time via `static_assert`.

| Packet                    | Direction           | Purpose                                       |
| ------------------------- | ------------------- | --------------------------------------------- |
| `SYNC_PING` / `SYNC_PONG` | WROOM <-> CrowPanel | Confirm ESP-NOW link before starting          |
| `SYNC_START`              | WROOM -> CrowPanel  | Declares `sync_id`, total chunks, total bytes |
| `SYNC_DATA`               | WROOM -> CrowPanel  | One chunk of raw `EmployeeSync[]` bytes       |
| `SYNC_CHUNK_ACK`          | CrowPanel -> WROOM  | Confirms receipt of a specific chunk index    |
| `SYNC_END`                | WROOM -> CrowPanel  | Signals completion, carries CRC32             |
| `SYNC_RESULT`             | CrowPanel -> WROOM  | OK or NACK with list of missing chunk indices |

`MAX_SYNC_EMPLOYEES = 127` — matches the AS608 slot limit. Do not increase without also expanding the fingerprint slot pool.  
`MAX_CHUNK_SIZE = 200` bytes — the largest transmitted packet must stay ≤ 250 bytes (ESP-NOW hard limit).

---

## 11. ESP-NOW Message Types

Most inter-node communication uses **JSON strings** sent over ESP-NOW. Only the employee sync uses binary packets (see Section 10). The message type field is `"type"`. The CrowPanel's `CommManager::dispatchJson()` routes all incoming JSON messages.

| `type`              | Direction          | Description                                                              |
| ------------------- | ------------------ | ------------------------------------------------------------------------ |
| `MATCH`             | WROOM -> CrowPanel | Fingerprint matched: `{emp_id, name, ts, confidence, slot, action_type}` |
| `ENROLL_RESULT`     | WROOM -> CrowPanel | Enrolment result: `{success, emp_id, finger_index, slot, msg}`           |
| `EMP_SYNC_START`    | CrowPanel -> WROOM | Trigger employee sync (WROOM starts SyncManager)                         |
| `EMP_SYNC_FAIL`     | WROOM -> CrowPanel | Sync failed — stop UI loading spinner                                    |
| `ACTIVATION_STATUS` | WROOM -> CrowPanel | Activation API result: `{activated, token}`                              |
| `NTP_STATUS`        | WROOM -> CrowPanel | Timestamp string to set CrowPanel RTC: `{time_str}`                      |
| `WIFI_STATUS`       | WROOM -> CrowPanel | WiFi state update: `{connected, ssid, ip}`                               |
| `CHANNEL_HOP`       | WROOM -> CrowPanel | AP changed channel; CrowPanel must re-init ESP-NOW                       |
| `WROOM_ONLINE`      | WROOM -> CrowPanel | Heartbeat on boot — triggers "WROOM online" toast                        |
| `GHOST_LOGIN`       | WROOM -> CrowPanel | Dev backdoor — bypasses scanner, opens Main Menu                         |
| `NUKE_USERS`        | WROOM -> CrowPanel | Dev backdoor — erases all fingerprint slots except slot 1                |
| `DEBUG_COMMS`       | WROOM -> CrowPanel | Dev backdoor — toggles ESP-NOW ping/pong debug output                    |

---

## 12. Persistent Storage Map

### WROOM — NVS (Preferences namespace `"biometrics"`)

| Key            | Type   | Content                           |
| -------------- | ------ | --------------------------------- |
| `activated`    | bool   | Whether device has been activated |
| `device_token` | String | Bearer token from backend         |
| `wifi_ssid`    | String | Saved WiFi network                |
| `wifi_pass`    | String | Saved WiFi password               |
| `emp_count`    | int    | Number of entries in slot map     |
| `emp_X_id`     | String | Employee ID for slot X            |
| `emp_X_fi`     | int    | Finger index for slot X           |

The AS608 sensor is the physical store for fingerprint templates. NVS only stores the slot→emp_id lookup table, managed by `employee_db.h/.cpp`.

### CrowPanel — LittleFS

| File               | Format                 | Content                                                                               |
| ------------------ | ---------------------- | ------------------------------------------------------------------------------------- |
| `/employees.json`  | JSON array             | Employee list (name, dept, job_title, branch, fp_enrolled, enrolled_fingers)          |
| `/config.json`     | JSON object            | Device settings (brightness, timeout, volume, adminPin, deviceName, activated, token) |
| `/wifi.json`       | JSON array (up to 5)   | Saved WiFi credentials                                                                |
| `/attendance.json` | JSON array (up to 200) | Local attendance log (name, time_str, action_type, synced, confidence, slot)          |
| `/fp_state.json`   | JSON object            | Enrollment bitmask per emp_id — persisted separately so it survives sync overwrites   |
| `/sync_log.jsonl`  | JSON Lines             | Last 5 sync event messages with timestamps                                            |

### CrowPanel — SD Card (`/templates/`)

Raw 512-byte binary fingerprint templates stored as `/templates/<emp_id>_<finger_index>.bin`. This is the **Deep Storage** layer — the WROOM's AS608 is L1 cache (127 slots); the SD card is the primary store for larger employee sets. Templates are loaded from SD and pushed to WROOM on demand during enrolment or sync.

---

## 13. Subsystem Deep-Dives

### CrowPanel Boot Sequence

```
setup():
  DataManager::begin()         -> mounts LittleFS, loads all JSON files
  CommManager::begin()         -> inits ESP-NOW, registers onEspNowRecv callback
  lcd.init()                   -> LovyanGFX + GPIO2 backlight reset
  lv_init()                    -> allocates PSRAM render buffers (2x 800x240 px)
  manpro_show_splash(callback) -> plays boot animation, then calls UIManager::loadInitialScreen
  UIManager::begin()           -> builds all LVGL screen objects

loop():
  lv_tick_inc() + lv_task_handler()   -> LVGL rendering tick
  CommManager::process()              -> drains ring buffer, dispatches JSON messages
  Screen timeout check                -> dims backlight after inactivity
```

`UIManager::loadInitialScreen` decides which screen shows after the splash: Activation screen (not activated) → WiFi setup (no saved credentials) → Idle screen (normal operation).

### WROOM Boot Sequence

```
setup():
  WiFiManager::begin()         -> attempts to connect to saved network
  FingerprintManager::init()   -> opens AS608 on UART1, counts stored templates
  TimeManager::init()          -> syncs NTP if WiFi available (DS3231 RTC: not yet implemented)
  Comms::begin()               -> inits ESP-NOW, registers CROWPANEL_MAC as peer
  SyncManager::init()          -> sets state to IDLE
  AudioManager::init()         -> inits DFPlayer Mini on UART2

loop():
  CommandHandler::poll()       -> polls AS608 T-OUT pin, reads Serial commands
  SyncManager::loop()          -> drives sync state machine, hourly auto-trigger
```

### Admin PIN System

- Default PIN: `0000` (set in `DataManager` default config).
- PIN is stored in `/config.json` (plaintext — no hashing currently).
- Lockout: 5 consecutive wrong attempts → 60-second lockout. Tracked in `DataManager::_failedAttempts` / `_lockoutStartTime`, persisted in config across reboots.
- PIN change: **Settings → Danger Zone** (requires current PIN first).
- Admin fingerprint: A special enrolment with `emp_id = "ADMIN"` stored on the SD card. Placing the admin finger on the idle screen opens Main Menu directly without PIN entry.

### Smart Cache Architecture

```
AS608 (WROOM) — L1 cache, 127 slots
  ^ push on enrolment
  v evict: NOT YET IMPLEMENTED (see Section 18)

SD Card (CrowPanel) — Deep storage, unlimited
  /templates/<emp_id>_<finger_index>.bin

ManPro Backend — Source of truth for employee records
```

**Enrolment flow:**

1. CrowPanel UI sends `ENROLL:<emp_id>:<finger_index>` command to WROOM.
2. WROOM captures template on AS608 (gets a physical slot number).
3. WROOM sends the 512-byte template back to CrowPanel in `ENROLL_RESULT`.
4. CrowPanel saves template to SD card.
5. `employee_db` on WROOM stores the slot→emp_id mapping in NVS.

### Time Source Priority

The WROOM currently uses a two-tier clock source:

1. **NTP** (requires WiFi) — synced on boot whenever WiFi is available.
2. **`millis()`-based offset** (last resort — drifts; no persistent timekeeping without WiFi).

> **DS3231 hardware RTC is a planned future implementation.** When added it will serve as a battery-backed middle tier, preserving time across power cycles even without WiFi. GPIO21/22 are already reserved. See the [DS3231 wiring section](#ds3231-rtc-module-not-implemented--future-work) for planned connection details.

---

## 14. Development Workflow

### Sensor-less Testing (WROOM)

Uncomment `#define MOCK_SENSOR 1` in `wroom_firmware/src/config.h`. This simulates fingerprint scans without physical hardware. The `GHOST_LOGIN` Serial command also bypasses the scanner and sends a fake match event to the CrowPanel.

### Image Asset Pipeline

Icons live as source PNGs in `assets/icons/`. To update or add icons:

```bash
cd crowpanel_firmware/tools
python run_conv.py    # batch converts assets/icons/ -> src/assets/*.c
```

The resulting `.c` files are `#include`d into the firmware and declared with `LV_IMG_DECLARE()`.

### Key Files by Area

| Area                                       | Primary Files                                       |
| ------------------------------------------ | --------------------------------------------------- |
| All persistence (CrowPanel)                | `data_manager.h` / `data_manager.cpp`               |
| ESP-NOW receive + JSON routing (CrowPanel) | `comm_manager.h` / `comm_manager.cpp`               |
| Employee sync — sender (WROOM)             | `sync_manager.h` / `sync_manager.cpp`               |
| Employee sync — receiver (CrowPanel)       | `sync_receiver.h` / `sync_receiver.cpp`             |
| Binary packet definitions (both nodes)     | `sync_protocol.h` — **keep both copies identical**  |
| Fingerprint hardware (WROOM)               | `fingerprint_manager.h` / `fingerprint_manager.cpp` |
| All UI screens (CrowPanel)                 | `src/ui/ui_*.h` / `src/ui/ui_*.cpp`                 |
| Screen navigation / routing                | `ui_manager.h` / `ui_manager.cpp`                   |
| Hardware config + pins                     | `wroom_firmware/src/config.h`                       |

---

## 15. WROOM Serial Commands

Connect at **115200 baud**.

| Command                          | Description                                                      |
| -------------------------------- | ---------------------------------------------------------------- |
| `ENROLL:<emp_id>:<finger_index>` | Enroll a finger (e.g. `ENROLL:1:0` = employee 1, first finger)   |
| `DELETE:<emp_id>:<finger_index>` | Erase a stored template (e.g. `DELETE:1:0`)                      |
| `RESET`                          | Reboot the WROOM                                                 |
| `TEST_HW`                        | Hardware test (Plays Track 1)                                    |
| `GHOST_LOGIN`                    | Dev backdoor — bypasses scanner, jumps to Main Menu on CrowPanel |
| `NUKE_USERS`                     | Dev backdoor — erases all stored fingerprints except slot 1      |
| `DEBUG_COMMS`                    | Dev backdoor — toggles ESP-NOW ping/pong debug output            |

---

## 16. Testing Checklist

No automated tests exist. Verify the following manually before any release:

- [ ] Cold boot (power off) → splash animation plays → idle screen loads with correct time
- [ ] Fingerprint time-in → result screen shows correct name and "Time In" → log entry appears in Logs screen
- [ ] Fingerprint time-out → "Time Out" result screen
- [ ] Unrecognized finger → "Not Recognized" result → no log entry written
- [ ] Employee sync triggered from Settings → `[SYNC]` log messages in WROOM Serial → CrowPanel Sync Status screen shows updated employee list
- [ ] Admin PIN: correct PIN opens Main Menu; wrong PIN × 5 triggers 60s lockout
- [ ] WiFi disconnected during sync → WROOM enters FAST_RETRY_MODE → retries after 5 minutes
- [ ] Factory reset on CrowPanel → all data wiped → returns to WiFi setup screen
- [ ] Factory reset on WROOM (hold GPIO14 for 5 s) → fingerprints wiped → NVS cleared
- [ ] Channel hop: change router channel → both nodes re-negotiate within ~10 s

---

## 17. Backend API Endpoints

The firmware calls these endpoints. Consult the ManPro backend team for authentication details and any schema changes.

| Method | Endpoint                 | Caller                       | Purpose                                          |
| ------ | ------------------------ | ---------------------------- | ------------------------------------------------ |
| `POST` | `/api/devices/activate`  | WROOM `activation.cpp`       | Register device, receive bearer token            |
| `GET`  | `/api/devices/employees` | WROOM `sync_manager.cpp`     | Fetch employee list for binary sync to CrowPanel |
| `POST` | `/api/attendance`        | CrowPanel `data_manager.cpp` | Upload attendance log entries                    |

Authentication: `Authorization: Bearer <device_token>` header. Token is obtained during activation and stored in WROOM NVS and CrowPanel `/config.json`.

> `sync_manager.cpp` tries five different JSON wrapper keys (`employees`, `data`, `result`, `payload`, bare array) to handle backend response format variations. If the API schema changes, check `SyncManager::fetchEmployeesFromApi()` first.

---

## 18. Open Work & Known Issues

### Issue 1 — Device ID Versioning Scheme (Undefined)

`DEVICE_ID` in `config.h` and `DEVICE_ID_HARDCODED` in `data_manager.h` are hardcoded strings. A TODO comment in `data_manager.h` reads: _"Make this dynamic, get the versioning pattern from senior dev."_ Until the scheme is defined, IDs must be manually assigned and kept in sync across both files for each physical unit.

### Issue 2 — No Fingerprint Eviction Policy

The AS608 has a hard limit of 127 template slots. There is no LRU or frequency-based eviction policy. If more than 127 fingers are enrolled, enrolment will silently fail. This must be addressed before deploying at any site with more than 127 enrolled fingers.

### Issue 3 — sync_protocol.h Dual-Copy Maintenance Risk

Both `wroom_firmware/src/sync_protocol.h` and `crowpanel_firmware/src/core/sync_protocol.h` must stay byte-for-byte identical. There is no automated check enforcing this. **Any edit to either copy must be manually mirrored to the other.**

### Issue 4 — Admin PIN Stored in Plaintext

The admin PIN is stored as a plain string in `/config.json`. Consider hashing before production deployment.

### Issue 5 — Attendance Upload: No Retry Queue

`DataManager::uploadPendingLogs()` runs attendance POSTs on a FreeRTOS background task. If the task fails mid-batch, those logs remain as `synced=false` and are retried on the next upload cycle. This is acceptable for now but should be replaced with a proper retry queue before production.

### Issue 6 — WiFi Channel Lock Dependency

The router **must** be locked to a fixed channel (default: channel 1). If the router uses auto-channel, the WROOM sends a `CHANNEL_HOP` notification, but this path is not fully hardened. Always configure and test with a fixed channel.

### Issue 7 — OTA Firmware Updates Not Implemented

See Section 19 below.

---

## 19. OTA Firmware Update Roadmap

Both the WROOM and CrowPanel will autonomously check for new firmware over the internet, download it, and apply it — no USB cable needed after the one migration flash.

### How It Will Work

```
Your PC                  GitHub Releases                Devices (anywhere)
------                   ---------------                ------------------
Build .bin files  -push-> firmware_manifest.json        WROOM & CrowPanel
Tag a release           + wroom_v1.1.0.bin              check manifest on boot
                        + crowpanel_v1.1.0.bin  <------  and every 6 hours
                                                               |
                                                    If version newer: download
                                                    .bin over HTTPS -> flash
                                                    -> reboot into new firmware
```

### Open Questions Before Implementation

> ⚠️ **CrowPanel flash size** — The CrowPanel currently uses `Huge APP (3MB No OTA)`, occupying a 3 MB app slot. OTA requires two equal app slots. On a 4 MB chip, two 3 MB slots don't fit. Options:
>
> - **Option A (Recommended):** If the board ships with **16 MB flash** (many do — the `4MB` Arduino setting is a conservative default), use a custom 16 MB partition with two 6 MB app slots.
> - **Option B:** If truly 4 MB flash, reduce the CrowPanel firmware footprint to fit two ~1.9 MB OTA slots (may require stripping some compiled icon assets).
>
> **Action required:** In Arduino IDE with CrowPanel selected, go to `Sketch → Export Compiled Binary` and report the `.bin` file size. If under ~1.7 MB, 4 MB OTA works.

> ⚠️ **One final USB flash required.** Switching partition schemes wipes the existing partition table. Both boards need one last USB flash to migrate to the OTA-capable partition.

### Proposed Changes

**WROOM:**

- `config.h` — add `FIRMWARE_VERSION`, `OTA_MANIFEST_URL`, `OTA_CHECK_INTERVAL_MS`
- New `ota_manager.h/.cpp` — state machine: `IDLE → CHECK_VERSION → DOWNLOADING → DONE/FAILED`; uses `HTTPUpdate` + `GTS_ROOT_R4` cert; sends `OTA_STATUS` JSON to CrowPanel during download
- `command_handler.cpp` — handle `OTA_CHECK` command from CrowPanel
- Partition scheme: `Default 4MB with spiffs` → **Min SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**

**CrowPanel:**

- New `src/core/ota_manager.h/.cpp` — checks manifest, calls `HTTPUpdate`, triggers WROOM OTA after self-update
- New `src/ui/ui_ota.h/.cpp` — LVGL overlay with "Update available" prompt + progress bar
- `ui_settings.cpp` — add "Firmware Update" entry to Settings menu
- `comm_manager.cpp` — handle incoming `OTA_STATUS` from WROOM
- Partition scheme: TBD (see Open Questions above)

**Shared:**

- `firmware_manifest.json` at repo root (served via GitHub raw URL)
- `sync_protocol.h` — add `OTA_STATUS` and `OTA_CHECK` message type constants

### Release Workflow (once implemented)

```
1. Edit firmware, bump FIRMWARE_VERSION in config.h
2. Arduino IDE -> Sketch -> Export Compiled Binary
3. Create a GitHub Release tagged "v1.x.x"
4. Upload wroom_v1.x.x.bin and crowpanel_v1.x.x.bin as release assets
5. Edit firmware_manifest.json with the new version + URLs
6. Commit and push
   -> Devices pick it up within 6 hours (or immediately on next boot)
```

---

## 20. Design Notes

**Partition schemes differ intentionally.** The CrowPanel uses _Huge APP (3MB)_ because LVGL + LovyanGFX + compiled icon assets consume significantly more flash than a typical sketch. OTA is not used on that board yet. The WROOM uses the standard _Default 4MB with spiffs_ scheme.

**PSRAM is enabled only on the CrowPanel.** LVGL's double display buffers (~160 KB each) are allocated from PSRAM at boot. The WROOM-32 module does not have PSRAM populated.

**ESP-NOW channel management.** Both boards must be on the same WiFi channel for ESP-NOW to work. The WROOM monitors the channel in real time and sends `CHANNEL_HOP` notifications to the CrowPanel whenever the AP channel changes, so they stay in sync without a reboot.

**Time source priority.** The WROOM currently uses two clock sources: NTP (requires WiFi, synced on boot) and a `millis()`-based offset as a last resort when offline. A DS3231 hardware RTC is planned as a battery-backed middle tier for future implementation — GPIO21/22 are already reserved for it.

**Employee data lives on the CrowPanel.** The CrowPanel stores the employee list in LittleFS and syncs it from the backend. The WROOM only stores fingerprint template slot-to-employee-ID mappings in its own flash via the `employee_db` module. A sync operation pushes the mapping table from CrowPanel to WROOM over ESP-NOW using a binary framed protocol (`sync_protocol.h`).

**Admin PIN is plaintext.** The PIN is stored as a plain string in `/config.json`. Hashing (e.g. SHA-256 via `mbedtls`) should be added before production deployment.
