# Draft GitHub release

**Suggested tag:** `v2.2.6-beta`  
**Title:** NodePad v2.2.6 Beta — BLE picker, PIN entry and quieter alerts

NodePad is an independent, off-grid MeshCore Companion terminal for the original M5Stack Cardputer.

### What's new

- Select and reconnect to a BLE Companion on the Cardputer
- Enter the six-digit PIN locally and remember it per Companion after pairing
- Public channels, private messaging, repeater tools, self-advertisements
- Softer message alerts and mute toggle

### Compatibility

A separate **preconfigured MeshCore Companion BLE** radio is required. **Configure the Companion with the official MeshCore app first**; NodePad cannot configure the radio, network or channels. The author has successfully tested browser installation on the original Cardputer and subsequent Companion BLE connection (Heltec V3 and XIAO Wio are tested Companion types). Wider testing is pending.

**Before publishing a binary release:** consider additional testing across hardware/Companion versions, choose a software license if intending open-source distribution, and check the repository for personal identifiers or secrets.
