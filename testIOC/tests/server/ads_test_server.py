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
import select
import signal
import struct
import sys
import threading
import time

try:
    from pyads.testserver import AdsTestServer, AdvancedHandler, PLCVariable
    from pyads.testserver.testserver import AdsClientConnection
    from pyads.testserver.handler import AmsResponseData
    from pyads import constants
except ImportError:
    sys.exit("pyads is required: pip install pyads")

# ── SUMUP_READWRITE constant (0xF082) — not in pyads constants ───────────────
ADSIGRP_SUMUP_READWRITE = 0xF082


class AdsClientConnectionFixed(AdsClientConnection):
    """
    Fixes TCP fragmentation in pyads testserver and adds server-initiated
    DEVICENOTE push support for ADS notifications.

    The original AdsClientConnection reads at most 4096 bytes per recv()
    call, which truncates large AMS packets (e.g. SUMUP_READWRITE with
    500 symbols). This subclass reads the full AMS TCP header first to
    determine the payload length, then loops until all bytes are received.

    send_device_notification() accepts the full AMS address per call so
    that each notification is delivered to the correct AMS port on the
    driver side (the port that registered the notification).
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._notif_queue = []
        self._notif_lock  = threading.Lock()

    def send_device_notification(self, notif_handle: int, value: bytes,
                                 client_net_id: bytes, client_port: int,
                                 server_net_id: bytes, server_port: int) -> None:
        """Queue a DEVICENOTE AMS packet for this connection."""
        with self._notif_lock:
            self._notif_queue.append((notif_handle, value,
                                      client_net_id, client_port,
                                      server_net_id, server_port))

    def _flush_notifications(self) -> None:
        """Send all queued DEVICENOTE packets to the client."""
        with self._notif_lock:
            queue = list(self._notif_queue)
            self._notif_queue.clear()

        for (notif_handle, value,
             client_net_id, client_port,
             server_net_id, server_port) in queue:
            try:
                self._send_devicenote(
                    client_net_id, client_port,
                    server_net_id, server_port,
                    notif_handle, value
                )
            except Exception:
                pass

    def _send_devicenote(self, target_net_id: bytes, target_port: int,
                         source_net_id: bytes, source_port: int,
                         notif_handle: int, value: bytes) -> None:
        """Build and send a single DEVICENOTE AMS packet."""
        import time as _time

        # Windows FILETIME: 100ns intervals since 1601-01-01
        FILETIME_EPOCH_DIFF = 116444736000000000
        filetime = int(_time.time() * 1e7) + FILETIME_EPOCH_DIFF

        # ADS/AdsLib NotificationDispatcher::Run() parses the AMS data section as:
        #   cbLength(4)  nStamps(4)
        #     AdsStampHeader:        nTimeStamp(8)   nSamples(4)
        #       AdsNotificationSample: hNotification(4)  cbSampleSize(4)  data
        # AoEHeader.length() is prepended into the ring by ReceiveNotification(),
        # so it must NOT be part of this payload. The per-stamp nSamples count and
        # the leading cbLength were both previously missing -> RingBuffer mis-parse.
        stamp = (
            struct.pack("<Q", filetime)        # nTimeStamp
            + struct.pack("<I", 1)             # nSamples (one sample in this stamp)
            + struct.pack("<I", notif_handle)  # hNotification
            + struct.pack("<I", len(value))    # cbSampleSize
            + value                            # sample data
        )
        payload = (
            struct.pack("<I", 4 + len(stamp))  # cbLength: bytes following this field
            + struct.pack("<I", 1)             # nStamps
            + stamp
        )

        # AoEHeader (32 bytes)
        aoe_header = (
            target_net_id                        # targetNetId  (6)
            + struct.pack("<H", target_port)     # targetPort   (2)
            + source_net_id                      # sourceNetId  (6)
            + struct.pack("<H", source_port)     # sourcePort   (2)
            + struct.pack("<H", 0x0008)          # cmdId DEVICENOTE (2)
            + struct.pack("<H", 0x0004)          # stateFlags AMS_REQUEST (2)
            + struct.pack("<I", len(payload))    # length       (4)
            + struct.pack("<I", 0)               # errorCode    (4)
            + struct.pack("<I", 0)               # invokeId     (4)
        )

        # AmsTcpHeader: reserved(2) + length(4)
        tcp_header = struct.pack("<HI", 0, len(aoe_header) + len(payload))

        self.client.sendall(tcp_header + aoe_header + payload)

    def run(self) -> None:
        """Listen for data, reassembling fragmented AMS packets."""
        self._run = True
        self._registered = False

        while self._run:
            ready, _, _ = select.select([self.client], [], [], 0.05)

            # ── Flush pending notifications every loop ────────────────
            self._flush_notifications()

            if not ready:
                continue

            # ── Read full AMS TCP header (6 bytes) ────────────────────
            header = b""
            while len(header) < 6:
                chunk = self.client.recv(6 - len(header))
                if not chunk:
                    self.client.close()
                    self._run = False
                    break
                header += chunk

            if not self._run or len(header) < 6:
                break

            # ── Parse payload length from TCP header bytes 2–6 ────────
            payload_len = struct.unpack("<I", header[2:6])[0]

            # ── Read full payload, looping until complete ─────────────
            payload = b""
            while len(payload) < payload_len:
                to_read = min(65536, payload_len - len(payload))
                chunk   = self.client.recv(to_read)
                if not chunk:
                    self.client.close()
                    self._run = False
                    break
                payload += chunk

            if not self._run:
                break

            data = header + payload
            if len(data) < 38:
                continue

            # ── Register connection on first packet ───────────────────
            if not self._registered:
                self._registered = True
                if hasattr(self.handler, 'register_connection'):
                    self.handler.register_connection(self)

            request_packet = self.construct_request(data)
            self.server.request_history.append(request_packet)
            response = self.handler.handle_request(request_packet)
            if isinstance(response, AmsResponseData):
                response_bytes = self.construct_response(
                    response, request_packet
                )
                self.client.send(response_bytes)

        # Deregister on disconnect
        if hasattr(self.handler, 'deregister_connection'):
            self.handler.deregister_connection(self)


class AdsTestServerFixed(AdsTestServer):
    """
    AdsTestServer that uses AdsClientConnectionFixed for proper TCP
    packet reassembly — required for large SUMUP_READWRITE payloads.
    """

    def run(self) -> None:
        """Listen for incoming connections, spawning fixed client threads."""
        self._run = True
        self.server.listen(5)

        while self._run:
            ready, _, _ = select.select([self.server], [], [], 0.1)
            if ready:
                try:
                    client, address = self.server.accept()
                except Exception:
                    continue

                client_thread = AdsClientConnectionFixed(
                    handler=self.handler,
                    client=client,
                    address=address,
                    server=self,
                )
                client_thread.daemon = True
                client_thread.start()
                self.clients.append(client_thread)


class AdsTestHandler(AdvancedHandler):
    """
    Extended AdvancedHandler that adds support for:
    - ADSIGRP_SUMUP_READWRITE (0xF082) — batched resolveSymbolInfo/resolveSymbolHandles
    - ADSIGRP_SUMUP_READ (0xF080) — bulk read by handle
    - Plain READ with AoEReadResponseHeader fix
    - ADD_DEVICE_NOTIFICATION with handle-based lookup and per-handle AMS address
    - Server-initiated DEVICENOTE push when variables change
    """

    def __init__(self):
        super().__init__()
        self._connections = []
        self._conn_lock   = threading.Lock()
        # notif_handle → (client_net_id, client_port, server_net_id, server_port)
        self._notif_addrs = {}
        self._addr_lock   = threading.Lock()

    def register_connection(self, conn) -> None:
        with self._conn_lock:
            if conn not in self._connections:
                self._connections.append(conn)

    def deregister_connection(self, conn) -> None:
        with self._conn_lock:
            if conn in self._connections:
                self._connections.remove(conn)

    def push_notification(self, notif_handle: int, value: bytes) -> None:
        """Push a value-change DEVICENOTE to all connected clients."""
        with self._addr_lock:
            addr = self._notif_addrs.get(notif_handle)
        if addr is None:
            return
        client_net_id, client_port, server_net_id, server_port = addr
        with self._conn_lock:
            conns = list(self._connections)
        for conn in conns:
            conn.send_device_notification(
                notif_handle, value,
                client_net_id, client_port,
                server_net_id, server_port
            )

    def handle_request(self, request) -> object:
        """Override to intercept ADDDEVICENOTE, plain READ, SUMUP variants."""
        import struct as _struct

        command_id = _struct.unpack("<H", request.ams_header.command_id)[0]

        # ── ADD_DEVICE_NOTIFICATION ────────────────────────────────────
        if command_id == constants.ADSCOMMAND_ADDDEVICENOTE:
            data = request.ams_header.data
            index_group, index_offset = _struct.unpack_from("<II", data[:8])

            # Extract per-request AMS addresses for DEVICENOTE targeting
            client_net_id = bytes(request.ams_header.source_net_id)
            client_port   = _struct.unpack_from("<H",
                                request.ams_header.source_port)[0]
            server_net_id = bytes(request.ams_header.target_net_id)
            server_port   = _struct.unpack_from("<H",
                                request.ams_header.target_port)[0]

            try:
                if index_group == constants.ADSIGRP_SYM_VALBYHND:
                    var = self.get_variable_by_handle(index_offset)
                else:
                    var = self.get_variable_by_indices(index_group, index_offset)
                notif_handle = var.register_notification()
                with self._addr_lock:
                    self._notif_addrs[notif_handle] = (
                        client_net_id, client_port,
                        server_net_id, server_port
                    )
                content = notif_handle.to_bytes(4, byteorder='little')
            except Exception:
                content = b"\x00\x00\x00\x00"

            return self._build_write_response(request, content)

        # ── Plain READ ─────────────────────────────────────────────────
        if command_id == constants.ADSCOMMAND_READ:
            data         = request.ams_header.data
            index_group  = _struct.unpack_from("<I", data, 0)[0]
            index_offset = _struct.unpack_from("<I", data, 4)[0]
            read_length  = _struct.unpack_from("<I", data, 8)[0]

            try:
                if index_group == constants.ADSIGRP_SYM_VALBYHND:
                    var = self.get_variable_by_handle(index_offset)
                else:
                    var = self.get_variable_by_indices(index_group, index_offset)
                value     = var.value[:read_length]
                read_data = struct.pack("<II", 0, len(value)) + value
            except Exception:
                read_data = struct.pack("<II", 0x710, 0)

            return self._build_response(request, read_data)

        # ── READWRITE (SUMUP_READWRITE / SUMUP_READ) ───────────────────
        if command_id == constants.ADSCOMMAND_READWRITE:
            data         = request.ams_header.data
            index_group  = _struct.unpack_from("<I", data, 0)[0]
            index_offset = _struct.unpack_from("<I", data, 4)[0]
            write_length = _struct.unpack_from("<I", data, 12)[0]
            write_data   = data[16: 16 + write_length]

            if index_group == ADSIGRP_SUMUP_READWRITE:
                read_data = self._handle_sumup_readwrite(index_offset, write_data)
                return self._build_response(request, read_data)

            elif index_group == constants.ADSIGRP_SUMUP_READ:
                read_data = self._handle_sumup_read(index_offset, write_data)
                return self._build_response(request, read_data)

        try:
            return super().handle_request(request)
        except KeyError:
            import struct as _s
            state = _s.unpack("<H", request.ams_header.state_flags)[0]
            state = _s.pack("<H", state | 0x0001)
            return AmsResponseData(state, _s.pack("<I", 0x710), b"")

    def _handle_sumup_readwrite(self, num_requests: int,
                                write_data: bytes) -> bytes:
        """Process ADSIGRP_SUMUP_READWRITE (0xF082)."""
        header_bytes = num_requests * 16
        rq_list = []
        for i in range(num_requests):
            off = i * 16
            ig  = struct.unpack_from("<I", write_data, off)[0]
            io  = struct.unpack_from("<I", write_data, off + 4)[0]
            rl  = struct.unpack_from("<I", write_data, off + 8)[0]
            wl  = struct.unpack_from("<I", write_data, off + 12)[0]
            rq_list.append((ig, io, rl, wl))

        write_payload = write_data[header_bytes:]
        status_block  = b""
        data_block    = b""
        write_offset  = 0

        for ig, io, rl, wl in rq_list:
            sub_write    = write_payload[write_offset: write_offset + wl]
            write_offset += wl

            try:
                if ig == constants.ADSIGRP_SYM_INFOBYNAMEEX:
                    sym_name = sub_write.decode("utf-8", errors="replace")
                    var      = self.get_variable_by_name(sym_name)
                    packed   = var.get_packed_info()
                    status_block += struct.pack("<II", 0, len(packed))
                    data_block   += packed

                elif ig == constants.ADSIGRP_SYM_HNDBYNAME:
                    sym_name = sub_write.decode("utf-8",
                                                errors="replace").rstrip("\x00")
                    var    = self.get_variable_by_name(sym_name)
                    handle = struct.pack("<I", var.handle)
                    status_block += struct.pack("<II", 0, len(handle))
                    data_block   += handle

                else:
                    var = self.get_variable_by_indices(ig, io)
                    val = var.value[:rl]
                    status_block += struct.pack("<II", 0, len(val))
                    data_block   += val

            except Exception:
                status_block += struct.pack("<II", 0x710, 0)
                data_block   += bytes(rl)

        read_data = status_block + data_block
        return struct.pack("<II", 0, len(read_data)) + read_data

    def _handle_sumup_read(self, num_requests: int,
                           write_data: bytes) -> bytes:
        """Handle ADSIGRP_SUMUP_READ (0xF080) — bulk read by handle."""
        status_block = b""
        data_block   = b""

        for i in range(num_requests):
            off  = i * 12
            ig   = struct.unpack_from("<I", write_data, off)[0]
            io   = struct.unpack_from("<I", write_data, off + 4)[0]
            size = struct.unpack_from("<I", write_data, off + 8)[0]

            try:
                if ig == constants.ADSIGRP_SYM_VALBYHND:
                    var = self.get_variable_by_handle(io)
                else:
                    var = self.get_variable_by_indices(ig, io)
                val = var.value[:size]
                if len(val) < size:
                    val = val + bytes(size - len(val))
                status_block += struct.pack("<I", 0)
                data_block   += val
            except Exception:
                status_block += struct.pack("<I", 0x710)
                data_block   += bytes(size)

        read_data = status_block + data_block
        return struct.pack("<II", 0, len(read_data)) + read_data

    def _build_response(self, request, read_data: bytes) -> object:
        """Build an AMS READ response (AoEReadResponseHeader format)."""
        import struct as _s
        state = _s.pack("<H", _s.unpack("<H", request.ams_header.state_flags)[0] | 0x0001)
        return AmsResponseData(state, request.ams_header.error_code, read_data)

    def _build_write_response(self, request, content: bytes) -> object:
        """Build an AMS WRITE-style response (error_code(4) + content)."""
        import struct as _s
        state = _s.pack("<H", _s.unpack("<H", request.ams_header.state_flags)[0] | 0x0001)
        return AmsResponseData(state, request.ams_header.error_code,
                               b"\x00\x00\x00\x00" + content)


class AdsPLCVariable(PLCVariable):
    """
    PLCVariable subclass that:
    - Uses null terminators in get_packed_info() matching TwinCAT wire format
    - Pushes DEVICENOTE to connected clients via handler when write() is called
    """

    def __init__(self, *args, handler=None, **kwargs):
        super().__init__(*args, **kwargs)
        self._handler = handler

    def set_handler(self, handler) -> None:
        self._handler = handler

    def get_packed_info(self) -> bytes:
        if self.comment is None:
            self.comment = ""
        name_bytes        = self.name.encode("utf-8")
        symbol_type_bytes = self.symbol_type.encode("utf-8")
        comment_bytes     = self.comment.encode("utf-8")
        entry_length = (
            6 * 4 + 3 * 2
            + len(name_bytes)        + 1
            + len(symbol_type_bytes) + 1
            + len(comment_bytes)
        )
        return (
            struct.pack(
                "<IIIIIIHHH",
                entry_length,
                self.index_group,
                self.index_offset,
                self.size,
                self.ads_type,
                0,
                len(name_bytes),
                len(symbol_type_bytes),
                len(comment_bytes),
            )
            + name_bytes        + b"\x00"
            + symbol_type_bytes + b"\x00"
            + comment_bytes
        )

    def write(self, value: bytes, request=None) -> None:
        """Override write to push DEVICENOTE to connected clients."""
        super().write(value, request)
        if self._handler is not None and self.notifications:
            for notif_handle in self.notifications:
                self._handler.push_notification(notif_handle, value)


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
    "BOOL":  "GVL_Logger.bTrickleTripped",
    "DINT":  "GVL_Logger.iLogPort",
    "LREAL": "Main.M1.fBacklash",
    "INT":   "Main.fbMotionArr[10].fbMotionAxis."
             "fbParMgrNc.fbNcSoftAxis.p.Backlash.eAi",
    "NOTIF": "Main.fbMotionArr[10].stMotionStatus.fActPosition",  # TS_MS=10
}


def resolve_ads_type(datatype: str):
    """Resolve a TwinCAT datatype string to (ads_type, symbol_type)."""
    base = datatype.split("[")[0].strip()
    if base in _BASE_TYPE_MAP:
        ads_type, symbol_type = _BASE_TYPE_MAP[base]
        if "[" in datatype:
            symbol_type = datatype
        return ads_type, symbol_type
    return constants.ADST_UINT8, datatype


def load_symbols(json_path: str, handler=None) -> list:
    """
    Load symbols from ads_symbol_dict.json, deduplicated by PLC symbol name.

    The same PLC symbol may appear twice in the JSON (readback + setpoint
    record), but the server only needs one variable per PLC symbol.
    """
    with open(json_path) as f:
        data = json.load(f)

    variables    = []
    seen_symbols = {}
    skipped      = 0

    for epics_name, entry in data.items():
        symbol   = entry.get("symbol", "")
        datatype = entry.get("datatype", "")
        size     = entry.get("size", 0)

        if not symbol or not datatype or size <= 0:
            skipped += 1
            continue

        key = symbol.lower()
        if key in seen_symbols:
            continue
        seen_symbols[key] = True

        ads_type, symbol_type = resolve_ads_type(datatype)
        variables.append(
            AdsPLCVariable(
                name=symbol,
                value=bytes(size),
                ads_type=ads_type,
                symbol_type=symbol_type,
                handler=handler,
            )
        )

    print(f"[ads_test_server] Loaded {len(variables)} unique symbols "
          f"({skipped} skipped, "
          f"{len(data) - len(variables) - skipped} duplicates) "
          f"from {json_path}")
    return variables


def start_variable_updater(handler,
                           interval: float = 0.1) -> threading.Thread:
    """
    Periodically update well-known test symbols with incrementing values.

    Uses var.write() so that registered notification handles receive
    DEVICENOTE packets via AdsPLCVariable.write() → push_notification().
    """
    stop_event = threading.Event()

    def _update():
        counter = 0
        while not stop_event.is_set():
            try:
                # BOOL — alternate True/False
                handler.get_variable_by_name(TEST_SYMBOLS["BOOL"]).write(
                    struct.pack("<?", bool(counter % 2)))

                # DINT — incrementing int32
                handler.get_variable_by_name(TEST_SYMBOLS["DINT"]).write(
                    struct.pack("<i", counter % 0x7FFFFFFF))

                # LREAL — incrementing float64
                handler.get_variable_by_name(TEST_SYMBOLS["LREAL"]).write(
                    struct.pack("<d", float(counter)))

                # INT — incrementing int16
                handler.get_variable_by_name(TEST_SYMBOLS["INT"]).write(
                    struct.pack("<h", counter % 0x7FFF))

                # NOTIF — notification symbol (TS_MS=10), LREAL
                handler.get_variable_by_name(TEST_SYMBOLS["NOTIF"]).write(
                    struct.pack("<d", float(counter)))

            except Exception as e:
                print(f"[ads_test_server] Updater error: {e}")

            counter += 1
            stop_event.wait(interval)

    t = threading.Thread(target=_update, daemon=True, name="variable-updater")
    t.stop_event = stop_event
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

    handler = AdsTestHandler()
    for var in load_symbols(json_path, handler=handler):
        handler.add_variable(var)

    server = AdsTestServerFixed(handler=handler, logging=False)
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
