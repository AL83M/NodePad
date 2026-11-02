# NodePad v2.8 Beta — public testing

**Status:** Public beta (not the stable v3 release). The author has installed and booted v2.8 Beta successfully on an **original M5Stack Cardputer**. Full regression testing and compatibility on **Cardputer ADV have not yet been verified**.

## What's new

- **Local Wi-Fi setup page** hosted by the Cardputer (System → P), accessible at `http://192.168.4.1` after joining the temporary `NodePad-XXXX` Wi-Fi network with the password displayed on screen.
- **Radio configuration over BLE** for a connected, ready MeshCore Companion, including radio settings and region-related controls. Review settings carefully before applying; keep radio frequencies and power within applicable rules.
- **Companion naming and MeshCore region settings** in the beta configuration workflow.
- **Credits** screen (System → C).
- Experimental single ESP32-S3 firmware candidate for original Cardputer and **ADV testing**. ADV support is **not yet confirmed on physical hardware**.

The main menu has intentionally remained similar to previous releases. On the Cardputer, check **System → Version** for `V2.8 BETA`.

## Get the beta

- **Verified build:** [NodePad v2.8 Beta — GitHub Actions](https://github.com/AL83M/NodePad/actions/workflows/build-beta-28.yml) (select the latest successful build for the intended revision; the author's validated code fix is commit [`5c28ad8`](https://github.com/AL83M/NodePad/commit/5c28ad82c6759af9197d883b79ef2f4566503612)).
- Download the artifact named `NodePad-v2.8-Beta-unified-ESP32S3` and extract its ZIP. GitHub Actions artifacts may require sign-in and expire.
- **Source:** [v2.8 beta branch](https://github.com/AL83M/NodePad/tree/beta/v2.8-cardputer-adv-web-credits).

For a full USB flash, use `NodePad_v2.8_beta_factory_0x0000.bin` at flash address `0x0000` with an ESP32-S3 flash tool; **do not write the application-only BIN at address zero**. The application-only image, `NodePad_v2.8_beta_app_0x10000.bin`, requires the appropriate existing partition layout and an update tool that writes at `0x10000`. A reliable low baud rate such as 115200 is recommended when troubleshooting. Back up data, avoid erasing NVS unnecessarily, and refer to [flashing instructions](FLASHING.md).

**Note:** The [website installer](https://al83m.github.io/NodePad/) still offers the previous v2.2.6 build, not this new beta. Do not use it expecting v2.8.

## First test

1. Boot NodePad and check `System → Version` shows `V2.8 BETA`.
2. Connect your separately configured MeshCore BLE Companion. If necessary, use the official MeshCore app to set it up before pairing with NodePad.
3. Open `System → P`, join the displayed local Wi-Fi network on a phone or computer, and visit `http://192.168.4.1`.
4. Review the page's detected values. **Do not change radio settings unless you know the correct regional and network configuration.**
5. Return to System and press `C` to check Credits.

## Important limitations

- This is **test software**. The original Cardputer has been tested by the author; ADV and wider radio/hardware combinations need independent validation.
- A successful GitHub Actions build does not prove that every feature or device configuration works.
- The local setup page is intended for local use while its access point is active. Keep its Wi-Fi password and any pairing information private.
- NodePad is an **independent community project**, not affiliated with MeshCore or M5Stack.
- The project has not yet selected a source code license. Public availability does not by itself grant third-party redistribution or modification rights.

Please report problems through [GitHub Issues](https://github.com/AL83M/NodePad/issues), including device type (original or ADV), firmware revision, steps to reproduce, and logs with personal identifiers, tokens and PINs removed.

## Suggested GitHub pre-release

- **Tag:** `v2.8.0-beta.1`
- **Title:** `NodePad v2.8 Beta 1 — Public Testing`
- **Target:** `beta/v2.8-cardputer-adv-web-credits`
- **Mark as pre-release** and attach the tested successful-build ZIP (or its two BIN files) so downloads are available without depending on expiring GitHub Actions artifacts.
