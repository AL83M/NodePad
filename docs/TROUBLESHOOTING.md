# Troubleshooting NodePad

## Configure the Companion BEFORE NodePad

Use the **official MeshCore app** to set up your separate **Companion BLE** radio (for example Heltec V3 or XIAO Wio) before attempting to connect with the Cardputer. Configure its required mesh/radio settings and channels in that app. NodePad is a BLE client, **not** a Companion configuration tool, and cannot currently set up or reconfigure the radio. Disconnect the official app from the Companion before pairing with NodePad.

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

BLE being connected doesn't prove a valid LoRa route. Verify the Companion's network/radio and channel settings using the **official MeshCore app**, as NodePad cannot modify them. Also check antenna positioning and mesh coverage.

When reporting bugs, include NodePad and radio firmware versions, hardware and error details. Redact private identifiers and keys.
