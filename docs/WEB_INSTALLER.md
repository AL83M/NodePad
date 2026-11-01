# NodePad Web Installer

The source for the NodePad installation page is in `web/`. It is published by the **Deploy NodePad web installer** workflow as a GitHub Pages website.

**Prerequisite:** First configure a separate **MeshCore Companion BLE** radio using the **official MeshCore app**. NodePad **cannot** configure that radio's firmware, LoRa/network settings or channels. The NodePad web installer only installs NodePad **on the original Cardputer**. Disconnect the official app from the radio before connecting with NodePad.

## Enable GitHub Pages

1. Open `Settings → Pages` on the NodePad repository.
2. Under **Build and deployment → Source**, choose **GitHub Actions**.
3. Open **Actions → Deploy NodePad web installer**.
4. Select **Run workflow** (or rerun the latest failed workflow after enabling Pages).
5. When the deployment succeeds, visit `https://al83m.github.io/NodePad/`.

## How the web flasher works

The website uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/) via HTTPS and Web Serial. The build job produces a segmented firmware set for ESP32-S3 at these flash offsets:

- Bootloader: `0x0000`
- Partition table: `0x8000`
- OTA boot data: `0xE000`
- Application: `0x10000`

These files are extracted from the merged firmware produced by the same compilation workflow. The job checks the merged image size, bootloader, partition table and app equality to reduce mismatch risk.

**CAUTION:** This is a beta installer. The author has successfully tested browser installation and subsequent MeshCore BLE connection on the original Cardputer, but wider testing is needed. Only the original M5Stack Cardputer is supported. Users should back up settings and know that flashing or choosing erase can destroy existing data. No other ESP32-S3 models are supported simply because the tool recognizes their chip family.

The manifest sets `new_install_prompt_erase: true` so users can decide whether to erase on a new installation, but data preservation is never guaranteed.

## Update the release

When you change the sketch, update the firmware version and firmware paths in `web/manifest.json` and `.github/workflows/deploy-pages.yml` together. The Pages deploy workflow compiles directly from source, so its downloadable website assets do not need to be committed as large binaries.
