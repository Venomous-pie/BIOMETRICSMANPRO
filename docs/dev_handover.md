# Developer Handover — Biometrics Time-In/Out System

**Date:** 2026-08-11  
**Status:** Active development — pre-production  
**Prepared for:** Incoming developer / maintainer

---

## 1. Quick Orientation

This is an **ESP32 dual-node biometric attendance system**. There is no PC application — the entire product is two microcontroller firmware images and a hosted backend (ManPro):

| Node | Hardware | Sketch | Role |
|---|---|---|---|
| **WROOM** | ESP32-D0WD-V3 (WROOM-32) | `wroom_firmware/wroom_firmware.ino` | Fingerprint scanner, RTC, ESP-NOW sender |
| **CrowPanel** | ESP32-S3 QFN56 (Elecrow 5") | `crowpanel_firmware/crowpanel_firmware.ino` | Touchscreen UI, backend WiFi, ESP-NOW receiver |

They talk to each other wirelessly over **ESP-NOW** (no UART cable). The CrowPanel also talks to the ManPro backend over **HTTPS**. The WROOM talks to the backend only for **activation** and **employee list fetching**.

Read the main `README.md` first. This document picks up where it leaves off — it covers the codebase internals, active bugs, and incomplete work that the README does not.

---

## 2. Repository Layout (Complete)

```
BIOMETRICSMANPRO/
├── README.md                        <- Hardware setup, flash order, wiring, config reference
├── firmware_update_plan.md          <- OTA update design (not yet implemented — see Section 7)
├── docs/
│   ├── admin_manual.md              <- End-user / admin guide (UI screens explained)
│   └── dev_handover.md              <- THIS FILE
│
├── crowpanel_firmware/
│   ├── crowpanel_firmware.ino       <- setup() + loop(); LVGL init, display driver
│   └── src/
│       ├── core/
│       │   ├── display_driver.h     <- LovyanGFX LGFX config (pins, resolution, touch)
│       │   ├── data_manager.h/.cpp  <- All persistent state: employees, logs, settings
│       │   ├── comm_manager.h/.cpp  <- ESP-NOW init, RX ring buffer, JSON dispatch
│       │   ├── sync_receiver.h/.cpp <- Binary protocol handler for employee sync
│       │   ├── sync_protocol.h      <- Shared packet structs (MUST match WROOM copy)
│       │   └── certs.h              <- GTS Root R4 CA cert for HTTPS
│       ├── ui/
│       │   ├── ui_manager.h/.cpp    <- Screen router, showXxx() functions
│       │   ├── ui_idle.h/.cpp       <- Default standby screen (clock, finger prompt)
│       │   ├── ui_result.h/.cpp     <- Post-scan feedback (green=OK, red=fail)
│       │   ├── ui_pin.h/.cpp        <- Admin PIN entry + lockout logic
│       │   ├── ui_main_menu.h/.cpp  <- Admin hub (Enroll / Logs / Settings)
│       │   ├── ui_enroll.h/.cpp     <- Fingerprint enrolment wizard
│       │   ├── ui_logs.h/.cpp       <- Attendance log viewer
│       │   ├── ui_settings.h/.cpp   <- Settings hub
│       │   ├── ui_settings_clock.h/.cpp    <- Manual time/date picker
│       │   ├── ui_settings_danger.h/.cpp   <- Factory reset / nuke confirmations
│       │   ├── ui_settings_display.h/.cpp  <- Brightness + timeout sliders
│       │   ├── ui_settings_server.h/.cpp   <- API base URL override
│       │   ├── ui_wifi_setup.h/.cpp        <- WiFi scan + connect UI
│       │   ├── ui_sync_status.h/.cpp       <- Sync log viewer
│       │   └── ui_activation.h/.cpp        <- Device registration screen
│       ├── splash/
│       │   └── manpro_splash.h      <- Boot animation (calls UIManager::loadInitialScreen on done)
│       └── assets/                  <- Compiled LVGL C arrays (icons, fonts)
│
├── wroom_firmware/
│   ├── wroom_firmware.ino           <- setup() + loop(); task scheduler
│   └── src/
│       ├── config.h                 <- Pin defs, DEVICE_ID, API_BASE_URL, ESP-NOW constants
│       ├── certs.h                  <- GTS Root R4 CA cert for HTTPS
│       ├── employee_db.h/.cpp       <- Slot->emp_id mapping (stored in NVS via Preferences)
│       ├── comms.h/.cpp             <- ESP-NOW send/recv, ring buffer, CROWPANEL_MAC
│       ├── wifi_manager.h/.cpp      <- WiFi connect, scan, auto-reconnect
│       ├── time_manager.h/.cpp      <- DS3231 RTC + NTP, three-tier fallback
│       ├── fingerprint_manager.h/.cpp <- AS608 sensor: match, enroll, delete
│       ├── activation.h/.cpp        <- POST /api/devices/activate -> store token
│       ├── sync_manager.h/.cpp      <- Employee sync state machine (see Section 4)
│       ├── sync_protocol.h          <- Shared packet structs (MUST match CrowPanel copy)
│       └── command_handler.h/.cpp   <- Dispatches Serial + ESP-NOW JSON commands
│
├── assets/
│   ├── logo/                        <- Source ManPro logo PNGs
│   ├── icons/                       <- Source icon PNGs (converted to C arrays by tools/)
│   └── boot_anim/                   <- Boot animation frames
│
├── crowpanel_firmware/tools/
│   ├── convert_img.py               <- Converts PNG -> LVGL C array
│   ├── run_conv.py                  <- Batch runner for convert_img.py
│   └── admin_mock.py                <- Lightweight mock for admin API calls (dev only, incomplete)
│
└── ui_reference/                    <- Color palette & design reference screenshots
```

---

## 3. Critical Configuration — Things to Update Per Unit

These values are hardcoded and **must be changed** before flashing a new physical unit. The README has the full flash order; this is just the change checklist.

| File | Value to Change | Notes |
|---|---|---|
| `wroom_firmware/src/comms.cpp` | `CROWPANEL_MAC[6]` | Station MAC of the paired CrowPanel. Flash CrowPanel first, read `[BOOT] CP MAC:` from Serial. |
| `crowpanel_firmware/src/core/comm_manager.h` | `WROOM_MAC[6]` | Station MAC of the paired WROOM. Flash WROOM first, read `[BOOT] WROOM MAC:` from Serial. |
| `wroom_firmware/src/config.h` | `DEVICE_ID` | Must be unique per registered unit. Must match `DEVICE_ID_HARDCODED` below. |
| `crowpanel_firmware/src/core/data_manager.h` | `DEVICE_ID_HARDCODED` | Must match `DEVICE_ID` above. Both boards send this to the backend. |

> **NOTE:** `DEVICE_ID` in `config.h` currently reads `"F001-2608-6AEC-ON92"` and `DEVICE_ID_HARDCODED` in `data_manager.h` has a comment: *"Make this dynamic, get the versioning pattern from senior dev."*
> The versioning/ID generation scheme has not been defined yet. Until then, manually assign IDs and keep them in sync across both files.

---

## 4. Employee Sync Pipeline (Most Complex Subsystem)

The employee list lives on the ManPro backend. Here is how it gets onto both boards:

```
ManPro Backend API
     |
     |  GET /api/devices/employees  (HTTPS, Bearer token)
     v
WROOM: SyncManager::fetchEmployeesFromApi()
     |  Parses JSON -> EmployeeSync[] buffer
     |  Supports 5 wrapper key styles: "employees", "data", "result", "payload", bare array
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
     SYNC_RESULT (OK or NACK with missing chunk list)
     v
WROOM: SyncManager::handleIncomingPacket()
     |  OK -> SYNC_STATE_IDLE
     |  NACK -> resend missing chunks -> re-send SYNC_END
```

### SyncManager State Machine (WROOM)

States in order:
`IDLE -> FETCH_WIFI -> SET_ESPNOW_CHANNEL -> SEND_PING -> SEND_SYNC_START -> SEND_CHUNKS -> SEND_SYNC_END -> AWAIT_SYNC_RESULT`

- Failure at any state triggers `failToFastRetry()` — enters `FAST_RETRY_MODE` (retries every 5 minutes).
- Sync is triggered: (a) manually via `SYNC_EMP` ESP-NOW command, or (b) automatically at the top of every hour when NTP time is valid.
- **BUG-10 fix** (already applied): `s_syncEndResendCount` is a separate counter from `s_retryCount` because `setState()` resets `s_retryCount`. Using a unified counter made the 3-retry limit on SYNC_END resends unreachable.
- **EDGE-07 fix** (already applied): `sync_id` falls back to a monotonic counter if `time(NULL)` returns a pre-NTP epoch value (< 1,000,000,000), preventing stale-packet acceptance.

### sync_protocol.h — The Contract

Both firmwares include a **copy** of `sync_protocol.h`. They must be **identical**. If you change a struct or add a packet type, update both copies. Struct sizes are enforced at compile time via `static_assert`.

| Packet | Direction | Purpose |
|---|---|---|
| `SYNC_PING` / `SYNC_PONG` | WROOM<->CrowPanel | Confirm ESP-NOW link before starting |
| `SYNC_START` | WROOM->CrowPanel | Declares `sync_id`, total chunks, total bytes |
| `SYNC_DATA` | WROOM->CrowPanel | One chunk of raw `EmployeeSync[]` bytes |
| `SYNC_CHUNK_ACK` | CrowPanel->WROOM | Confirms receipt of a specific chunk |
| `SYNC_END` | WROOM->CrowPanel | Signals completion, carries CRC32 |
| `SYNC_RESULT` | CrowPanel->WROOM | OK or NACK with list of missing chunk indices |

`MAX_SYNC_EMPLOYEES = 127` matches the AS608 slot limit. Do not increase it without also expanding the AS608 slot pool.
`MAX_CHUNK_SIZE = 200` bytes. The largest transmitted packet must stay <= 250 bytes (ESP-NOW hard limit).

---

## 5. ESP-NOW Message Types (JSON Layer)

Most inter-node communication uses **JSON strings** over ESP-NOW. Only the employee sync uses binary packets (see Section 4). The JSON message type field is `"type"`. The CrowPanel's `CommManager::dispatchJson()` routes incoming messages.

| `type` | Direction | Description |
|---|---|---|
| `MATCH` | WROOM->CrowPanel | Fingerprint matched: `{emp_id, name, ts, confidence, slot, action_type}` |
| `ENROLL_RESULT` | WROOM->CrowPanel | Result of enrolment: `{success, emp_id, finger_index, slot, msg}` |
| `EMP_SYNC_START` | CrowPanel->WROOM | Begin employee sync (WROOM triggers SyncManager) |
| `EMP_SYNC_FAIL` | WROOM->CrowPanel | Sync failed, stop UI loading spinner |
| `ACTIVATION_STATUS` | WROOM->CrowPanel | Result of activation API call: `{activated, token}` |
| `NTP_STATUS` | WROOM->CrowPanel | Timestamp string for CrowPanel RTC: `{time_str}` |
| `WIFI_STATUS` | WROOM->CrowPanel | WiFi state update: `{connected, ssid, ip}` |
| `CHANNEL_HOP` | WROOM->CrowPanel | AP changed channel; CrowPanel must re-init ESP-NOW |
| `WROOM_ONLINE` | WROOM->CrowPanel | Heartbeat on boot — triggers "WROOM online" toast |
| `GHOST_LOGIN` | WROOM->CrowPanel | Dev backdoor — bypasses scanner, opens Main Menu |
| `NUKE_USERS` | WROOM->CrowPanel | Dev backdoor — erases all fingerprint slots except slot 1 |
| `DEBUG_COMMS` | WROOM->CrowPanel | Dev backdoor — toggles ESP-NOW ping/pong debug output |

---

## 6. Persistent Storage Map

### WROOM — NVS (Preferences namespace `"biometrics"`)

| Key | Type | Content |
|---|---|---|
| `activated` | bool | Whether device has been activated |
| `device_token` | String | Bearer token from backend |
| `wifi_ssid` | String | Saved WiFi network |
| `wifi_pass` | String | Saved WiFi password |
| `emp_count` | int | Number of entries in slot map |
| `emp_X_id` | String | Employee ID for slot X |
| `emp_X_fi` | int | Finger index for slot X |

The employee-slot mapping is managed by `employee_db.h/.cpp`. The AS608 sensor is the physical store for templates — the NVS only holds the slot->emp_id lookup.

### CrowPanel — LittleFS

| File | Format | Content |
|---|---|---|
| `/employees.json` | JSON array | Employee list (name, dept, job_title, branch, fp_enrolled, enrolled_fingers) |
| `/config.json` | JSON object | Device settings (brightness, timeout, volume, adminPin, deviceName, activated, token) |
| `/wifi.json` | JSON array (up to 5) | Saved WiFi credentials |
| `/attendance.json` | JSON array (up to 200) | Local attendance log (name, time_str, action_type, synced, confidence, slot) |
| `/fp_state.json` | JSON object | Enrollment bitmask per emp_id (persisted separately so it survives sync overwrites) |
| `/sync_log.jsonl` | JSON Lines | Last 5 sync event messages with timestamps |

### CrowPanel — SD Card (`/templates/`)

Raw 512-byte binary fingerprint templates, stored as `/templates/<emp_id>_<finger_index>.bin`. This is the **Deep Storage** layer. The WROOM's AS608 is L1 cache (127 slots); the SD card is the primary store for large employee sets. Templates are loaded from SD and pushed to WROOM on demand during enrolment or sync.

---

## 7. Open Work & Known Issues

### 7.1 OTA Firmware Updates (Not Implemented)

A full design has been written up in `firmware_update_plan.md`. **Nothing has been coded yet.** The plan calls for:

- `ota_manager.h/.cpp` on both nodes (GitHub Releases as host, `HTTPUpdate` for flashing)
- A `firmware_manifest.json` at the repo root
- Partition scheme changes on both boards (**requires one final USB flash**)
- A `ui_ota.h/.cpp` progress screen on the CrowPanel

> **CAUTION:** The CrowPanel currently uses `Huge APP (3MB No OTA/1MB SPIFFS)`. Switching to an OTA-capable scheme wipes the partition table. Read the Open Questions section in `firmware_update_plan.md` before starting — the physical flash size of the CrowPanel board needs to be confirmed before choosing the new partition scheme.

### 7.2 Device ID Versioning Pattern (Undefined)

`DEVICE_ID` in `config.h` and `DEVICE_ID_HARDCODED` in `data_manager.h` are currently hardcoded strings. The comment in `data_manager.h` says: *"Make this dynamic, get the versioning pattern from senior dev."* Until this is clarified, IDs are assigned manually and must be kept in sync across both files for each physical unit.

### 7.3 `admin_mock.py` (tools/) — Partial Implementation

`crowpanel_firmware/tools/admin_mock.py` is a lightweight mock for admin API calls. It was written for local dev but is **incomplete** — it does not cover all endpoints. Use the main mock server instead (documented in README "Mock Server Usage" section).

### 7.4 Attendance Upload Race Condition (Low Priority)

`DataManager::uploadPendingLogs()` runs attendance POSTs on a FreeRTOS background task (`attendanceUploadTask`). The flag `s_uploadInProgress` (an `std::atomic<bool>`) guards against double-uploads, but there is no retry queue — if the background task fails mid-batch, those logs are silently left as `synced=false` and will be retried on the next upload cycle. This is acceptable for now but should be replaced with a proper retry queue before production.

### 7.5 sync_protocol.h Dual-Copy Maintenance Risk

Both `wroom_firmware/src/sync_protocol.h` and `crowpanel_firmware/src/core/sync_protocol.h` must stay byte-for-byte identical. There is no automated check enforcing this. **Any edit to either copy must be manually mirrored to the other.** Consider symlinking or a pre-build script if the file changes frequently.

### 7.6 WiFi Channel Lock Requirement

ESP-NOW requires both nodes to be on the same WiFi channel. The router **must** be locked to a fixed channel (default: channel 1 per `ESPNOW_CHANNEL` in `config.h`). If the router uses auto-channel, the WROOM sends a `CHANNEL_HOP` notification and both nodes re-negotiate, but this is not fully hardened. Test with a fixed channel.

### 7.7 No Fingerprint Eviction Policy

There is **no eviction policy** for the AS608 cache when all 127 slots fill up. If you enrol more than 127 fingers, the enrolment will fail. The firmware does not currently implement any LRU or frequency-based eviction. This must be addressed before deploying at a site with more than 127 enrolled fingers.

---

## 8. Subsystem Deep-Dives

### 8.1 CrowPanel Boot Sequence

```
setup():
  DataManager::begin()         -> mounts LittleFS, loads all JSON files
  CommManager::begin()         -> inits ESP-NOW, registers onEspNowRecv callback
  lcd.init()                   -> LovyanGFX + GPIO2 backlight reset
  lv_init()                    -> allocate PSRAM render buffers (2x 800x240 px)
  manpro_show_splash(callback) -> plays boot animation, then calls UIManager::loadInitialScreen
  UIManager::begin()           -> builds all LVGL screen objects in background

loop():
  lv_tick_inc() + lv_task_handler()   -> LVGL rendering tick
  CommManager::process()              -> drains ring buffer, dispatches JSON messages
  Screen timeout check                -> dims backlight after inactivity
```

`UIManager::loadInitialScreen` decides which screen to show after the splash:
Activation screen (if not activated) -> WiFi setup (if no credentials) -> Idle screen (normal operation).

### 8.2 WROOM Boot Sequence

```
setup():
  WiFiManager::begin()         -> attempts to connect to saved network
  FingerprintManager::init()   -> opens AS608 on UART1, counts stored templates
  TimeManager::init()          -> inits DS3231 on I2C, syncs NTP if WiFi available
  Comms::begin()               -> inits ESP-NOW, registers CROWPANEL_MAC as peer
  SyncManager::init()          -> sets state to IDLE
  AudioManager::init()         -> inits DFPlayer Mini on UART2

loop():
  CommandHandler::poll()       -> polls AS608 T-OUT pin, reads Serial commands
  SyncManager::loop()          -> drives sync state machine, hourly auto-trigger
```

### 8.3 Admin PIN System

- Default PIN: `0000` (set in `DataManager` default config).
- PIN is stored in `/config.json` (plaintext — no hashing currently).
- Lockout: 5 consecutive wrong attempts -> 60-second lockout. Tracked in `DataManager::_failedAttempts` / `_lockoutStartTime` (stored in config, survives reboots).
- PIN change is done via **Settings -> Danger Zone** (requires current PIN first).
- Admin fingerprint: A special enrolment (`emp_id = "ADMIN"`) stored on the SD card. Placing the admin finger on the idle screen opens Main Menu directly without PIN entry.

### 8.4 Smart Cache Architecture (Fingerprint Storage)

```
AS608 (WROOM) — L1 cache, 127 slots
  ^ push on enrolment
  v evict (NOT YET IMPLEMENTED — see Issue 7.7)

SD Card (CrowPanel) — Deep storage, unlimited
  /templates/<emp_id>_<finger_index>.bin

Backend — Source of truth for employee list
```

When a new fingerprint is enrolled:
1. CrowPanel UI sends `ENROLL:<emp_id>:<finger_index>` command to WROOM.
2. WROOM captures and stores template in AS608 (gets a slot number).
3. WROOM sends template bytes back to CrowPanel via `ENROLL_RESULT`.
4. CrowPanel saves the 512-byte template to SD card.
5. `employee_db` on WROOM stores the slot->emp_id mapping in NVS.

---

## 9. Development Workflow

### Local Development (Mock Server)

```bash
# From the repo root — mock_server/ directory may be separate from this repo
cd mock_server
pip install flask
python mock_server.py
```

Then change `API_BASE_URL` in `wroom_firmware/src/config.h` to your LAN IP:
```cpp
#define API_BASE_URL "http://192.168.1.50:8000"
```

Also change `API_BASE_URL` in `crowpanel_firmware/src/core/comm_manager.h`.

### Sensor-less Testing (WROOM)

Uncomment `#define MOCK_SENSOR 1` in `wroom_firmware/src/config.h`. The `GHOST_LOGIN` Serial command also bypasses the physical sensor and sends a fake match event to the CrowPanel.

### Image Asset Pipeline

Icons are stored as source PNGs in `assets/icons/`. To update or add icons:

```bash
cd crowpanel_firmware/tools
python run_conv.py    # batch converts assets/icons/ -> src/assets/*.c
```

Resulting `.c` files are `#include`d into the firmware and declared with `LV_IMG_DECLARE()`.

### Arduino IDE Settings (Quick Reference)

| Node | Board | Partition | PSRAM |
|---|---|---|---|
| CrowPanel | ESP32S3 Dev Module | Huge APP (3MB No OTA/1MB SPIFFS) | OPI PSRAM |
| WROOM | ESP32 Dev Module | Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS) | Disabled |

**WROOM only:** Enable **"Erase All Flash Before Sketch Upload"** — the Preferences (NVS) partition layout changed during development and old data causes silent boot failures.

---

## 10. Testing Checklist

No automated tests exist. Verify the following manually before any release:

- [ ] Cold boot (power off) -> splash animation plays -> idle screen loads with correct time
- [ ] Fingerprint time-in -> result screen shows correct name and "Time In" -> log entry appears in Logs screen
- [ ] Fingerprint time-out -> "Time Out" result
- [ ] Unrecognized finger -> "Not Recognized" result -> no log entry written
- [ ] Employee sync triggered from Settings -> `[SYNC]` log messages in WROOM Serial -> CrowPanel Sync Status screen shows updated list
- [ ] Admin PIN entry: correct PIN opens Main Menu; wrong PIN x5 triggers 60s lockout
- [ ] WiFi disconnect during sync -> WROOM enters FAST_RETRY_MODE -> retries after 5 minutes
- [ ] Factory reset on CrowPanel -> all data wiped -> returns to WiFi setup screen
- [ ] Factory reset on WROOM (hold GPIO14 for 5s) -> fingerprints wiped -> NVS cleared
- [ ] Channel hop: change router channel -> both nodes re-negotiate within ~10s

---

## 11. Backend API Endpoints Used

| Method | Endpoint | Caller | Purpose |
|---|---|---|---|
| `POST` | `/api/devices/activate` | WROOM `activation.cpp` | Register device, receive bearer token |
| `GET` | `/api/devices/employees` | WROOM `sync_manager.cpp` | Fetch employee list for binary sync to CrowPanel |
| `POST` | `/api/attendance` | CrowPanel `data_manager.cpp` | Upload attendance log entries |

Authentication: `Authorization: Bearer <device_token>` header. Token is obtained during activation and stored in WROOM NVS and CrowPanel `/config.json`.

> **NOTE:** `sync_manager.cpp` tries five different JSON wrapper keys (`employees`, `data`, `result`, `payload`, bare array) to handle backend response format variations. If the backend schema changes, check `SyncManager::fetchEmployeesFromApi()` in `sync_manager.cpp` first.

---

## 12. Useful Serial Commands (WROOM, 115200 baud)

| Command | Effect |
|---|---|
| `ENROLL:<emp_id>:<finger_index>` | Begin fingerprint enrolment (e.g. `ENROLL:42:0`) |
| `DELETE:<emp_id>:<finger_index>` | Delete a stored template (e.g. `DELETE:42:0`) |
| `RESET` | Reboot WROOM |
| `TEST_HW` | Hardware test: beeps buzzer 3x and plays track 1 on DFPlayer |
| `GHOST_LOGIN` | Dev: skips scanner, sends fake MATCH to CrowPanel -> opens Main Menu |
| `NUKE_USERS` | Dev: wipes all AS608 slots except slot 1 |
| `DEBUG_COMMS` | Dev: toggles ESP-NOW ping/pong debug output |

---

## 13. Key Files to Know for Each Area

| Area | Primary Files |
|---|---|
| Everything persistent (CrowPanel) | `data_manager.h` / `data_manager.cpp` |
| ESP-NOW receive + JSON dispatch (CrowPanel) | `comm_manager.h` / `comm_manager.cpp` |
| Employee sync (WROOM sender) | `sync_manager.h` / `sync_manager.cpp` |
| Employee sync (CrowPanel receiver) | `sync_receiver.h` / `sync_receiver.cpp` |
| Binary packet definitions (both) | `sync_protocol.h` — **keep both copies identical** |
| Fingerprint hardware (WROOM) | `fingerprint_manager.h` / `fingerprint_manager.cpp` |
| All UI screens (CrowPanel) | `src/ui/ui_*.h` / `src/ui/ui_*.cpp` |
| Screen navigation / routing | `ui_manager.h` / `ui_manager.cpp` |
| Hardware config + pins | `wroom_firmware/src/config.h` |
| OTA update design (unimplemented) | `firmware_update_plan.md` |
