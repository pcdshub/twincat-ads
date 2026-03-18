"""
ADS test server for adsAsynPortDriver integration tests.

Reads testIoc/ads_symbol_dict.json and registers every symbol as a
PLCVariable in a pyads AdvancedHandler, then starts an AdsTestServer
on the default ADS port (48898 / 0xBF02).

Usage:
    python ads_test_server.py [--json path/to/ads_symbol_dict.json]

The server runs until interrupted (Ctrl-C or SIGTERM).
"""

import argparse
import json
import os
import signal
import sys
import time

try:
    from pyads.testserver import AdsTestServer, AdvancedHandler, PLCVariable
    from pyads import constants
except ImportError:
    sys.exit("pyads is required: pip install pyads")


# ── Datatype map: TwinCAT type string → (pyads ads_type constant, symbol_type) ──
# Arrays are handled separately by stripping the '[N]' suffix.
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
    # CHAR arrays are treated as byte strings
    "CHAR":  (constants.ADST_STRING,  "STRING"),
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
        # Preserve array suffix in symbol_type for clarity
        if "[" in datatype:
            symbol_type = datatype
        return ads_type, symbol_type
    # Unknown type — fall back to raw bytes (ADST_UINT8)
    return constants.ADST_UINT8, datatype


def load_symbols(json_path: str) -> list:
    """
    Load symbols from ads_symbol.json and return a list of PLCVariable.

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


def main():
    parser = argparse.ArgumentParser(description="pyads ADS test server")
    parser.add_argument(
        "--json",
        default=os.path.join(
            os.path.dirname(__file__), "..", "ads_symbol.json"
        ),
        help="Path to ads_symbol_dict.json (default: ../ads_symbol.json)",
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
    print("[ads_test_server] Listening on 127.0.0.1:48898 — press Ctrl-C to stop")

    def _shutdown(sig, frame):
        print("\n[ads_test_server] Shutting down...")
        server.close()
        sys.exit(0)

    signal.signal(signal.SIGINT,  _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    # Keep main thread alive while server runs in background thread
    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()
