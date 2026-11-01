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

A separate MeshCore **Companion BLE** radio is required. Limited hardware tests have been performed; wider testing is pending.

**Before publishing a binary release:** run a clean GitHub Actions build, flash and validate the resulting image on the original Cardputer, choose a software license if intending open-source distribution, and check the repository for personal identifiers or secrets.
