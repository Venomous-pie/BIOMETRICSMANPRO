# Agent Behavioral Rules

## 1. PING/PONG Communication Protocol
**CRITICAL CONSTRAINT:** Do NOT modify the UART PING/PONG heartbeat logic between the WROOM (`wroom_firmware.ino`) and the CrowPanel (`comm_manager.cpp`) without EXPLICIT, prior user consent. 

The bidirectional UART communication is extremely sensitive. Any unauthorized changes to the timing, JSON structure, or serial handling of `{"type":"PING"}` and `{"type":"PONG"}` can cause the entire system to desync, hang, or fall apart. 

If you suspect an issue relates to the communication layer, ask the user for permission before attempting any edits to:
- `wroom_firmware.ino` (specifically the `lastPingMs` loop logic and the `handleCmd` PONG parser)
- `comm_manager.cpp` (specifically `dispatchJson` PING/PONG handling)
- `Serial` / `HardwareSerial` buffer sizes and timeout configurations

Always err on the side of caution and leave the communication layer untouched unless specifically instructed otherwise.

## 2. Display and UI Configurations
**CRITICAL CONSTRAINT:** Do NOT modify the display driver configurations in `display_driver.h` without EXPLICIT, prior user consent.

The LovyanGFX configuration for the ESP32-S3 RGB panel has been finely tuned to prevent screen tearing, jittering, and PSRAM bus starvation. Specifically, DO NOT alter the following parameters unless explicitly requested:
- `cfg.use_psram` (Must remain at `2` for double buffering)
- `cfg.freq_write` (Must remain at `12000000` to prevent PSRAM EDMA starvation)
- Any other `Bus_RGB` or `Panel_RGB` timing settings.
