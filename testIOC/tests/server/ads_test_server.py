"""
ADS test server for adsAsynPortDriver integration tests.

Reads testIoc/ads_symbol_dict.json and registers every symbol as a
PLCVariable in a pyads AdvancedHandler, then starts an AdsTestServer
on the default ADS port (48898 / 0xBF02).

A background updater thread periodically increments a set of well-known
test symbols so that bulk read and notification tests can verify the
driver picks up live value changes.

Usage:
    python ads_test_server.py [--json path/to/ads_symbol_dict.json]
                              [--update-interval 0.1]

The server runs until interrupted (Ctrl-C or SIGTERM).
"""

import argparse
import json
import os
import signal
import struct
import sys
import threading
import time

try:
    from pyads.testserver import AdsTestServer, AdvancedHandler, PLCVariable
    from pyads import constants
except ImportError:
    sys.exit("pyads is required: pip install pyads")


# ── Datatype map: TwinCAT type string → (pyads ads_type constant, symbol_type) ──
_BASE_TYPE_MAP = {
    "BOOL":  (constants.ADST_BIT,     "BOOL"),
    "BYTE":  (constants.ADST_UINT8,   "BYTE"),
    "USINT": (constants.ADST_UINT8,   "USINT"),
    "SINT":  (constants.ADST_INT8,    "SINT"),
    "UINT":  (constants.ADST_UINT16,  "UINT"),
    "INT":   (constants.ADST_INT16,   "INT"),
    "WORD":  (constants.ADST_UINT16,  "WORD"),
    "DINT":  (constants.ADST_INT32,   "DINT"),
    "UDINT": (constants.ADST_UINT32,  "UDINT"),
    "REAL":  (constants.ADST_REAL32,  "REAL"),
    "LREAL": (constants.ADST_REAL64,  "LREAL"),
    "CHAR":  (constants.ADST_STRING,  "STRING"),
}

# ── Well-known test symbols ───────────────────────────────────────────────────
# These symbols are updated periodically by the updater thread.
# C++ tests can rely on these names and types being present and changing.
TEST_SYMBOLS = {
    "BOOL":  "GVL_Logger.bTrickleTripped",               # size 1
    "DINT":  "GVL_Logger.iLogPort",                       # size 4
    "LREAL": "Main.M1.fBacklash",                         # size 8
    "INT":   "Main.fbMotionArr[10].fbMotionAxis."
             "fbParMgrNc.fbNcSoftAxis.p.Backlash.eAi",   # size 2
}


def resolve_ads_type(datatype: str):
    """
    Resolve a TwinCAT datatype string to a (ads_type, symbol_type) tuple.

    Handles both scalar types (e.g. 'LREAL') and array types
    (e.g. 'LREAL[100]', 'CHAR[80]') by stripping the array suffix.

    Returns (ADST_UINT8, datatype) as a safe fallback for unknown types.
    """
    base = datatype.split("[")[0].strip()
    if base in _BASE_TYPE_MAP:
        ads_type, symbol_type = _BASE_TYPE_MAP[base]
        if "[" in datatype:
            symbol_type = datatype
        return ads_type, symbol_type
    return constants.ADST_UINT8, datatype


def load_symbols(json_path: str) -> list:
    """
    Load symbols from ads_symbol_dict.json and return a list of PLCVariable.

    Each JSON entry has the form:
        {
            "symbol":   "GVL_Logger.bTrickleTripped",
            "datatype": "BOOL",
            "size":     1,
            "name":     "$(PREFIX)General:GlobalLogTrickleTrip_RBV"
        }
    """
    with open(json_path) as f:
        data = json.load(f)

    variables = []
    skipped = 0

    for epics_name, entry in data.items():
        symbol   = entry.get("symbol", "")
        datatype = entry.get("datatype", "")
        size     = entry.get("size", 0)

        if not symbol or not datatype or size <= 0:
            skipped += 1
            continue

        ads_type, symbol_type = resolve_ads_type(datatype)
        value = bytes(size)  # zero-initialised

        variables.append(
            PLCVariable(
                name=symbol,
                value=value,
                ads_type=ads_type,
                symbol_type=symbol_type,
            )
        )

    print(f"[ads_test_server] Loaded {len(variables)} symbols "
          f"({skipped} skipped) from {json_path}")
    return variables


def start_variable_updater(handler: AdvancedHandler,
                           interval: float = 0.1) -> threading.Thread:
    """
    Start a background thread that periodically updates the well-known
    test symbols with incrementing values.

    This simulates live PLC data so that bulk read and notification
    tests can verify the driver picks up value changes.

    Update encoding per type:
        BOOL  — alternates True/False each cycle
        DINT  — increments counter as int32
        LREAL — increments counter as float64
        INT   — increments counter as int16 (wraps at 32767)
    """
    stop_event = threading.Event()

    def _update():
        counter = 0
        while not stop_event.is_set():
            try:
                # BOOL — alternate True/False
                var = handler.get_variable_by_name(TEST_SYMBOLS["BOOL"])
                var.value = struct.pack("<?", bool(counter % 2))

                # DINT — incrementing int32
                var = handler.get_variable_by_name(TEST_SYMBOLS["DINT"])
                var.value = struct.pack("<i", counter % 0x7FFFFFFF)

                # LREAL — incrementing float64
                var = handler.get_variable_by_name(TEST_SYMBOLS["LREAL"])
                var.value = struct.pack("<d", float(counter))

                # INT — incrementing int16
                var = handler.get_variable_by_name(TEST_SYMBOLS["INT"])
                var.value = struct.pack("<h", counter % 0x7FFF)

            except Exception as e:
                print(f"[ads_test_server] Updater error: {e}")

            counter += 1
            stop_event.wait(interval)

    t = threading.Thread(target=_update, daemon=True, name="variable-updater")
    t.stop_event = stop_event  # allow caller to stop it
    t.start()
    print(f"[ads_test_server] Variable updater started "
          f"(interval={interval}s, {len(TEST_SYMBOLS)} symbols)")
    return t


def main():
    parser = argparse.ArgumentParser(description="pyads ADS test server")
    parser.add_argument(
        "--json",
        default=os.path.join(
            os.path.dirname(__file__), "..", "..", "ads_symbols.json"
        ),
        help="Path to ads_symbol_dict.json",
    )
    parser.add_argument(
        "--update-interval",
        type=float,
        default=0.1,
        help="Variable update interval in seconds (default: 0.1)",
    )
    parser.add_argument(
        "--no-updater",
        action="store_true",
        help="Disable the periodic variable updater",
    )
    args = parser.parse_args()

    json_path = os.path.abspath(args.json)
    if not os.path.exists(json_path):
        sys.exit(f"[ads_test_server] JSON not found: {json_path}")

    handler = AdvancedHandler()
    for var in load_symbols(json_path):
        handler.add_variable(var)

    server = AdsTestServer(handler=handler, logging=False)
    server.start()
    print("[ads_test_server] Listening on 127.0.0.1:48898")

    updater = None
    if not args.no_updater:
        updater = start_variable_updater(handler, args.update_interval)

    def _shutdown(sig, frame):
        print("\n[ads_test_server] Shutting down...")
        if updater:
            updater.stop_event.set()
        server.close()
        sys.exit(0)

    signal.signal(signal.SIGINT,  _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()