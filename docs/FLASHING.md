# Flashing NodePad v2.2.6

**Beta warning:** the GitHub-built image has not yet been independently compiled and hardware-tested. Use the **original ESP32-S3 Cardputer** only.

## Download a binary

1. Visit the repository **Actions** tab.
2. Open **Build NodePad firmware** and select **Run workflow**.
3. After a successful build, download the `NodePad-v2.2.6-binaries` artifact.
4. Extract the ZIP.

Possible outputs:

- `NodePad_v2.2.6_factory_0x0000.bin` — merged image if produced; flash at address `0x0000`.
- `NodePad_v2.2.6_app_0x10000.bin` — app-only image; flash at `0x10000` **only** with an already compatible bootloader and partition map.
- `SHA256SUMS.txt` — checksums for the included files.

**Never flash an app-only binary at address `0x0000`.** Use a flashing tool that supports the ESP32-S3, verify the correct device and flash address, and back up important existing firmware/configuration before replacement.

NodePad requires a **separate MeshCore Companion BLE radio** to provide the LoRa connection.

After building and testing on hardware, attach validated binaries to a GitHub release tagged `v2.2.6-beta`, accompanied by compatibility notes. A successful compile alone is not enough to claim the firmware has been tested.
