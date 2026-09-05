"""Companion BLE scanner to verify the test advertisement over the air.

Run this on a SECOND computer (a Bluetooth adapter generally cannot receive
its own advertisements). It reports every sighting of the test beacon
(manufacturer id 0xFFFF, payload b'PERIPH') and of the connectable GATT server
advertisement (carrying service UUID 34a1b001-...).

    python verify_scan.py [--seconds 30] [--name SUBSTRING]
"""

import argparse
import asyncio

from bleak import BleakScanner

COMPANY_ID = 0xFFFF
SERVICE_UUID_PREFIX = "34a1b0"


def callback(device, advertisement):
    name = advertisement.local_name or device.name or ""
    manufacturer_data = advertisement.manufacturer_data or {}
    service_uuids = [str(u) for u in (getattr(advertisement, "service_uuids", None) or [])]

    if COMPANY_ID in manufacturer_data:
        pass
    elif any(u.replace("-", "").startswith(SERVICE_UUID_PREFIX) for u in service_uuids):
        pass
    elif args.name and args.name.lower() in (name or "").lower():
        pass
    else:
        return

    extra = ""
    if manufacturer_data:
        payloads = ", ".join(
            f"0x{cid:04X}={bytes(data)!r}" for cid, data in manufacturer_data.items()
        )
        extra += f"  manufacturer: {payloads}"
    if service_uuids:
        extra += f"  services: {service_uuids}"
    print(
        f"[sighting] {name or '<unnamed>':<24} addr={device.address}  "
        f"rssi={advertisement.rssi:>4} dBm{extra}",
        flush=True,
    )


async def main():
    scanner = BleakScanner(detection_callback=callback)
    print(
        f"Scanning for {args.seconds}s (looking for manufacturer id "
        f"0x{COMPANY_ID:04X} or service UUID {SERVICE_UUID_PREFIX}...) ..."
    )
    await scanner.start()
    await asyncio.sleep(args.seconds)
    await scanner.stop()
    print("Scan finished.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Verify the BLE peripheral test over the air")
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--name", help="also match any device whose name contains SUBSTRING")
    args = parser.parse_args()
    asyncio.run(main())
