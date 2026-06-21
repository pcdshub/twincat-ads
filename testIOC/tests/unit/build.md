# Building and running the unit tests

The GoogleTest suite under `testIOC/tests/unit/` exercises `adsAsynPortDriver`
against a Python ADS test server. You can build and run it two ways: a native
`make` build, or inside the CI Docker image.

Both need the pyads test server on `127.0.0.1:48898` and a driver library built
with `-DADS_UNIT_TEST`. That flag compiles in the test hooks `setGlobalInstance`
and `g_callbackCount` that the test binaries link against.

## Native build with make

### Prerequisites

- An EPICS environment reachable through `RELEASE_SITE`. `configure/RELEASE.local`
  resolves it at `$(TOP)/../../RELEASE_SITE` (two levels above the repo). On the
  PCDS NFS machines that points at
  `/cds/group/pcds/epics/<base>/modules/RELEASE_SITE`.
- `pyads` for the test server: `pip install pyads`.

### Steps

Run from the repo root.

```bash
# 1. Build the driver library WITH the unit-test hooks.
make -C adsApp USR_CXXFLAGS+="-DADS_UNIT_TEST -DCONFIG_DEFAULT_LOGLEVEL=1"

# 2. Build the test binaries.
make -C testIOC/tests/unit
```

If you built the driver earlier without the flag, `make` may skip the recompile
and the test link fails on `undefined reference to setGlobalInstance`. Force it:

```bash
rm -f adsApp/src/O.*/adsAsynPortDriver.o lib/*/libads.*
make -C adsApp USR_CXXFLAGS+="-DADS_UNIT_TEST -DCONFIG_DEFAULT_LOGLEVEL=1"
```

Start the test server in a second terminal and leave it running:

```bash
python3 -u testIOC/tests/server/ads_test_server.py
# wait for: [ads_test_server] Listening on 127.0.0.1:48898
```

Run the suite from the first terminal:

```bash
make -C testIOC/tests/unit runtests
```

Expected: `test_basic` 1/1 and `test_ioc_lifecycle` 13/13. TAP output lands in
`testIOC/tests/unit/O.<arch>/*.tap`.

For faster iteration, run one binary directly. Cd into the arch dir first so it
finds the DBD:

```bash
cd testIOC/tests/unit/O.rhel9-x86_64
./test_ioc_lifecycle
```

## Inside the CI Docker image

Run the suite inside the same image the CI `unit-tests` job uses. Mount your
checkout at the modules path so `RELEASE_SITE` resolves with no symlink.

Set these for your checkout and image, then open a shell in the container:

```bash
REPO=/abs/path/to/your/twincat-ads   # your checkout (repo root)
BASE=<epics-base-version>            # the image's EPICS base, the dir under /cds/group/pcds/epics/
VER=<module-version>                 # this checkout's version dir name
IMAGE=<docker-image-name>            # your local build, or ghcr.io/yanns2016/ioc-common-ads-ioc/ci:latest
MOD=/cds/group/pcds/epics/$BASE/modules/twincat-ads/$VER

docker run --rm -it -v "$REPO:$MOD" -w "$MOD" "$IMAGE" bash
```

Inside the container:

```bash
# Drop host build artifacts; the container shares the rhel9-x86_64 arch.
make -C adsApp clean
make -C testIOC/tests/unit clean

python3 testIOC/tests/server/ads_test_server.py --json testIOC/ads_symbols.json &
sleep 2

make -C adsApp USR_CXXFLAGS+="-DADS_UNIT_TEST -DCONFIG_DEFAULT_LOGLEVEL=1"
make -C testIOC/tests/unit
make -C testIOC/tests/unit runtests
```

The image already provides EPICS base, the module dependencies, and the
modules-level `RELEASE_SITE`, so no host EPICS setup is needed.

## Notes

- Start the server before the tests. The driver constructor blocks in a connect
  loop until the server answers, so a missing server hangs startup.
- Use one server instance, on port 48898. Kill a stale one before restarting.
- The container shares the host arch (`rhel9-x86_64`) and reuses the host
  `O.<arch>` build dirs. Run the `clean` steps first when mounting, and rebuild
  on the host before running natively again.
