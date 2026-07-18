# Biometrics Time-In/Out System

An ESP32-based biometric attendance system. A fingerprint scanner node (WROOM) communicates wirelessly via ESP-NOW to a touchscreen display node (CrowPanel), which connects to the backend over WiFi.

---

## Hardware

| Node | Board | Role |
|---|---|---|
| CrowPanel | ESP32-S3 (5" 800×480) | Touchscreen UI, WiFi, backend comms |
| WROOM | ESP32-WROOM-32 | Fingerprint scanner, RTC, ESP-NOW bridge |

**WROOM wiring:**

| Peripheral | Interface | Pins |
|---|---|---|
| AS608 Fingerprint | UART1 | RX=GPIO27, TX=GPIO26, TOUCH=GPIO34 |
| DS3231 RTC | I2C | SDA=GPIO21, SCL=GPIO22 |
| Factory Reset Button | GPIO | GPIO14 (active HIGH, internal pull-down) |
| CrowPanel | ESP-NOW | Wireless — no UART wire |

---

## Project Structure

```
BIOMETRICSMANPRO/
├── crowpanel_firmware/
│   ├── crowpanel_firmware.ino      Entry point (setup + loop)
│   └── src/
│       ├── core/                   comm_manager, data_manager, display_driver
│       ├── ui/                     All LVGL screens (idle, enroll, logs, settings…)
│       ├── splash/                 Boot animation (manpro_splash + manpro_logo)
│       └── assets/                 Compiled LVGL icon and font data (.c files)
├── wroom_firmware/
│   ├── wroom_firmware.ino          Entry point (setup + loop)
│   └── src/
│       ├── config.h                Pin definitions, device ID, ESP-NOW constants
│       ├── employee_db.h/.cpp      Employee records and slot lookup
│       ├── comms.h/.cpp            ESP-NOW transport, ring buffer, channel sync
│       ├── wifi_manager.h/.cpp     WiFi connection, scan, auto-reconnect
│       ├── time_manager.h/.cpp     RTC, NTP sync, timestamp formatter
│       ├── fingerprint_manager.h/.cpp  AS608 sensor, match, enroll
│       ├── activation.h/.cpp       Backend API call for device registration
│       └── command_handler.h/.cpp  Command dispatcher, factory reset, fingerprint poll
├── assets/
│   ├── logo/                       ManPro logo and banner
│   ├── icons/                      Source icon PNGs (icon_*.png)
│   └── boot_anim/                  Boot animation source and GIF reference
├── mock_server/                    Development mock server (Python)
├── wroom_firmware/tools/           Image conversion scripts
└── UI_REFERENCE/                   Color palette and design references
```

---

## Arduino IDE Setup

**Version:** Arduino IDE 2.3.10

> Since the IDE's Tools menu settings are **per-board-selection** and do not persist per-sketch, always double-check the **Board** dropdown before flashing. Flashing the wrong sketch to the wrong board (or with the wrong partition scheme) will produce a silent bad build.

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

> ⚠️ **LVGL is pinned to 8.3.11.** Version 9.x contains breaking API changes affecting `lv_obj_t` styling and display driver registration. Do **not** allow Library Manager to auto-update this library.

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

The following are used directly but bundled with the ESP32 board package — no separate install needed: `WiFi`, `WiFiUdp`, `HTTPClient`, `esp_now`, `esp_system`, `esp_wifi`, `Preferences`, `Wire`, `time.h`.

---

## First-Time Flash Order

1. **Flash the CrowPanel first.** Open the Serial Monitor at 115200 baud and copy the `[BOOT] CP MAC:` line — this is the CrowPanel's station MAC address.
2. **Paste the MAC into `wroom_firmware/src/config.h`** — update the `CROWPANEL_MAC` array with the 6 hex bytes from step 1.
3. **Flash the WROOM.** Open its Serial Monitor and confirm `[BOOT] WROOM MAC:` appears, followed by `[AS608] Found!` and `[RTC] Ready:`.
4. The two boards will pair automatically over ESP-NOW on channel 1.

---

## WROOM Serial Commands

Connect at **115200 baud**.

| Command | Description |
|---|---|
| `ENROLL:<emp_id>:<finger_index>` | Enroll a finger (e.g. `ENROLL:1:0` = employee 1, first finger) |
| `DELETE:<emp_id>:<finger_index>` | Erase a stored template (e.g. `DELETE:1:0`) |
| `RESET` | Reboot the WROOM |
| `GHOST_LOGIN` | Dev backdoor — bypasses scanner, jumps to Main Menu on CrowPanel |
| `NUKE_USERS` | Dev backdoor — erases all stored fingerprints except slot 1 |
| `DEBUG_COMMS` | Dev backdoor — toggles ESP-NOW ping/pong debug output |

---

## Design Notes

**Partition schemes differ intentionally.** The CrowPanel uses *Huge APP (3MB)* because LVGL + LovyanGFX + compiled icon assets consume significantly more flash than a typical sketch. OTA is not used on that board. The WROOM uses the standard *Default 4MB with spiffs* scheme.

**PSRAM is enabled only on the CrowPanel.** LVGL's double display buffers (~160KB each) are allocated from PSRAM at boot. The WROOM-32 module does not have PSRAM populated.

**ESP-NOW channel management.** Both boards must be on the same WiFi channel for ESP-NOW to work. The WROOM monitors the channel in real time and sends `CHANNEL_HOP` notifications to the CrowPanel whenever the AP channel changes, so they stay in sync without a reboot.
