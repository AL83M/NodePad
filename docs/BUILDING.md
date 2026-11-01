# Building NodePad v2.2.6

NodePad is an Arduino sketch for the **original M5Stack Cardputer** (ESP32-S3).

## Build online

Run **Actions → Build NodePad firmware → Run workflow**. The GitHub runner installs Arduino CLI, ESP32 board definitions, M5Cardputer and NimBLE-Arduino, compiles the sketch, and uploads downloadable firmware binaries if successful.

This workflow is experimental and its build outcome has not yet been verified. Inspect Actions errors if it fails.

## Dependencies

- ESP32 Arduino board package
- `M5Cardputer` (initially developed with 1.1.1)
- `NimBLE-Arduino` 2.x
- M5GFX / M5Unified (M5Cardputer dependencies)

The full original dependency lockfile is unavailable; some newer combinations may need adjustment.

## Manual compilation

1. Open `firmware/NodePad_V2_2_6/NodePad_V2_2_6.ino`.
2. Install the ESP32 board definitions and dependencies.
3. Select a matching original Cardputer ESP32-S3 board configuration.
4. Compile and upload with Arduino IDE or Arduino CLI.
5. Use a separate MeshCore Companion BLE radio for the LoRa link.

## Settings

`DEFAULT_MESHCORE_BLE_PIN = 123456` is an initial suggestion, **not a universal PIN**. Enter the actual configured PIN when pairing; the confirmed radio PIN is remembered locally.

`SOFT_NOTIFICATION_VOLUME` controls alert loudness. `M` on System toggles mute. `LOCAL_UTC_OFFSET_SECONDS` is a manually set UTC offset and does not automatically change for daylight saving time.

Do not publish private device identifiers from serial logs.
