Manpro — Biometric Fingerprint Attendance/Access Device

KiCad hardware project for Manpro, a fingerprint-based attendance/access control device. The design is split into two boards — a project (logic) board and a power board — connected via JST connectors carrying GND / +5V / +3.3V.

Project Files
File	Purpose
JRDraws.kicad_pro	KiCad project file
JRDraws.kicad_sch	Main schematic (project board + power board)
JRDraws.kicad_pcb	PCB layout
JRDraws.kicad_prl	Local project settings
Dpad-button-controller.kicad_sch	Sub-schematic for the 5-button D-pad nav controller
fp-lib-table	Footprint library table
Hardware Overview

Main controller (project board)

ESP32 DevKit (38-pin) — main MCU
AS608 fingerprint sensor — RX → G26, TX → G27, T-OUT → G34
DFPlayer Mini (MH2024K) audio module — on G16/G17 via a 5V↔3.3V logic level converter
DS3231 RTC
CrowPanel Node (separate ESP32-S3) touchscreen UI
Buzzer/speaker — on G13
5 navigation push buttons (D-pad sub-board) + a dedicated hardware reset button tied to EN

Power board

USB-C receptacle (power only, 6-pin) input
MCP73871 (QFN-20) LiPo charge/power-path management IC
MT3608 boost converter module
3.7V 5000 mAh LiPo battery
Fuse + protection/flag circuitry

Passives/misc: several resistors and capacitors, 3 LEDs, PWR flag and GND/+5V/+3.3V power symbols throughout.

✔️AS608 TX/RX swap fixed
✔️Decided on a single AS608 sensor (not dual)
✔️ \5 nav buttons + hardware reset button wired correctly
✔️DFPlayer, AS608, and buzzer pins reassigned per updated reference doc
