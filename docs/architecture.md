# How this component works

This is a from-first-principles walkthrough of what each piece does and why
it's structured this way — useful if you want to add a new sensor platform,
debug a pairing failure, or just understand what's actually happening before
you trust it with your credentials.

## The three components, and why three

```
esp32_ble_tracker (ESPHome core)
        │  scans, reports discovered BLE advertisements
        ▼
tuya_ble_tracker   — filters scan results down to MACs you've configured,
                     triggers a connection attempt when one is seen
        │
        ▼
tuya_ble_client    — the actual GATT client: connect, discover services,
                     encrypt/decrypt, pairing state machine, write/notify
        │  owns one or more...
        ▼
tuya_ble_node      — per-device state: keys, session, DP callback registry,
                     command queue (for anything that needs to *write* a DP)
        │  sensors/binary_sensors register themselves here
        ▼
sensor / binary_sensor platforms — your actual ESPHome entities
```

This mirrors how ESPHome's own `ble_client` component is structured
(tracker finds devices generically, client owns the connection, "node"-style
child components attach to it), so if you've used other BLE ESPHome
components before, the shape should feel familiar.

## The wire protocol, in order

1. **Connect.** Standard BLE GATT connect + service discovery. Tuya's BLE
   service is `0x1910`, with a notify characteristic `0x2b10` and a write
   characteristic `0x2b11`.
2. **Enable notifications.** This needs an explicit write to the
   characteristic's Client Characteristic Configuration Descriptor (CCCD,
   `0x2902`) — `esp_ble_gattc_register_for_notify()` alone only tells your
   *own* BLE stack to accept notifications; it doesn't tell the peripheral
   to start sending them. Skipping this is a common, quiet failure mode:
   everything else works, but you never receive anything.
3. **`FUN_SENDER_DEVICE_INFO` (code `0x0000`).** Send this once, encrypted
   with `login_key = MD5(local_key[:6])`, empty payload. The device replies
   with a random 6-byte value (`srand`) as part of its device-info response.
4. **Session key.** `session_key = MD5(local_key[:6] + srand)`. Every
   exchange after this point uses `session_key`, not `login_key`.
5. **Pairing, if configured with `device_id`/`uuid`.**
   `FUN_SENDER_PAIR` (code `0x0001`) with a payload of
   `uuid + local_key[:6] + device_id`, encrypted with `session_key`.
6. **Status request.** `FUN_SENDER_DEVICE_STATUS` (code `0x0003`), empty
   payload — this is the part that's easy to miss if you're only reading the
   base upstream component this was forked from: without something to
   *send*, the client has no reason to stay connected once paired, so a
   pure-sensor node (nothing to write) needs to explicitly ask for a status
   dump, or it disconnects before the device ever gets a chance to answer.
7. **`FUN_RECEIVE_DP` (code `0x8001`).** The device's response: a TLV list
   of `dp_id(1) + dp_type(1) + dp_len(1) + value(dp_len bytes)`, repeated for
   every datapoint it has. `dp_type` is `1` for boolean, `2` for a
   big-endian integer (this is what sensor.py decodes), `3` for string, `4`
   for enum, `5` for bitmap.

Every frame (in either direction) is wrapped as
`seq_num(4 BE) + response_to(4 BE) + code(2 BE) + data_len(2 BE) + data +
CRC16(2 BE)`, padded to a multiple of 16 bytes, then
`security_flag(1) + IV(16) + AES-128-CBC(that padded frame)`. The CRC16
variant is CRC-16/MODBUS: init `0xFFFF`, polynomial `0xA001` (reflected), no
final XOR.

Because BLE write characteristics are limited to the negotiated MTU (often
just 20-23 bytes), a single encrypted frame is usually split across several
GATT writes, each prefixed with a 1-byte chunk index; the first chunk also
carries the total encrypted length and a protocol-version nibble. The
receive side (`tuya_ble_client.cpp`'s `collect_data()`) reassembles these
before attempting to decrypt.

## Where a new DP-reading sensor plugs in

`tuya_ble_node`'s `handle_dp_frame()` parses the TLV list on every
`FUN_RECEIVE_DP` and, for each DP, looks up a registered callback by
`dp_id` — logging every DP it sees at `DEBUG` regardless of whether anything
is registered for it, specifically so you can point a logic analyzer's
worth of curiosity at your own device without writing any mapping code
first. The `sensor`/`binary_sensor` platforms in `components/tuya_ble_node/`
just register a small lambda against a `dp_id` in their `to_code()` and
convert whatever bytes show up into a published state — that's the entire
extension point if you want to add, say, a `text_sensor` platform for
string-type DPs, which this repo doesn't have yet.

## Deliberately out of scope

- **BLE Security Manager (SMP) pairing / bonding.** Verified, across this
  component and two independent reference implementations, that Tuya's BLE
  protocol (at least this version of it) doesn't use link-layer bonding at
  all — the "security" is entirely the app-layer AES scheme above. If your
  device's peripheral proactively sends a GAP security request
  (`ESP_GAP_BLE_SEC_REQ_EVT`), that would be a genuinely different situation
  this component doesn't currently handle.
- **Writing DPs safely.** The `output` platform (from the original upstream
  component) demonstrates the write path, but this repo's focus is reading
  sensor data. Before wiring up control of any DP you haven't verified the
  meaning of, check its documented behavior on the Tuya IoT Platform's
  "Specifications" tab for your product — an unknown write-capable DP is not
  something to probe by trial and error against real hardware.
