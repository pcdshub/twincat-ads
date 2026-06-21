# twincat-ads tests

What the test suite exercises, and how the pieces fit together.

## Layout

| Path | What it is |
|------|------------|
| `tests/unit/test_basic.cpp` | Smallest check: the driver constructs and connects. |
| `tests/unit/test_ioc_lifecycle.cpp` | Full IOC boot lifecycle, 13 GoogleTest stages against a live server. |
| `tests/server/ads_test_server.py` | pyads-based ADS test server; serves the real PLC symbol dict. |
| `tests/server/test_server_smoke.py` | Standalone smoke test of the server (read / write / live updates). |
| `tests/unit/build.md` | How to build and run the unit tests (native `make` and CI Docker image). |

The driver is built with `-DADS_UNIT_TEST`, which compiles in test-only hooks
(`setGlobalInstance`, `g_callbackCount`). Symbols and records come from the
`testIOC` submodule (`pcdshub/lcls-plc-tst-tcads`): a 26755-record `TestIOC.db`
and a 23368-symbol `ads_symbol_dict.json`.

## How the pieces connect

```mermaid
graph TD
    subgraph host["Test host / CI runner"]
        TB["GoogleTest binaries<br/>test_basic · test_ioc_lifecycle"]
        DRV["adsAsynPortDriver<br/>libads, built -DADS_UNIT_TEST"]
        EP["EPICS IOC core<br/>db · asyn · scan threads"]
        SRV["pyads ADS test server<br/>ads_test_server.py"]
    end
    JSON["ads_symbol_dict.json<br/>23368 symbols"]
    DB["TestIOC.db<br/>26755 records"]
    SUB["testIOC submodule<br/>lcls-plc-tst-tcads"]

    SUB --> JSON
    SUB --> DB
    TB --> DRV
    TB --> EP
    EP --> DRV
    DB -. dbLoadRecords .-> EP
    JSON -. served by .-> SRV
    DRV <-->|"ADS / AMS over TCP :48898"| SRV
```

The unit test process *is* a minimal IOC: it loads the DB, creates the driver,
and calls `iocInit()`. The driver talks ADS to the Python server exactly as it
would to a real TwinCAT PLC.

## Lifecycle stages

`test_ioc_lifecycle.cpp` shares one driver instance across all stages
(`SetUpTestSuite` / `TearDownTestSuite`). The stage numbers follow the driver's
boot sequence, so the list skips 4.

```mermaid
flowchart TD
    SU["SetUpTestSuite<br/>load DBD + DB · new driver · iocInit · settle"] --> S1
    S1["Stage 1 — Connection<br/>isConnected · ADS port open"] --> S2
    S2["Stage 2 — resolveSymbolInfo<br/>dict populated · LREAL/BOOL size + ADS type"] --> S3
    S3["Stage 3 — resolveSymbolHandles<br/>handles resolved · non-zero"] --> S5
    S5["Stage 5 — bulkOK<br/>bulkOK == 1 after iocInit (PINI fix)"] --> S6
    S6["Stage 6 — bulk read thread<br/>still connected after polling cycles"] --> S7
    S7["Stage 7 — notifications<br/>g_callbackCount advances (ON_CHANGE)"] --> S8
    S8["Stage 8 — runtime read<br/>readFloat64 of a real param succeeds"] --> S9
    S9["Stage 9 — disconnect / reconnect<br/>route preserved · reconnect succeeds"] --> TD
    TD["TearDownTestSuite<br/>scanStop · drain · delete driver"]
```

| Stage | Test(s) | Asserts |
|-------|---------|---------|
| 1 | `Stage1_ConnectedToServer`, `Stage1_AdsPortOpen` | Driver connected; local ADS port > 0. |
| 2 | `Stage2_SymbolDictPopulated`, `Stage2_LrealSymbolResolved`, `Stage2_BoolSymbolResolved` | Symbol dict filled; LREAL is 8 bytes / `ADST_REAL64`, BOOL is 1 byte / `ADST_BIT`. |
| 3 | `Stage3_HandlesAcquired`, `Stage3_AllTestSymbolsHaveHandles` | BOOL/DINT/LREAL each resolved to a non-zero handle. |
| 5 | `Stage5_BulkOKSet` | `bulkOK == 1` once the IOC is running. |
| 6 | `Stage6_DriverRemainsConnectedAfterPolling` | Still connected after bulk-read cycles. |
| 7 | `Stage7_CallbacksFired` | At least one notification callback fires (counter advances). |
| 8 | `Stage8_DirectReadSucceeds` | Direct `readFloat64` of a registered param returns `asynSuccess`. |
| 9 | `Stage9_DisconnectPreservesRoute`, `Stage9_ReconnectSucceeds` | ADS route survives disconnect; reconnect succeeds. |

`test_basic.cpp` is independent: it constructs a driver, asserts it connects,
and deletes it.

## Boot sequence and ADS exchange

What `iocInit()` triggers, and the ADS traffic between driver and server.

```mermaid
sequenceDiagram
    participant T as test (SetUp)
    participant E as EPICS core
    participant D as adsAsynPortDriver
    participant S as pyads server

    T->>E: dbLoadDatabase + registerRecordDeviceDriver
    T->>E: dbLoadRecords(TestIOC.db)
    T->>D: new adsAsynPortDriver(...)
    T->>E: iocInit()
    E->>D: drvUserCreate (per record link)
    D->>S: SUMUP_READWRITE · SYM_INFOBYNAMEEX
    S-->>D: symbol size + ADS type
    D->>S: SUMUP_READWRITE · SYM_HNDBYNAME
    S-->>D: symbol handles
    D->>S: ADDDEVICENOTE (I/O Intr inputs)
    Note over D,S: bulkOK = 1 · cyclic + bulk-read threads start
    loop polling
        D->>S: SUMUP_READ (read by handle)
        S-->>D: values
        S-)D: DEVICENOTE on change → g_callbackCount++
    end
```

Teardown is ordered to avoid a use-after-free: `scanStop()` halts the EPICS scan
threads and a short sleep drains in-flight processing **before** `delete driver`,
so no record callback dereferences a `paramInfo` while the destructor is freeing
it. The driver also rejects `adsReadParam`/`adsWriteParam` once `stopThreads_` is
set, as a second guard.

## CI

`.github/workflows/standard.yml` runs three jobs on push / PR.

```mermaid
flowchart LR
    P["push / pull_request"] --> J1 & J2 & J3
    J1["standard<br/>clang-format · cppcheck"]
    J2["smoke-test<br/>checkout +submodules<br/>pip install pyads<br/>run test_server_smoke.py"]
    J3["unit-tests · CI Docker image<br/>checkout +submodules<br/>start ads_test_server<br/>build driver -DADS_UNIT_TEST<br/>make + runtests"]
```

Both `smoke-test` and `unit-tests` check the `testIOC` submodule out, because the
symbol dict and DB live there. See [`unit/build.md`](unit/build.md) to run the
suite locally.
