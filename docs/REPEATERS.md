# Repeaters and radio link metrics

NodePad's **Ping Repeaters** feature uses MeshCore path discovery, **not** IP/ICMP ping.

- **HEARD:** Companion has recently heard the repeater.
- **BIDIR:** a valid recent request/response exchange was observed; not an always-on connection.
- **RSSI:** reported receive signal strength.
- **RX SNR:** local receive signal-to-noise ratio when available.
- **TX SNR:** reverse-link metric if returned by the repeater/protocol.

A missing response does not necessarily mean a repeater is down. Antenna placement, RF coverage, network parameters, interference, congestion and airtime all affect results.
