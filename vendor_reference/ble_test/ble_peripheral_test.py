"""BLE peripheral capability test for Windows.

Answers the question: can this PC's Bluetooth adapter operate in the BLE
peripheral role?

Phase A - report what the driver/adapter claims (BluetoothAdapter properties
          and the Bluetooth radio state).
Phase B - broadcast a real beacon advertisement via
          BluetoothLEAdvertisementPublisher (non-connectable peripheral).
Phase C - attempt a connectable GATT server via GattServiceProvider, with a
          readable/writable/notifiable characteristic. NOTE: Windows restricts
          this API for apps without package identity, so a failure here is a
          *test result*, not a bug in the script.

Run with the Python interpreter that has the winrt packages installed:

    python ble_peripheral_test.py [--adv-seconds 15] [--gatt-seconds 60]
                                  [--skip-beacon] [--skip-gatt]

Serve mode - keep the GATT server running until Ctrl+C so a phone can
connect and interact (reads/writes/notifications are logged):

    python ble_peripheral_test.py --skip-beacon --gatt-seconds 0
"""

import argparse
import asyncio
import sys
import time
import uuid

from winrt.windows.devices.bluetooth import BluetoothAdapter, BluetoothError
from winrt.windows.devices.bluetooth.advertisement import (
    BluetoothLEAdvertisement,
    BluetoothLEAdvertisementPublisher,
    BluetoothLEAdvertisementPublisherStatus,
    BluetoothLEManufacturerData,
)
from winrt.windows.devices.bluetooth.genericattributeprofile import (
    GattCharacteristicProperties,
    GattLocalCharacteristicParameters,
    GattProtectionLevel,
    GattServiceProvider,
    GattServiceProviderAdvertisementStatus,
    GattServiceProviderAdvertisingParameters,
)
from winrt.windows.devices.radios import Radio, RadioKind, RadioState
from winrt.windows.storage.streams import DataWriter

# --- test identifiers -------------------------------------------------------

COMPANY_ID = 0xFFFF  # reserved for testing
SERVICE_UUID = uuid.UUID("34a1b001-7c6d-4e8f-9a2b-3d4e5f607182")
CHAR_UUID = uuid.UUID("34a1b002-7c6d-4e8f-9a2b-3d4e5f607182")

PUB_STATUS_NAMES = {
    int(BluetoothLEAdvertisementPublisherStatus.CREATED): "CREATED",
    int(BluetoothLEAdvertisementPublisherStatus.WAITING): "WAITING",
    int(BluetoothLEAdvertisementPublisherStatus.STARTED): "STARTED",
    int(BluetoothLEAdvertisementPublisherStatus.STOPPING): "STOPPING",
    int(BluetoothLEAdvertisementPublisherStatus.STOPPED): "STOPPED",
    int(BluetoothLEAdvertisementPublisherStatus.ABORTED): "ABORTED",
}
GATT_STATUS_NAMES = {
    int(GattServiceProviderAdvertisementStatus.CREATED): "CREATED",
    int(GattServiceProviderAdvertisementStatus.STARTED): "STARTED",
    int(
        GattServiceProviderAdvertisementStatus.STARTED_WITHOUT_ALL_ADVERTISEMENT_DATA
    ): "STARTED_WITHOUT_ALL_ADVERTISEMENT_DATA",
    int(GattServiceProviderAdvertisementStatus.ABORTED): "ABORTED",
    int(GattServiceProviderAdvertisementStatus.STOPPED): "STOPPED",
}
BLUETOOTH_ERROR_NAMES = {
    int(v): k
    for k, v in vars(BluetoothError).items()
    if isinstance(v, int)
}


def log(msg: str) -> None:
    print(msg, flush=True)


def bytes_to_buffer(data: bytes):
    writer = DataWriter()
    writer.write_bytes(bytes(data))
    return writer.detach_buffer()


def buffer_to_bytes(buffer) -> bytes:
    view = memoryview(buffer)
    return bytes(view[: buffer.length])


def decode_error(error) -> str:
    if error is None:
        return "none"
    return BLUETOOTH_ERROR_NAMES.get(int(error), str(error))


# --- Phase A: adapter / radio capability report ------------------------------

async def phase_adapter() -> dict:
    log("=" * 62)
    log("PHASE A: adapter capability report")
    log("=" * 62)

    adapter = await BluetoothAdapter.get_default_async()
    if adapter is None:
        log("[FAIL] No Bluetooth adapter found (BluetoothAdapter is null).")
        return {"peripheral_supported": False}

    address = adapter.bluetooth_address
    mac = ":".join(f"{(address >> shift) & 0xFF:02X}" for shift in range(40, -1, -8))
    log(f"Adapter MAC address          : {mac}")
    log(f"is_low_energy_supported      : {adapter.is_low_energy_supported}")
    log(f"is_central_role_supported    : {adapter.is_central_role_supported}")
    log(f"is_peripheral_role_supported : {adapter.is_peripheral_role_supported}")
    log(f"is_advertisement_offload_supported : {adapter.is_advertisement_offload_supported}")

    peripheral_supported = bool(adapter.is_peripheral_role_supported)
    if peripheral_supported:
        log("[PASS] The adapter/driver claims BLE peripheral role support.")
    else:
        log("[FAIL] The adapter/driver does NOT support the BLE peripheral role.")

    log("")
    log("Radios:")
    try:
        radios = await Radio.get_radios_async()
        for radio in radios:
            state_name = {
                int(RadioState.UNKNOWN): "UNKNOWN",
                int(RadioState.ON): "ON",
                int(RadioState.OFF): "OFF",
                int(RadioState.DISABLED): "DISABLED",
            }.get(int(radio.state), str(radio.state))
            marker = "  <-- Bluetooth" if radio.kind == RadioKind.BLUETOOTH else ""
            log(f"  {radio.name:<24} kind={int(radio.kind)} state={state_name}{marker}")
    except Exception as e:
        log(f"  [WARN] could not enumerate radios: {e!r}")

    log("")
    return {"peripheral_supported": peripheral_supported}


# --- Phase B: beacon advertisement test --------------------------------------

async def phase_beacon(seconds: float) -> dict:
    log("=" * 62)
    log(f"PHASE B: beacon advertisement test (mfg id 0x{COMPANY_ID:04X}, {int(seconds)}s)")
    log("=" * 62)
    log("Scanners will see an unnamed device with manufacturer data")
    log("0xFFFF = b'PERIPH' (no local name: this stack rejects it).")

    advertisement = BluetoothLEAdvertisement()
    # LocalName is deliberately NOT set: some stacks (this MediaTek one) reject
    # it with E_INVALIDARG. Manufacturer data only works everywhere.
    manufacturer_data = BluetoothLEManufacturerData()
    manufacturer_data.company_id = COMPANY_ID
    manufacturer_data.data = bytes_to_buffer(b"PERIPH")  # keep total AD <= 31 bytes
    advertisement.manufacturer_data.append(manufacturer_data)

    publisher = BluetoothLEAdvertisementPublisher(advertisement)
    state = {"status": None, "error": None}

    def on_status_changed(sender, args):
        state["status"] = int(args.status)
        state["error"] = args.error

    token = publisher.add_status_changed(on_status_changed)
    log("Starting BluetoothLEAdvertisementPublisher ...")
    try:
        publisher.start()
    except OSError as e:
        hresult = getattr(e, "winerror", None)
        code = f" (HRESULT 0x{hresult & 0xFFFFFFFF:08X})" if hresult else ""
        log(f"  [FAIL] publisher.start() raised {e}{code}")
        publisher.remove_status_changed(token)
        log("")
        return {"beacon_pass": False}

    started_seen = False
    aborted = False
    try:
        # Stop ~1.5s after STARTED is confirmed; the test goal is reached then.
        elapsed = 0.0
        while elapsed < seconds and not aborted:
            await asyncio.sleep(0.25)
            elapsed += 0.25
            status = state["status"]
            if status is not None and status != state.get("printed"):
                state["printed"] = status
                name = PUB_STATUS_NAMES.get(status, str(status))
                log(f"  publisher status -> {name} (error: {decode_error(state['error'])})")
            if status == int(BluetoothLEAdvertisementPublisherStatus.STARTED):
                if not started_seen:
                    started_seen = True
                    log(f"  [PASS] Advertising over the air after {elapsed:.1f}s.")
                if elapsed >= 1.5:
                    break
            if status == int(BluetoothLEAdvertisementPublisherStatus.ABORTED):
                aborted = True
    finally:
        try:
            publisher.stop()
        except Exception:
            pass
        publisher.remove_status_changed(token)

    if not started_seen:
        log("  [FAIL] The advertisement never reached the STARTED state.")
        if aborted:
            log(f"  Reason: ABORTED, error = {decode_error(state['error'])}")
            log("  (RADIO_NOT_AVAILABLE -> Bluetooth is off; DISABLED_BY_POLICY/"
                "DISABLED_BY_USER -> advertising blocked by system/user setting; "
                "NOT_SUPPORTED -> radio lacks peripheral/advertising support.)")
        else:
            log("  (If it stayed WAITING, the radio never became available.)")

    log("")
    return {"beacon_pass": started_seen}


# --- Phase C: connectable GATT server test -----------------------------------

GATT_BLOCKED_HINT = """  Windows restricts GattServiceProvider for apps WITHOUT package identity.
  A plain desktop Python process usually cannot run a connectable GATT server.
  Workarounds:
    1. Give the app package identity (MSIX or 'sparse package' with an external
       manifest) and declare the 'bluetooth' capability.
    2. Use Linux + BlueZ, where bleak's BleakServer implements a full GATT
       peripheral.
    3. Use an external BLE dongle/board (ESP32, nRF52, Raspberry Pi)."""


async def phase_gatt(seconds: float) -> dict:
    log("=" * 62)
    log(f"PHASE C: connectable GATT server test ({int(seconds)}s)")
    log("=" * 62)
    log(f"Service UUID        : {SERVICE_UUID}")
    log(f"Characteristic UUID : {CHAR_UUID} (read / write / notify)")

    loop = asyncio.get_running_loop()

    # -- create service provider -------------------------------------------
    try:
        result = await GattServiceProvider.create_async(SERVICE_UUID)
    except Exception as e:
        hresult = getattr(e, "winerror", None)
        code = f" (HRESULT 0x{hresult & 0xFFFFFFFF:08X})" if hresult else ""
        log(f"  [BLOCKED] GattServiceProvider.create_async raised {e!r}{code}")
        log(GATT_BLOCKED_HINT)
        log("")
        return {"gatt_result": "BLOCKED", "client_connected": False}

    provider = result.service_provider
    if provider is None or int(result.error) != int(BluetoothError.SUCCESS):
        log(f"  [BLOCKED] Service provider creation failed: {decode_error(result.error)}")
        log(GATT_BLOCKED_HINT)
        log("")
        return {"gatt_result": "BLOCKED", "client_connected": False}

    log("  Service provider created (bluetooth capability granted).")

    # -- characteristic ------------------------------------------------------
    char_params = GattLocalCharacteristicParameters()
    char_params.characteristic_properties = (
        GattCharacteristicProperties.READ
        | GattCharacteristicProperties.WRITE
        | GattCharacteristicProperties.NOTIFY
    )
    char_params.read_protection_level = GattProtectionLevel.PLAIN
    char_params.write_protection_level = GattProtectionLevel.PLAIN
    char_params.static_value = bytes_to_buffer(b"WinBLE-GATT ready")
    char_params.user_description = "Test read/write/notify characteristic"

    char_result = await provider.service.create_characteristic_async(CHAR_UUID, char_params)
    characteristic = char_result.characteristic
    if characteristic is None or int(char_result.error) != int(BluetoothError.SUCCESS):
        log(f"  [FAIL] Characteristic creation failed: {decode_error(char_result.error)}")
        log("")
        return {"gatt_result": "FAIL", "client_connected": False}
    log("  Characteristic created.")

    # -- event handlers (they fire on WinRT threadpool threads) --------------
    state = {"status": None, "error": None, "printed": None,
             "connected": False, "interactions": 0, "received": []}
    started_at = time.monotonic()

    def note_central_activity():
        state["interactions"] += 1
        if not state["connected"]:
            state["connected"] = True
            log("  [GATT] *** a central is connected and interacting ***")

    async def handle_read(args):
        request = await args.get_request_async()
        value = f"hello #{state['interactions']}".encode()
        log(f"  [GATT] read from central -> {value!r}")
        request.respond_with_value(bytes_to_buffer(value))

    async def handle_write(args):
        request = await args.get_request_async()
        data = buffer_to_bytes(request.value)
        log(f"  [GATT] write received from central: {data!r}")
        state["received"].append((round(time.monotonic() - started_at, 1), data))
        request.respond()

    def dispatch(args, coroutine):
        deferral = args.get_deferral()

        def handle_done(future):
            try:
                future.result()
            except Exception as e:
                log(f"  [GATT] request handling error: {e!r}")
            finally:
                try:
                    deferral.complete()
                except Exception:
                    pass

        asyncio.run_coroutine_threadsafe(coroutine, loop).add_done_callback(handle_done)

    def on_read_requested(sender, args):
        note_central_activity()
        dispatch(args, handle_read(args))

    def on_write_requested(sender, args):
        note_central_activity()
        dispatch(args, handle_write(args))

    def on_subscribed_clients_changed(sender, args):
        try:
            count = len(list(characteristic.subscribed_clients))
        except Exception:
            count = -1
        note_central_activity()
        log(f"  [GATT] subscribed clients -> {count} (notifications {'on' if count else 'off'})")

    read_token = characteristic.add_read_requested(on_read_requested)
    write_token = characteristic.add_write_requested(on_write_requested)
    sub_token = characteristic.add_subscribed_clients_changed(on_subscribed_clients_changed)

    # -- advertisement --------------------------------------------------------
    # This SDK projection of GattServiceProvider has no .Advertisement property,
    # so the connectable payload carries the service UUID automatically.
    adv_params = GattServiceProviderAdvertisingParameters()
    adv_params.is_connectable = True
    adv_params.is_discoverable = True

    def on_adv_status_changed(sender, args):
        state["status"] = int(args.status)
        state["error"] = args.error

    status_token = provider.add_advertisement_status_changed(on_adv_status_changed)

    log("  Starting connectable advertisement ...")
    try:
        provider.start_advertising_with_parameters(adv_params)
    except OSError as e:
        hresult = getattr(e, "winerror", None)
        code = f" (HRESULT 0x{hresult & 0xFFFFFFFF:08X})" if hresult else ""
        log(f"  [BLOCKED] start_advertising raised {e}{code}")
        log(GATT_BLOCKED_HINT)
        characteristic.remove_read_requested(read_token)
        characteristic.remove_write_requested(write_token)
        characteristic.remove_subscribed_clients_changed(sub_token)
        provider.remove_advertisement_status_changed(status_token)
        log("")
        return {"gatt_result": "BLOCKED", "client_connected": False}

    log("  How to connect from your phone:")
    log("   1. Install 'nRF Connect for Mobile' (Android/iOS).")
    log("   2. SCAN. The entry is unnamed - hold the phone next to this PC,")
    log("      sort by RSSI, and check the advertised service UUID 34a1b001-...")
    log("      (run without --skip-beacon to also add manufacturer 0xFFFF).")
    log("   3. CONNECT, then expand the custom service 34a1b001-...")
    log("   4. Read / write bytes / enable notifications on characteristic")
    log(f"      {CHAR_UUID}; every action is logged below.")

    verdict = "FAIL"
    counter = 0
    try:
        elapsed = 0.0
        last_notify = 0.0
        last_beat = 0.0
        while seconds == 0 or elapsed < seconds:
            await asyncio.sleep(0.25)
            elapsed += 0.25
            status = state["status"]
            if status is not None and status != state["printed"]:
                state["printed"] = status
                name = GATT_STATUS_NAMES.get(status, str(status))
                log(f"  advertisement status -> {name} (error: {decode_error(state['error'])})")
                if status in (
                    int(GattServiceProviderAdvertisementStatus.STARTED),
                    int(
                        GattServiceProviderAdvertisementStatus.STARTED_WITHOUT_ALL_ADVERTISEMENT_DATA
                    ),
                ):
                    verdict = "PASS"
                    log("  [PASS] Connectable GATT server is advertising.")
                    log("  (This PASS only means the connectable advertisement")
                    log("   started - no client connection is required for it.)")
                elif status == int(GattServiceProviderAdvertisementStatus.ABORTED):
                    verdict = "BLOCKED"
                    log(f"  [BLOCKED] Advertisement aborted: {decode_error(state['error'])}")
                    log(GATT_BLOCKED_HINT)
                    break

            # heartbeat so the user knows it is still running
            if verdict == "PASS" and not state["connected"] and elapsed - last_beat >= 5.0:
                limit = f"{int(seconds)}s limit" if seconds else "Ctrl+C to stop"
                log(f"  advertising ({elapsed:.0f}s elapsed, {limit}) - waiting for a central")
                last_beat = elapsed

            # push a notification every 3s while at least one central listens
            if verdict == "PASS" and elapsed - last_notify >= 3.0:
                try:
                    if len(list(characteristic.subscribed_clients)) > 0:
                        counter += 1
                        await characteristic.notify_value_async(
                            bytes_to_buffer(f"notify #{counter}".encode())
                        )
                except Exception:
                    pass
                last_notify = elapsed
    finally:
        try:
            provider.stop_advertising()
        except Exception:
            pass
        try:
            characteristic.remove_read_requested(read_token)
            characteristic.remove_write_requested(write_token)
            characteristic.remove_subscribed_clients_changed(sub_token)
            provider.remove_advertisement_status_changed(status_token)
        except Exception:
            pass

    if verdict == "FAIL":
        log("  [FAIL] GATT advertisement never started within the timeout.")

    if state["received"]:
        log(f"  Data received from the central ({len(state['received'])} writes):")
        for at, data in state["received"]:
            log(f"    [{at:6.1f}s] {data!r}")
    else:
        log("  No data was written by any central during this run.")

    log("")
    return {"gatt_result": verdict, "client_connected": state["connected"],
            "received_count": len(state["received"])}


# --- main ---------------------------------------------------------------------

async def main() -> int:
    if sys.stdout.encoding and sys.stdout.encoding.lower() not in ("utf-8", "utf8"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    parser = argparse.ArgumentParser(description="Windows BLE peripheral capability test")
    parser.add_argument("--adv-seconds", type=float, default=15.0,
                        help="max seconds for the beacon test (default 15)")
    parser.add_argument("--gatt-seconds", type=float, default=60.0,
                        help="seconds to keep the GATT server advertising "
                             "(default 60; 0 = serve until Ctrl+C)")
    parser.add_argument("--skip-beacon", action="store_true", help="skip Phase B")
    parser.add_argument("--skip-gatt", action="store_true", help="skip Phase C")
    args = parser.parse_args()

    log("Windows BLE peripheral capability test")
    log(f"Python {sys.version.split()[0]} on {sys.platform}")
    log("")

    summary = {}

    summary.update(await phase_adapter())

    if not args.skip_beacon:
        summary.update(await phase_beacon(args.adv_seconds))

    if not args.skip_gatt:
        summary.update(await phase_gatt(args.gatt_seconds))

    # --- verdict -------------------------------------------------------------
    log("=" * 62)
    log("VERDICT")
    log("=" * 62)
    peripheral = summary.get("peripheral_supported")
    beacon = summary.get("beacon_pass")
    gatt = summary.get("gatt_result")
    client = summary.get("client_connected")
    log(f"Adapter supports BLE peripheral role : {'PASS' if peripheral else 'FAIL'}")
    log(f"Beacon advertisement over the air    : "
        f"{'PASS' if beacon else 'FAIL' if beacon is False else 'SKIPPED'}")
    log(f"Connectable GATT server advertising  : "
        f"{gatt if gatt else 'SKIPPED'}"
        f"{'' if gatt != 'PASS' else ' (advertisement only, no client needed)'}")
    if gatt:
        log(f"Central connected during the run     : "
            f"{'YES' if client else 'NO (nothing connected; connect with nRF Connect)'}")
        log(f"Writes received from the phone       : {summary.get('received_count', 0)}")
    log("")
    if peripheral and beacon:
        log("This laptop CAN use its BLE adapter in peripheral mode"
            " (at least for advertising)." + (
                " Connectable GATT server also works!" if gatt == "PASS" else
                " The connectable GATT server is blocked by Windows"
                " (package identity), not by your hardware." if gatt == "BLOCKED" else ""))
        return 0
    if peripheral and beacon is None and gatt == "PASS":
        log("This laptop CAN act as a connectable BLE peripheral.")
        return 0
    log("This laptop cannot fully use BLE peripheral mode with the current"
        " adapter/driver/settings. See the phase details above.")
    return 1


if __name__ == "__main__":
    try:
        sys.exit(asyncio.run(main()))
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
        sys.exit(130)
