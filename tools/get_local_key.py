"""
Fetch the real local_key (and secKey, if your device uses one) for a Tuya BLE
device via Tuya's *mobile-app* API — the same call the Smart Life / Tuya Smart
app itself makes when it opens the device.

Why this exists: the Tuya Cloud OpenAPI (the standard "IoT Platform" business
API that tools like tinytuya and most Home Assistant Tuya integrations use)
often returns a wrong or placeholder local_key for BLE-only sub-devices that
are routed through a gateway in Tuya's device model. See
../docs/getting-the-local-key.md for the full explanation of why, and how to
recognize whether you've hit this problem.

This script never touches your ESP32 or your Tuya device directly — it just
authenticates to Tuya's cloud as if it were the mobile app, and asks for one
device's activation credentials. It requires no rooted phone, no emulator,
and no reverse-engineered APK; it's built on the pure-Python `tuya-mobile`
package (https://github.com/AboveColin/tuya-mobile).

Usage:
    python3 -m venv venv && source venv/bin/activate
    pip install "tuya-mobile==1.2.0" aiohttp
    python3 get_local_key.py

You'll be asked for your Tuya/Smart Life account email (or phone number) and
password (hidden input). Your password is sent only to Tuya's real login API
and is never written to disk by this script.

Optional: if you've captured a real encrypted packet from your device (e.g.
via an Android Bluetooth HCI snoop log — see docs/getting-the-local-key.md
for how), you can paste its IV and ciphertext below to verify the fetched key
actually decrypts real traffic before you wire anything up. This step is
optional but recommended for a first-time device: it lets you confirm the
key is correct in seconds, instead of finding out by trial and error against
real hardware.
"""

import asyncio
import getpass
import hashlib

import aiohttp
from tuya_mobile import TuyaMobileApp, TuyaPasswordClient

# Tuya's country-code convention for login, not a phone number — e.g. "1"
# for the US, "44" for the UK, "48" for Poland, "49" for Germany, etc.
COUNTRY_CODE = "1"


def crc16_tuya(data: bytes) -> int:
    """Tuya's DP-frame CRC16 (CRC-16/MODBUS variant): init 0xFFFF, poly 0xA001
    reflected, no final XOR. Only used here if you paste a packet to verify."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte & 0xFF
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def verify_against_packet(local_key: str, sec_key: str | None) -> None:
    try:
        from Crypto.Cipher import AES
    except ImportError:
        print(
            "\n(Skipping packet verification: run `pip install pycryptodome` "
            "if you want this step.)"
        )
        return

    print(
        "\nOptional verification: paste a captured device_info packet's IV "
        "and ciphertext (hex, no spaces) if you have one, or just press "
        "Enter twice to skip this step."
    )
    iv_hex = input("IV (32 hex chars, blank to skip): ").strip()
    if not iv_hex:
        return
    ct_hex = input("Ciphertext (hex): ").strip()

    iv = bytes.fromhex(iv_hex)
    ct = bytes.fromhex(ct_hex)

    candidates: list[tuple[str, bytes]] = [
        ("classic MD5(local_key[:6])", hashlib.md5(local_key.encode("ascii")[:6]).digest())
    ]
    if sec_key:
        candidates.append(
            (
                "protocol-v2 MD5(local_key+sec_key)",
                hashlib.md5(f"{local_key}{sec_key}".encode("ascii")).digest(),
            )
        )

    print()
    any_pass = False
    for name, key in candidates:
        plain = AES.new(key, AES.MODE_CBC, iv).decrypt(ct)
        code = int.from_bytes(plain[8:10], "big")
        data_len = int.from_bytes(plain[10:12], "big")
        ok = code == 0 and data_len == 0
        any_pass = any_pass or ok
        print(
            f"  [{name}] -> code=0x{code:04x} data_len={data_len} "
            f"{'<<< PASS' if ok else '(fail)'}"
        )
    print("\n=== RESULT:", "PASS" if any_pass else "FAIL (try the other key, or this device may need something else)", "===")


async def main() -> None:
    username = input("Tuya/Smart Life account email or phone: ").strip()
    password = getpass.getpass("Password (hidden): ")
    device_id = input("Device ID (from the Tuya IoT Platform device list): ").strip()

    async with aiohttp.ClientSession() as session:
        client = TuyaPasswordClient.for_application(
            TuyaMobileApp.SMART_LIFE, session, username=username
        )
        await client.login_with_password(password, country_code=COUNTRY_CODE)
        creds = await client.get_device_credentials(device_id)

    print(f"\nlocalKey: {creds.local_key}")
    print(f"secKey  : {creds.sec_key}")
    print(
        "\nCopy the localKey above into your ESPHome secrets.yaml as "
        "tuya_local_key. Most devices don't use secKey at all — see "
        "docs/getting-the-local-key.md for when it matters."
    )

    verify_against_packet(creds.local_key, creds.sec_key)


if __name__ == "__main__":
    asyncio.run(main())
