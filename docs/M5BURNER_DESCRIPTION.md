# NodePad — M5Burner description (v2.2.6 Beta)

**Name:** NodePad
**Version:** 2.2.6 Beta
**Device:** Original M5Stack Cardputer (ESP32-S3)
**Website:** https://al83m.github.io/NodePad/
**Source:** https://github.com/AL83M/NodePad

NodePad is an off-grid MeshCore BLE client for the original M5Stack Cardputer. Use a real keyboard to send public and private messages, manage contacts, explore repeater links and send self-advertisements.

**IMPORTANT — COMPANION SETUP REQUIRED:** Before using NodePad, you MUST configure a separate MeshCore Companion BLE radio using the official MeshCore app (radio/network and channel settings as needed). NodePad v2.2.6 CANNOT configure the Companion; it only connects to an already configured radio. Disconnect the official MeshCore app from the radio before pairing NodePad.

Tested with Heltec V3 and XIAO Wio MeshCore BLE Companions. An external Companion BLE device is required for the LoRa connection; NodePad does not transmit LoRa itself.

Install from a desktop browser: https://al83m.github.io/NodePad/
Beta firmware: designed for the original Cardputer, not Cardputer ADV. Back up your existing Cardputer settings before flashing.
