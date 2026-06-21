"""
Smoke test for ads_test_server.py

Verifies:
1. Server starts and loads all symbols
2. Client can connect
3. Zero-initialised read works
4. Write + readback works
5. Periodic updater changes values over time (bulk read / notification path)
"""

import time
import json
import os
import struct
import sys

import pyads
from pyads.testserver import AdsTestServer, AdvancedHandler, PLCVariable
from pyads import constants

sys.path.insert(0, os.path.dirname(__file__))
from ads_test_server import load_symbols, start_variable_updater, TEST_SYMBOLS, AdsTestHandler, AdsTestServerFixed

# Symbol dict lives in the testIOC submodule; the smoke-test CI job must check
# the submodule out (submodules: recursive).
JSON_PATH = os.path.join(
    os.path.dirname(__file__), "..", "..",
    "testIOC", "iocBoot", "ioc-TestIOC", "ads_symbol_dict.json"
)
TEST_SERVER_AMS_NET_ID = "127.0.0.1.1.1"
TEST_SERVER_IP         = "127.0.0.1"
TEST_SERVER_AMS_PORT   = pyads.PORT_TC3PLC1  # 851


def test_server_starts_and_symbol_readable():
    # ── Setup ─────────────────────────────────────────────────────────────────
    handler = AdsTestHandler()
    for var in load_symbols(JSON_PATH, handler=handler):
        handler.add_variable(var)

    server = AdsTestServerFixed(handler=handler, logging=False)
    server.start()
    time.sleep(1)

    pyads.open_port()
    pyads.add_route(
        pyads.AmsAddr(TEST_SERVER_AMS_NET_ID, TEST_SERVER_AMS_PORT),
        TEST_SERVER_IP
    )

    plc = pyads.Connection(
        TEST_SERVER_AMS_NET_ID, TEST_SERVER_AMS_PORT, TEST_SERVER_IP
    )
    plc.open()

    try:
        # ── Test 1: zero-initialised read ─────────────────────────────────────
        bool_sym = TEST_SYMBOLS["BOOL"]
        val = plc.read_by_name(bool_sym, pyads.PLCTYPE_BOOL)
        assert val == False, f"Expected False (zero init), got {val}"
        print(f"✓ Zero-init read:  {bool_sym} = {val}")

        # ── Test 2: write + readback ──────────────────────────────────────────
        plc.write_by_name(bool_sym, True, pyads.PLCTYPE_BOOL)
        val = plc.read_by_name(bool_sym, pyads.PLCTYPE_BOOL)
        assert val == True, f"Expected True after write, got {val}"
        print(f"✓ Write+readback:  {bool_sym} = {val}")

        # ── Test 3: updater changes values over time ──────────────────────────
        updater = start_variable_updater(handler, interval=0.05)

        lreal_sym = TEST_SYMBOLS["LREAL"]
        val_before = plc.read_by_name(lreal_sym, pyads.PLCTYPE_LREAL)
        time.sleep(0.3)  # wait for a few update cycles
        val_after = plc.read_by_name(lreal_sym, pyads.PLCTYPE_LREAL)
        assert val_after != val_before, (
            f"Expected value to change after updater ran, "
            f"before={val_before} after={val_after}"
        )
        print(f"✓ Updater working: {lreal_sym} "
              f"{val_before:.1f} → {val_after:.1f}")

        # ── Test 4: DINT updater ──────────────────────────────────────────────
        dint_sym = TEST_SYMBOLS["DINT"]
        v1 = plc.read_by_name(dint_sym, pyads.PLCTYPE_DINT)
        time.sleep(0.2)
        v2 = plc.read_by_name(dint_sym, pyads.PLCTYPE_DINT)
        assert v2 != v1, f"DINT not updating: {v1} → {v2}"
        print(f"✓ DINT updating:   {dint_sym} {v1} → {v2}")

        updater.stop_event.set()
        print("\n✓ All smoke tests passed")

    finally:
        plc.close()
        server.close()


if __name__ == "__main__":
    test_server_starts_and_symbol_readable()
