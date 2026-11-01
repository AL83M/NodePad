# Known limitations — v2.2.6 Beta

- Limited test coverage across Cardputer revisions and MeshCore BLE Companion firmware versions.
- More long-running regression tests needed for reconnects, notifications, private chats and repeater tools.
- Paired Companion addresses and PINs are saved with ESP32 Preferences/NVS; no guarantee of physical extraction resistance.
- PIN may be visible locally on the Cardputer during entry.
- Device clock timezone is a fixed manual UTC offset; daylight saving time is not automatic.
- The web installer has been successfully tested on the original Cardputer by the author; other device revisions and setups need testing. No OTA updater is available.
- GitHub Actions compilation succeeds, but long-term reliability and reproducibility across dependency versions still need testing.
- **Companion setup and configuration are not supported in NodePad**. Use the official MeshCore app first for the Companion's mesh/radio settings and channels.
- Full dependency versions are not locked or independently reproducible.
- LoRa link status may be stale or missing even when BLE remains connected.
- No automated hardware tests supplied.

Please submit reproducible issues with version numbers, exact display errors and **redacted** logs.
