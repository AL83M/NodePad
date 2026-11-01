# Troubleshooting NodePad

## Stuck on SEARCHING

Power on your radio, confirm it runs **MeshCore Companion BLE** rather than repeater or USB-only firmware, and disconnect any other BLE app currently using it. Press `R` to rescan.

## Companion visible but fails to connect

Select the correct device; keep it disconnected from other BLE clients. The last successfully paired Companion is remembered for automatic reconnect.

## PIN/PAIR failure (including 1284)

Verify the six-digit passkey configured on the actual radio. An incorrect PIN, stale bond or incompatible settings may cause pairing failure; one error code alone does not prove the PIN is wrong. Try disconnecting other clients and clearing stale pairing records where supported.

## No radio found

Only advertising MeshCore Companion BLE devices using the supported service are shown. Check firmware and scan again.

## Alerts are too loud

Use `M` from the System screen to toggle sound, or adjust `SOFT_NOTIFICATION_VOLUME` in the sketch.

## BLE works but messages/repeaters fail

BLE being connected doesn't prove a valid LoRa route. Check network/radio settings, antenna positioning and mesh coverage.

When reporting bugs, include NodePad and radio firmware versions, hardware and error details. Redact private identifiers and keys.
