# Over-The-Air (OTA) Firmware Updates — Like Windows Update

Both the WROOM and CrowPanel will autonomously check for new firmware over the internet, download it, and apply it — no USB cable ever needed again after the one migration flash.

---

## How It Works (the "Windows Update" model)

```
  Your PC                     GitHub Releases                  Devices (anywhere)
  ──────                     ───────────────                  ──────────────────
  Build .bin files  ──push──▶  firmware_manifest.json         WROOM & CrowPanel
  Tag a release              + wroom_v1.1.0.bin               check manifest on boot
                             + crowpanel_v1.1.0.bin    ◀──────  and every 6 hours
                                                               ↓
                                                         If version newer → download
                                                         .bin over HTTPS → flash self
                                                         → reboot into new firmware
```

1. You compile both firmwares in Arduino IDE and export the `.bin` files.
2. You upload them to a **GitHub Release** (free, public CDN, HTTPS by default) and update a small `firmware_manifest.json` file with the new version string and download URLs.
3. Each device independently polls the manifest URL on every boot and every 6 hours while running. If the remote version is newer than the running version, it downloads and flashes.
4. The update runs silently in the background on the WROOM. The CrowPanel shows a brief "Updating firmware…" overlay and reboots.

---

## Open Questions

> [!IMPORTANT]
> **CrowPanel flash size** — The CrowPanel currently uses a `Huge APP (3MB No OTA)` partition, meaning it occupies a 3 MB app slot. OTA requires **two equal app slots** (the running firmware + the new download slot). On a 4 MB chip, two 3 MB slots don't fit. We have two options:
> - **Option A (Recommended):** If the Elecrow CrowPanel 5" board actually ships with **16 MB flash** (many do — the `4MB` in Arduino settings is just a conservative default), we can use a `16MB` custom partition with two 6 MB app slots and a healthy SPIFFS. This is the cleanest solution.
> - **Option B:** If it truly has 4 MB flash, we must reduce the CrowPanel firmware's footprint to fit two ~1.9 MB OTA slots. This may require stripping some compiled icon assets from flash.
>
> **Action required before implementation:** In Arduino IDE with the CrowPanel selected, go to `Sketch → Export Compiled Binary` and report the resulting `.bin` file size. If it's under ~1.7 MB, 4 MB OTA works. If it's larger, we need to know the physical flash size.

> [!WARNING]
> **One final USB flash required.** Switching partition schemes wipes the existing partition table. Both boards need one last USB flash to migrate to the OTA-capable partition. After that, all future updates are wireless.

> [!NOTE]
> **SPIFFS impact on WROOM:** The WROOM uses `Preferences` (NVS), not SPIFFS directly, so shrinking SPIFFS from 1.5 MB to 190 KB is safe. All WiFi credentials, fingerprint slot mappings, and device tokens live in NVS.

---

## Proposed Changes

### Hosting (GitHub Releases — no server cost)

#### [NEW] `firmware_manifest.json` (hosted in GitHub repo root, served via GitHub raw URL)

```json
{
  "wroom": {
    "version": "1.1.0",
    "url": "https://github.com/YOUR_USER/BIOMETRICSMANPRO/releases/download/v1.1.0/wroom_v1.1.0.bin"
  },
  "crowpanel": {
    "version": "1.1.0",
    "url": "https://github.com/YOUR_USER/BIOMETRICSMANPRO/releases/download/v1.1.0/crowpanel_v1.1.0.bin"
  }
}
```

The manifest URL (raw GitHub) will be `https://raw.githubusercontent.com/YOUR_USER/BIOMETRICSMANPRO/main/firmware_manifest.json`.

---

### WROOM Firmware

#### [MODIFY] `wroom_firmware/src/config.h`
- Add `#define FIRMWARE_VERSION "1.0.0"` — version string compared against manifest.
- Add `#define OTA_MANIFEST_URL "https://raw.githubusercontent.com/..."` — the manifest URL.
- Add `#define OTA_CHECK_INTERVAL_MS (6UL * 3600UL * 1000UL)` — check every 6 hours.

#### [NEW] `wroom_firmware/src/ota_manager.h`
Declares:
- `void otaManagerInit()` — called from `setup()` after WiFi init.
- `void otaManagerProcess()` — called from `loop()`, drives the non-blocking state machine.
- Internal: version comparison, manifest fetch, `HTTPUpdate` flash.

#### [NEW] `wroom_firmware/src/ota_manager.cpp`
State machine with states: `IDLE → CHECK_VERSION → DOWNLOADING → DONE/FAILED`.
- Uses `HTTPUpdate` from the ESP32 Arduino core (`<HTTPUpdate.h>`).
- Fetches manifest with `WiFiClientSecure` + `GTS_ROOT_R4` cert (already in `certs.h`) — no new cert needed since GitHub uses a Google-signed cert (or we add the GitHub cert).
- Compares `FIRMWARE_VERSION` string semver-style against the manifest version.
- On success: calls `ESP.restart()`. On failure: logs and retries at next interval.
- Sends `{"type":"OTA_STATUS","state":"downloading","progress":50}` etc. to CrowPanel over ESP-NOW so the display can show a progress indicator.

#### [MODIFY] `wroom_firmware/src/command_handler.cpp`
- Handle `OTA_CHECK` command from CrowPanel (user triggers manual update from Settings screen).
- Handle `OTA_STATUS_REQ` — replies with current version.

#### [MODIFY] `wroom_firmware/wroom_firmware.ino`
- Call `otaManagerInit()` in `setup()`.
- Call `otaManagerProcess()` in `loop()`.

#### Board Settings Change (Arduino IDE)
| Setting | Old | New |
|---|---|---|
| Partition Scheme | Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS) | **Min SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** |

---

### CrowPanel Firmware

#### [NEW] `crowpanel_firmware/src/core/ota_manager.h` / `ota_manager.cpp`
Same pattern as WROOM side:
- Checks manifest on boot and every 6 hours.
- Uses `HTTPUpdate` + `WiFiClientSecure`.
- Shows `uiOtaProgress()` overlay during download.
- Sends `OTA_CHECK` command to WROOM over ESP-NOW after completing its own update (so both boards update in sequence, not simultaneously).

#### [NEW] `crowpanel_firmware/src/ui/ui_ota.h` / `ui_ota.cpp`
A lightweight LVGL overlay screen:
- "Firmware update available — v1.1.0" with an **Update Now** / **Later** choice.
- Progress bar during download (0–100%).
- "Rebooting…" message on success.

#### [MODIFY] `crowpanel_firmware/src/ui/ui_settings.cpp`
- Add a **"Firmware Update"** entry to the Settings menu.
- Shows current version, checks for update on tap, shows `ui_ota` overlay.

#### [MODIFY] `crowpanel_firmware/src/core/comm_manager.cpp`
- Handle incoming `OTA_STATUS` messages from WROOM (forward to `uiOtaProgress`).
- Dispatch `OTA_CHECK` to WROOM when user triggers update from Settings.

#### [MODIFY] `crowpanel_firmware/src/core/data_manager.h`
- Add `static String getFirmwareVersion()` — returns `CROWPANEL_VERSION`.

#### [MODIFY] `crowpanel_firmware/crowpanel_firmware.ino`
- Call OTA init in `setup()`, process in `loop()`.

#### Board Settings Change (Arduino IDE)
| Setting | Old | New |
|---|---|---|
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) | **TBD — depends on binary size** (see Open Questions above) |

---

### `sync_protocol.h` (both firmwares)

#### [MODIFY] Add new ESP-NOW message types:
```cpp
// OTA coordination between nodes
#define MSG_OTA_STATUS    "OTA_STATUS"   // WROOM → CrowPanel: {state, progress, version}
#define MSG_OTA_CHECK     "OTA_CHECK"    // CrowPanel → WROOM: trigger manual OTA check
```

---

### `firmware_manifest.json` (new file at repo root)

The manifest file you will edit whenever you release a new firmware version.

---

## Update Release Workflow (your process going forward)

```
1. Edit firmware, bump FIRMWARE_VERSION in config.h / define
2. Arduino IDE → Sketch → Export Compiled Binary
3. Create a GitHub Release tagged "v1.x.x"
4. Upload wroom_vX.x.x.bin and crowpanel_vX.x.x.bin as release assets
5. Edit firmware_manifest.json in the repo with the new version + URLs
6. Commit and push
   ↓
   Devices will pick it up within 6 hours (or immediately on next boot)
```

---

## Verification Plan

### Automated (after implementation)
- Compile both firmwares in Arduino IDE — confirm no errors with new OTA modules added.
- Export `.bin` files and confirm sizes fit within chosen partition scheme.

### Manual Verification
1. Flash both boards via USB one final time with the OTA partition scheme and new firmware.
2. Set `FIRMWARE_VERSION` to `"0.9.9"` and upload a `"1.0.0"` manifest pointing to a test `.bin`.
3. Confirm WROOM Serial shows: `[OTA] Manifest fetched. Remote: 1.0.0, Local: 0.9.9 — update available.` → `[OTA] Downloading...` → `[OTA] Success. Rebooting.`
4. Confirm CrowPanel shows the update progress overlay during the WROOM's update.
5. Confirm CrowPanel self-updates successfully from the same manifest.
6. Confirm that after reboot, both boards report the new version.
