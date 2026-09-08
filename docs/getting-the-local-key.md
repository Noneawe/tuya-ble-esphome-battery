# Getting a working `local_key` (the part that actually takes time)

If you're doing this for the first time, budget most of your time for *this*
page, not for the ESPHome YAML. The BLE protocol and the component code are
the easy, mechanical part. Getting a `local_key` that actually decrypts your
device's traffic is the part that varies per device and per account, and
where a wrong assumption can cost you hours of "why won't it pair" debugging
against real hardware.

## What the `local_key` is, briefly

Every Tuya device has a 16-character secret (`local_key`) that's used to
derive the AES key for its local (non-cloud) protocols — this repo's BLE
component, but also the well-known LAN/Wi-Fi local protocol used by projects
like `tuya-local`/`localtuya`. Without the real key, you can see the device,
connect to it, even complete GATT service discovery — but you can't decrypt
or produce anything it will accept, because every meaningful exchange is
AES-encrypted with a key derived from this secret.

## The trap: the Cloud OpenAPI often gives you the wrong one

The "obvious" way to get a device's `local_key` is the Tuya IoT Platform's
standard OpenAPI (`/v1.0/devices/{device_id}` or `/v1.0/users/{uid}/devices`
under a registered Cloud Project's business-account credentials). This is
what tools like [`tinytuya`](https://github.com/jasonacox/tinytuya)'s wizard
and most Home Assistant Tuya integrations (including
[`ha_tuya_ble`](https://github.com/PlusPlus-ua/ha_tuya_ble)) use, and it's
the *correct* source for most Wi-Fi/LAN devices.

**For BLE-only devices that are modeled as a sub-device of a gateway**
(check: does your device's Cloud API record have `"sub": true` and an empty
`"ip"` field? that's the tell), this endpoint can return a **placeholder
value that isn't the device's real BLE key at all** — commonly the *same*
string is returned for several unrelated devices under the same gateway,
which is itself a strong hint something's wrong. If you build the protocol
correctly (this repo already does) and your device still never responds and
eventually disconnects, a wrong key — not a protocol bug — is the first
thing to suspect.

### How to know for sure, before you touch any hardware

If you have an Android phone with the real vendor app (Smart Life / Tuya
Smart) already paired to the device, you can get a **fully offline,
zero-hardware-required proof** of whether a candidate key is right:

1. On the phone: **Settings → Developer options → Bluetooth HCI snoop log**
   → set to **Full** (not "Filtered" — filtered strips the payload bytes you
   need). Toggle Bluetooth off/on to start a clean log.
2. Open the vendor app, view the device, pull-to-refresh its status. This
   makes the phone talk directly to the device over BLE using its real key.
3. Pull the log: `adb bugreport .` (works without root on modern Android;
   the snoop log is bundled inside the bugreport zip, under
   `FS/data/misc/bluetooth/logs/btsnoop_hci_*.log`, because that path isn't
   readable directly without root).
4. Open it in Wireshark/tshark, filter to your device's BLE connection, and
   find the app's write to the Tuya write characteristic (`0x2b11`, service
   `0x1910`) shortly after connecting — that's a `FUN_SENDER_DEVICE_INFO`
   request, encrypted with the device's real login key.
5. Reassemble the (possibly multi-chunk) write into `security_flag(1) +
   IV(16) + ciphertext`, and try decrypting it with your candidate key using
   the classic derivation (`MD5(local_key[:6])` as the AES-128-CBC key). A
   correct key decrypts to a plaintext starting with 12 zero/near-zero
   metadata bytes and `code == 0x0000` (device-info request has no payload);
   a wrong key decrypts to garbage. `tools/get_local_key.py` in this repo can
   do this check for you interactively if you paste in the IV/ciphertext.

This test is worth doing because it turns "does this key work?" from a
20-second BLE test against real hardware into an instant, side-effect-free
cryptographic check.

## Getting the real key: Tuya's mobile-app API

If the OpenAPI key fails the check above, the real key can be obtained the
same way the vendor app itself gets it: by authenticating to Tuya's
**mobile-app API** (not the OpenAPI/business API) as if you were the app,
and asking for that one device's activation credentials.

Doing this by hand means reverse-engineering the app's request-signing
scheme (`libthing_security.so`), which is why most guides for this jump
straight to rooting a phone and running Frida against the live app. That
works, but it's a lot of moving parts (root, an emulator or a spare phone,
bypassing the app's anti-tamper checks, which is its own rabbit hole on an
emulator — see the "why not just use an emulator" note below).

The [`tuya-mobile`](https://pypi.org/project/tuya-mobile/) Python package
reimplements the mobile app's request signing in pure Python, so you can just
call the real API directly:

```bash
python3 -m venv venv && source venv/bin/activate
pip install "tuya-mobile==1.2.0" aiohttp pycryptodome
python3 tools/get_local_key.py
```

It'll ask for your account email/phone, password (hidden input, sent only to
Tuya's real login endpoint), and the device's `device_id` (get this from the
Tuya IoT Platform's device list, or from the vendor app's device-info page).
It prints the real `localKey` (and `secKey`, if your device uses the newer
protocol variant — see below) straight from Tuya's servers, no phone or
emulator required.

If you captured a packet per the section above, the script will also offer
to verify the key against it before you go anywhere near your ESP32.

### A note on `secKey` / "protocol-v2"

Some newer Tuya BLE devices use a second secret (`secKey`) alongside
`local_key`, with the AES key derived as `MD5(local_key + secKey)` instead of
the classic `MD5(local_key[:6])`, and a different security-flag byte on the
wire (`0x0E`/`0x0F` instead of `0x04`/`0x05`). `tools/get_local_key.py`
fetches both fields, and the fetched `secKey` will be empty/`None` if your
device doesn't use this scheme. **Don't assume you need it** — check the
security-flag byte in a real captured packet (position 0 of the encrypted
blob) if you want to know for certain which scheme your specific device
uses before writing any decoding logic.

### Why not just root a phone / use an emulator instead?

You can, and it does work in principle — but budget for it being a much
bigger and less certain detour than the mobile-API approach above:

- A rooted physical phone is the most reliable option, but it's usually
  your daily phone, and unlocking the bootloader typically wipes it.
- An Android emulator avoids that risk, but modern vendor apps (Tuya's
  included) commonly ship anti-tamper/RASP checks that detect the emulator
  itself (not just root) — via blatant `build.prop` fingerprints
  (`ro.kernel.qemu`, `ro.hardware=ranchu`, etc.), and defeating that
  reliably can mean spoofing dozens of properties and still not being
  enough, since some checks look at things a property spoof can't fix
  (`/dev/qemu_pipe`, virtio devices, etc.). This is a genuinely open-ended
  rabbit hole, not a quick fix.
- The mobile-API approach sidesteps all of this because it never runs the
  app at all — it just re-implements the (unauthenticated, publicly known)
  request-signing math in plain Python.

## Getting `device_id`, `uuid`, and MAC

These three are much easier and not gated by any of the above:

- **MAC address**: scan for the device with any BLE scanner app (e.g. nRF
  Connect) — Tuya BLE devices typically advertise as `TY` or their product
  name.
- **`device_id`**: visible in the Tuya IoT Platform's device list for your
  linked Cloud Project, or in the vendor app's device settings ("Device
  Information" / "About").
- **`uuid`**: also in the Cloud API's device record (sometimes labeled
  `uuid` or `node_id`) — this is a separate value from `device_id`, used
  during the BLE pairing handshake specifically.
