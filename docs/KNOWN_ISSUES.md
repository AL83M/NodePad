# Known limitations — v2.2.6 Beta

- Limited test coverage across Cardputer revisions and MeshCore BLE Companion firmware versions.
- More long-running regression tests needed for reconnects, notifications, private chats and repeater tools.
- Paired Companion addresses and PINs are saved with ESP32 Preferences/NVS; no guarantee of physical extraction resistance.
- PIN may be visible locally on the Cardputer during entry.
- Device clock timezone is a fixed manual UTC offset; daylight saving time is not automatic.
- No validated prebuilt `.bin`, OTA updater or browser-based web flasher currently included.
- GitHub Actions build is included but needs its first successful verified run.
- Full dependency versions are not locked or independently reproducible.
- LoRa link status may be stale or missing even when BLE remains connected.
- No automated hardware tests supplied.

Please submit reproducible issues with version numbers, exact display errors and **redacted** logs.
