# Troubleshooting

Real problems hit while building and bringing this component up, in the
order you're likely to hit them.

## `esphome compile` fails with API/enum errors in `tuya_ble_client.cpp`

If you started from the original `BillyNate/esphome-tuya-ble` component
directly instead of the (already-fixed) copy in this repo, you may hit:

- `'SEARCHING' is not a member of 'esphome::esp32_ble_tracker::ClientState'`
  and similar — ESPHome's `ClientState` enum has changed across versions;
  current versions only have `INIT, DISCONNECTING, IDLE, DISCOVERED,
  CONNECTING, CONNECTED, ESTABLISHED`.
- `'...ESPBTClient::state_' is private within this context` — use the
  public `state()` accessor instead of the `state_` member directly.
- `'esp_fill_random' was not declared` — needs `#include "esp_random.h"`.
- `request for member 'c_str' in ...address_str_...` — `address_str_` is a
  plain `char[18]` on current ESPHome, not `std::string`.

This repo's `components/` already has all of these fixed. If you're merging
your own changes from a different fork, these are the compat gaps to expect.

## `undefined reference to vtable for ...` at link time

If you extend `TYBLENode`/`TYBLEClient` (in `tuya_ble_tracker/common.h`) with
a new virtual method, give it a real (even trivial) definition in
`common.cpp`. Under the Itanium C++ ABI, a class's vtable is only emitted in
the translation unit that defines its "key function" (its first declared,
non-pure, non-inline virtual method) — if every virtual method is declared
in the header with the *actual* implementation only in a derived class
(which is what the original upstream component did throughout), the base
class's vtable is never emitted anywhere, and this only surfaces as a link
error once something actually instantiates a concrete subclass in a real
build — easy to miss if you never got that far.

## Device pairs, but you never see any DP data — process just idles

Check two things, in order:

1. **CCCD write.** Does your log show a notification-enable step completing
   (`CCC descriptor write completed` or similar), or does it just silently
   move on after `esp_ble_gattc_register_for_notify`? The latter means the
   peripheral was never told to actually start sending notifications — see
   `docs/architecture.md` step 2.
2. **Nothing to keep the connection open.** If your node has only sensors
   (nothing to *write*), and the client's logic disconnects as soon as
   pairing/session-key setup finishes because there's no queued outbound
   command, the device never gets asked for its current DP values at all.
   This repo's `request_status()` (sends `FUN_SENDER_DEVICE_STATUS`) exists
   specifically to fix this — confirm it's present and being called if
   you're comparing against a different fork.

## Device accepts the connection, then disconnects after ~15-30s with HCI reason `0x13`

`0x13` is "Remote User Terminated Connection" — the *peripheral* is choosing
to hang up, not a link timeout. If this happens right after you send the
first encrypted `FUN_SENDER_DEVICE_INFO` request and the device never
responds to anything before disconnecting, this is the signature of the
device silently rejecting a request it can't decrypt — i.e. **your
`local_key` is wrong.** This is easy to mistake for a protocol bug, and
mistaking it cost real time on this project's own bring-up before the key
turned out to be the culprit — see `docs/getting-the-local-key.md`,
specifically the section on verifying a key offline before touching
hardware, so you don't have to rediscover this the slow way.

Things that do **not** explain this symptom, ruled out the hard way while
building this repo, so you don't have to re-litigate them:

- Missing BLE-level SMP/bonding — checked; this protocol version doesn't
  use it, confirmed against two independent reference implementations.
- CRC16 variant mismatch — checked bit-for-bit against two independent
  implementations; it's CRC-16/MODBUS, matches exactly.
- Wrong GATT write type (`WRITE_NO_RESPONSE` vs `WRITE_WITH_RESPONSE`) — the
  device's write characteristic in practice accepts both; this wasn't it.

## "I have a `secKey` value too — do I need it?"

Only if your device actually uses Tuya's newer protocol-v2 security scheme.
Don't assume; check the security-flag byte (the first byte of the encrypted
blob) in a real captured packet. `0x04`/`0x05` means classic
(`MD5(local_key[:6])`) — the scheme this component implements. `0x0E`/`0x0F`
means protocol-v2 (`MD5(local_key + secKey)`), which this component does
*not* currently implement (contributions welcome — see
`docs/architecture.md` for where the key-derivation logic lives). Plenty of
devices — including the one this repo was originally built against — never
use `secKey` at all, so fetching one and finding it doesn't help is a real,
expected outcome, not a sign you did something wrong.
