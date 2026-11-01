# Companion preparation — required before using NodePad

NodePad v2.2.6 Beta is a Bluetooth Low Energy client for **an already configured MeshCore Companion BLE radio**. The Cardputer does not supply the LoRa radio.

**You must configure the Companion using the official MeshCore app before you can use NodePad.** This includes the appropriate LoRa/network settings and channels for your intended mesh. NodePad **cannot configure or reconfigure** the Companion at this stage.

## First-use workflow

1. Install the appropriate **MeshCore Companion BLE** firmware on a compatible radio (tested: Heltec V3 and XIAO Wio). This is done separately from NodePad.
2. Open the **official MeshCore app**, connect to the radio, and complete the required radio/network and channel setup.
3. Disconnect the official app from the Companion. Keep the radio on and advertising Bluetooth.
4. Install NodePad on the **original M5Stack Cardputer**, using the [web installer](https://al83m.github.io/NodePad/) or a supported flashing method.
5. From NodePad's **SELECT COMPANION** screen, select your radio, enter its six-digit BLE PIN if asked, and connect.
6. Verify **BLE SECURE** and **MESH ONLINE** on the Cardputer. If BLE connects but messages fail, check the Companion's radio configuration **in the official MeshCore app**, not in NodePad.

NodePad does not replace the official MeshCore configuration app. Once the Companion is configured, NodePad can be used as a keyboard-based mesh client without a phone during normal operation. To change radio settings later, disconnect NodePad and use the official app again.
