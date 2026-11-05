import re
import json
import sys
from pathlib import Path

DB_FILE = sys.argv[1] if len(sys.argv) > 1 else "test.db"
OUT_FILE = sys.argv[2] if len(sys.argv) > 2 else "ads_symbol_dict.json"

# Mapping from EPICS DTYP to (PLC type string, byte size)
# Used as fallback when PLC naming convention cannot determine type
DTYPE_MAP = {
    "asynInt32":         ("DINT",  4),
    "asynInt16":         ("INT",   2),
    "asynInt64":         ("LINT",  8),
    "asynFloat64":       ("LREAL", 8),
    "asynFloat32":       ("REAL",  4),
    "asynUInt32Digital": ("UDINT", 4),
    "asynInt8ArrayIn":   ("CHAR_ARRAY", 1),
    "asynInt8ArrayOut":  ("CHAR_ARRAY", 1),
}

RECORD_START = re.compile(r"record\((\w+),\s*\"([^\"]+)\"\)")
FIELD_RE     = re.compile(r"field\((\w+),\s*\"([^\"]*)\"\)")
SYMBOL_RE    = re.compile(r"ADSPORT=\d+(?:/[^/]+=\d+)*/([^?=\s]+)")


def parse_records(text):
    records = []
    current = None
    for line in text.splitlines():
        line = line.strip()
        m = RECORD_START.match(line)
        if m:
            if current:
                records.append(current)
            current = {
                "rtype":  m.group(1),
                "name":   m.group(2),
                "fields": {}
            }
            continue
        if current:
            m = FIELD_RE.match(line)
            if m:
                current["fields"][m.group(1)] = m.group(2)
        if line == "}":
            if current:
                records.append(current)
                current = None
    if current:
        records.append(current)
    return records


def extract_symbol(inp):
    if not inp:
        return None
    m = SYMBOL_RE.search(inp)
    if m:
        return m.group(1)
    return None


def get_plc_type_from_name(symbol):
    """
    Infer PLC type from variable name prefix convention (IEC 61131-3 Hungarian).
    Checks the last component of the dotted path:
      e.g. 'GVL_Logger.bTrickleTripped' -> 'bTrickleTripped'

    Prefixes:
      b   -> BOOL     (1 byte)
      dw  -> UDINT    (4 bytes)
      f   -> LREAL    (8 bytes)  conservative — could be REAL(4)
      n/i -> DINT     (4 bytes)
      u   -> UDINT    (4 bytes)
      w   -> UINT     (2 bytes)
      e   -> INT      (2 bytes)  enum
      c   -> CHAR array — handled by waveform/string record
      st/fb -> struct/FB instance — unknown size, skip

    Returns (plc_type, size) or (None, None) if prefix unknown.
    """
    varname = symbol.split('.')[-1] if '.' in symbol else symbol

    if varname.startswith('b'):
        return 'BOOL', 1
    if varname.startswith('dw'):
        return 'UDINT', 4
    if varname.startswith('f'):
        return 'LREAL', 8
    # n/i prefix is ambiguous (DINT vs UDINT) — omitted intentionally.
    # Falls through to DTYP map or record-type fallback, which correctly
    # uses asynInt32 -> DINT mapping matching what EPICS asyn expects.
    if varname.startswith('u'):
        return 'UDINT', 4
    if varname.startswith('w'):
        return 'UINT', 2
    if varname.startswith('e'):
        return 'INT', 2
    # struct/FB instances — unknown size, do not infer
    if varname.startswith('st') or varname.startswith('fb'):
        return None, None

    return None, None


def infer_type(record, symbol):
    fields = record["fields"]
    dtyp   = fields.get("DTYP", "")
    rtype  = record["rtype"]

    # ── record type BOOL — highest priority ──────────────────────────
    # bi/bo are always BOOL regardless of symbol name or DTYP
    if rtype in ("bi", "bo"):
        return "BOOL", 1

    # ── PLC naming convention — before DTYP ──────────────────────────
    # Resolves ambiguity: both BOOL and DINT map to asynInt32 in EPICS
    # b-prefix wins over asynInt32 -> DINT mapping
    plc_type, plc_size = get_plc_type_from_name(symbol)
    if plc_type is not None:
        return plc_type, plc_size

    # ── waveform char array ───────────────────────────────────────────
    if rtype == "waveform" and fields.get("FTVL") == "CHAR":
        nelm = int(fields.get("NELM", "1"))
        return "CHAR[{}]".format(nelm), nelm + 1  # +1: TwinCAT STRING(N) = N+1 bytes

    # ── waveform other element types ──────────────────────────────────
    if rtype == "waveform":
        ftvl = fields.get("FTVL", "LONG")
        nelm = int(fields.get("NELM", "1"))
        ftvl_map = {
            "DOUBLE": ("LREAL", 8),
            "FLOAT":  ("REAL",  4),
            "LONG":   ("DINT",  4),
            "SHORT":  ("INT",   2),
            "ULONG":  ("UDINT", 4),
            "USHORT": ("UINT",  2),
            "CHAR":   ("CHAR_ARRAY", 1),
            "UCHAR":  ("USINT", 1),
        }
        base_type, base_size = ftvl_map.get(ftvl, ("DINT", 4))
        return "{}[{}]".format(base_type, nelm), nelm * base_size

    # ── DTYP map ──────────────────────────────────────────────────────
    if dtyp in DTYPE_MAP:
        dtype, size = DTYPE_MAP[dtyp]
        if dtype == "CHAR_ARRAY":
            nelm = int(fields.get("NELM", "1"))
            return "CHAR[{}]".format(nelm), nelm + 1  # +1: TwinCAT STRING(N) = N+1 bytes
        return dtype, size

    # ── record type fallback ──────────────────────────────────────────
    if rtype in ("longin", "longout"):          return "DINT",   4
    if rtype in ("ai", "ao"):                   return "LREAL",  8
    if rtype in ("mbbi", "mbbo"):               return "UINT",   2
    if rtype in ("mbbiDirect", "mbboDirect"):   return "UDINT",  4
    if rtype in ("stringin", "stringout"):      return "STRING", 81
    if rtype in ("int64in", "int64out"):        return "LINT",   8

    raise RuntimeError(
        "Cannot infer datatype for record '{}' "
        "(rtype={}, DTYP={}, symbol={})".format(
            record["name"], rtype, dtyp, symbol))


def build_symbol_dict(records):
    symbols = {}
    skipped = []
    for r in records:
        fields = r["fields"]
        link   = fields.get("INP") or fields.get("OUT")
        if not link:
            continue
        symbol = extract_symbol(link)
        if not symbol:
            continue
        try:
            dtype, size = infer_type(r, symbol)
        except RuntimeError as e:
            skipped.append(str(e))
            continue
        pv = r["name"]
        if pv not in symbols:
            symbols[pv] = {
                "symbol":   symbol,
                "datatype": dtype,
                "size":     size,
                "name":     r["name"]
            }
    return symbols, skipped


def main():
    text    = Path(DB_FILE).read_text()
    records = parse_records(text)
    print("Parsed {} records from {}".format(len(records), DB_FILE))

    symbols, skipped = build_symbol_dict(records)

    if skipped:
        print("\nWARNING: {} records skipped (unknown type):".format(len(skipped)))
        for s in skipped:
            print("  ", s)

    Path(OUT_FILE).write_text(json.dumps(symbols, indent=2))
    print("\nGenerated {} with {} symbols".format(OUT_FILE, len(symbols)))
    print(json.dumps(symbols, indent=2))


if __name__ == "__main__":
    main()
