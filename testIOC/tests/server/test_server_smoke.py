# testIOC/tests/server/test_server_smoke.py
"""
Smoke test for ads_test_server.py
Starts the server, connects via pyads, reads a known symbol, then shuts down.
"""
import time
import threading
import sys
import json
import os

import pyads
from pyads.testserver import AdsTestServer, AdvancedHandler, PLCVariable
from pyads import constants

# Import our server loader
sys.path.insert(0, os.path.dirname(__file__))
from ads_test_server import load_symbols, resolve_ads_type

JSON_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "ads_symbols.json")
TEST_SERVER_AMS_NET_ID = "127.0.0.1.1.1"
TEST_SERVER_IP          = "127.0.0.1"
TEST_SERVER_AMS_PORT    = pyads.PORT_TC3PLC1  # 851


def test_server_starts_and_symbol_readable():
    # ── Setup ────────────────────────────────────────────────────────────────
    handler = AdvancedHandler()
    variables = load_symbols(JSON_PATH)
    for var in variables:
        handler.add_variable(var)

    server = AdsTestServer(handler=handler, logging=False)
    server.start()
    time.sleep(1)  # let server settle

    # ── Add route (Linux requirement) ────────────────────────────────────────
    pyads.open_port()
    pyads.add_route(pyads.AmsAddr(TEST_SERVER_AMS_NET_ID, TEST_SERVER_AMS_PORT), TEST_SERVER_IP)

    # ── Connect ──────────────────────────────────────────────────────────────
    plc = pyads.Connection(TEST_SERVER_AMS_NET_ID, TEST_SERVER_AMS_PORT, TEST_SERVER_IP)
    plc.open()

    try:
        # Pick the first BOOL symbol from the JSON
        with open(JSON_PATH) as f:
            data = json.load(f)

        bool_symbol = next(
            v["symbol"] for v in data.values() if v["datatype"] == "BOOL"
        )
        print(f"Reading symbol: {bool_symbol}")

        val = plc.read_by_name(bool_symbol, pyads.PLCTYPE_BOOL)
        print(f"Value: {val}  (expected: False — zero initialised)")
        assert val == False, f"Expected False, got {val}"

        # Write a value and read it back
        plc.write_by_name(bool_symbol, True, pyads.PLCTYPE_BOOL)
        val = plc.read_by_name(bool_symbol, pyads.PLCTYPE_BOOL)
        print(f"After write: {val}  (expected: True)")
        assert val == True, f"Expected True, got {val}"

        print("✓ Smoke test passed")

    finally:
        plc.close()
        server.close()


if __name__ == "__main__":
    test_server_starts_and_symbol_readable()