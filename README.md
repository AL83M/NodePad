# NodePad

**Off-grid MeshCore companion terminal for M5Stack Cardputer**

**Version:** v2.2.6 Beta — community-tested, wider validation welcome.

**New: NodePad v2.8 public testing candidate.** The author reports successful installation on the original Cardputer. Web-based BLE Companion configuration and Credits are included; Cardputer ADV remains unverified. See the [v2.8 Beta download and test instructions](docs/BETA_2_8.md). The main web installer continues to offer v2.2.6 until a separate update is validated.

**Web installer:** [Install NodePad on your original Cardputer](https://al83m.github.io/NodePad/) — successfully tested on the original Cardputer by the author; broader testing is welcome. See [setup instructions](docs/WEB_INSTALLER.md).

NodePad turns the original M5Stack Cardputer into a small, keyboard-driven **Bluetooth Low Energy interface for a separate MeshCore Companion** radio. The Companion handles LoRa; the Cardputer provides the keyboard, display and controls.

**REQUIRED FIRST — Companion configuration:** Set up your separate **MeshCore Companion BLE** radio using the **official MeshCore app** before connecting NodePad. Configure the relevant radio/network settings and channels there. **NodePad v2.2.6 cannot configure the Companion**; it works as a client for an already configured radio. Disconnect the official app from the radio before connecting via NodePad.

This is an independent community project, not an official MeshCore, M5Stack or Seeed Studio product.

## Features

- Blue/dark menu interface and reduced display flicker
- Discovery, selection and automatic reconnection to a MeshCore BLE Companion
- Six-digit BLE PIN entered on the Cardputer, saved per radio after successful pairing
- Public channel messages and private/direct chats
- Contacts, zero-hop/flood self-advertisements
- Ping Repeaters (MeshCore path discovery) and Live Repeaters (link metrics where available)
- Cardputer battery information
- Soft message notification sound and mute toggle

## Hardware

- Original **M5Stack Cardputer (ESP32-S3)**, used for development
- A separate MeshCore **Companion BLE** radio; tested with **Heltec V3** and **XIAO Wio** companions
- USB-C data cable for flashing

A standalone repeater or USB-only Companion is **not** interchangeable with a BLE Companion. NodePad itself does **not** transmit LoRa.

## First connection (after installation)

1. **First configure** a MeshCore **Companion BLE** radio using the official MeshCore app (tested with Heltec V3 and XIAO Wio), then disconnect the app and keep the radio powered on.
2. On NodePad's **SELECT COMPANION** screen, use `W` / `S` to select the radio, then press `Enter`. The saved Companion may reconnect automatically.
3. On **PAIRING PIN**, enter that radio's actual six-digit PIN, then press `Enter`.
4. When the main menu reports **BLE SECURE / MESH ONLINE**, use the messaging and repeater features. Network reachability depends on LoRa conditions.

Press `R` on the picker to rescan, or open **System → Enter** to choose a different radio later. See the [illustrated four-step website guide](https://al83m.github.io/NodePad/#setup).

## Download and build a .bin without Arduino IDE

This repository includes an experimental GitHub Actions builder:

1. Open [Actions](../../actions) → **Build NodePad firmware**.
2. Choose **Run workflow** (or open a completed successful build).
3. Download the **NodePad-v2.2.6-binaries** artifact ZIP.
4. Extract the firmware files; follow [FLASHING.md](docs/FLASHING.md) for addresses and warnings.

**Build status:** GitHub Actions compilation succeeds, and the author has tested installation from the web installer and connection to a MeshCore Companion BLE radio on the original Cardputer. Broader hardware testing is still needed. The repository does not store a prebuilt `.bin` in source control.

## Compile from source

Open `firmware/NodePad_V2_2_6/NodePad_V2_2_6.ino` in Arduino IDE or use Arduino CLI. Requires ESP32 board support, **M5Cardputer**, **NimBLE-Arduino 2.x** and their M5GFX/M5Unified dependencies. See [BUILDING.md](docs/BUILDING.md).

Connect to a MeshCore Companion in BLE mode, select the device, and enter its six-digit BLE passkey when prompted. `123456` is merely an initial suggested default for some Companion firmware builds, not a universal key.

## Cardputer controls

| Action | Input |
| --- | --- |
| Up/down | `W`/`S` (or `Fn+;` / `Fn+.`) |
| Select | `Enter` |
| Back | `Esc` |
| Rescan BLE devices | `R` on Bluetooth screen |
| Enter BLE PIN | Type six digits; `Enter` to confirm, `Del` to delete |
| Change Companion | `System` → `Enter` |
| Notification sound on/off | `M` in System |
| Refresh contacts / Live Repeaters | `R` on the relevant screen |

## Menus

`CHAT` · `PRIVATE CHATS` · `NODES` · `PING REPEATERS` · `LIVE REPEATERS` · `ADVERT` · `SYSTEM`

**Ping Repeaters** performs MeshCore path discovery rather than IP/ICMP ping. `HEARD` is a recent received signal; `BIDIR` means a recent bidirectional exchange, not a persistent connection.

## Current testing and known limits

Pairing and PIN selection with a XIAO ESP32-S3 MeshCore BLE Companion, public/private messaging and softer alerts have been tested during development. More hardware combinations, long-term uptime, regression testing and the online binary build remain to be validated. See [known issues](docs/KNOWN_ISSUES.md) and [troubleshooting](docs/TROUBLESHOOTING.md).

The BLE PIN and Companion address are saved in ESP32 Preferences/NVS. The firmware does not claim secure at-rest encryption of the PIN. Avoid sharing keys, device identifiers, personal conversations or unredacted serial logs.

## Project and license

**The author has not yet chosen a software license.** Public GitHub access does not automatically grant permission to modify or redistribute this code. See [license options](docs/LICENSE_DECISION.md). NodePad is independent of [MeshCore](https://github.com/meshcore-dev/MeshCore) and [M5Stack](https://github.com/m5stack).

## Files

- `firmware/NodePad_V2_2_6/` — Arduino sketch
- `.github/workflows/build-firmware.yml` — experimental online binary builder
- `docs/` — installation, build and troubleshooting guides
- `CHANGELOG.md` — version history
- `RELEASE_NOTES.md` — draft beta release notes
