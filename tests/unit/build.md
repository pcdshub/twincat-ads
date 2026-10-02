# Building and running the unit tests

The GoogleTest suite under `tests/unit/` exercises `adsAsynPortDriver`
against a Python ADS test server. You can build and run it two ways: a native
`make` build with the repository-local Pixi environment for the server, or
inside the CI Docker image.

Both need the pyads test server on `127.0.0.1:48898` and a driver library built
with `-DADS_UNIT_TEST`. That flag compiles in the test hooks `setGlobalInstance`
and `g_callbackCount` that the test binaries link against.

## Native build with make

### Prerequisites

- EPICS Base **R7.0.6.1-2.0** and asyn **R4.42-1.0.0** at the paths in the
  example below. The default R7.0.3.1 Base is too old for APIs this branch
  already uses.
- C++14-capable compiler (the driver, GTest module, and unit tests use C++14).
- Pixi for the project-local pyads test-server environment; it is not needed
  for the config-only test.

### Exact native steps

Run from the `twincat-ads/` repo root.

1. Initialize all project submodules:

  ```bash
  git submodule update --init --recursive
  ```

2. Create local EPICS path files. These are git-ignored; adjust the paths if
  your installation differs. `RELEASE.local` must set a literal `EPICS_BASE`
  because the EPICS Release parser does not accept that value from the command
  line.

  ```bash
  cp RELEASE_SITE.example RELEASE_SITE
  cp RELEASE.local.example RELEASE.local
  ```

  For this host, `RELEASE_SITE` should use
  `/cds/group/pcds/epics`, `R7.0.6.1-2.0`, and its matching `modules`
  directory. Set `EPICS_BASE` in `RELEASE.local` to
  `/cds/group/pcds/epics/base/R7.0.6.1-2.0`.

3. Build/install the local EPICS GTest v1.0.1 module and the driver with the
  test hooks. Clean first so previously compiled non-test objects cannot be
  reused:

  ```bash
  make -C adsApp clean
  make gtest-local
  make -C adsApp 'USR_CXXFLAGS=-std=c++14 -DCONFIG_DEFAULT_LOGLEVEL=1 -DADS_UNIT_TEST'
  ```

4. Build all GoogleTest executables:

  ```bash
  make -C tests/unit clean
  make -C tests/unit
  ```

5. Install the locked Pixi environment in this repo (separate from
  `ads-actions`) for the ADS test server:

  ```bash
  pixi install
  pixi run python -c 'import pyads; print(pyads.__version__)'
  ```

6. To run only the config parser tests (no server needed):

  ```bash
  cd tests/unit/O.rhel9-x86_64
  ./test_ads_ioc_config
  ```

  Expected: `6 tests` pass.

7. To run `test_basic` and the full IOC lifecycle suite, start the server in
  one terminal and leave it running:

  ```bash
  pixi run test-server
  ```

  Wait for `[ads_test_server] Listening on 127.0.0.1:48898`, then, from the
  repository root in another terminal, run:

  ```bash
  pixi run unit-tests
  ```

  Expected: `test_basic` 1/1, `test_ads_ioc_config` 6/6, and
  `test_ioc_lifecycle` 13/13, with `pixi run unit-tests` exiting 0. TAP output
  is written under `tests/unit/O.rhel9-x86_64/`. The lifecycle fixture clears
  the test-only global driver pointer after destruction so the EPICS
  `iocShutdown` hook does not access a stale instance during process exit.

The lifecycle suite can take time because it resolves the static snapshot's
many symbol handles. Do not invoke it without the server: the driver startup
waits for ADS connectivity. For a direct lifecycle run after starting the
server, use `./test_ioc_lifecycle` from `tests/unit/O.rhel9-x86_64/`.

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
make -C tests/unit clean

python3 tests/server/ads_test_server.py --json testIOC/iocBoot/ioc-TestIOC/ads_symbol_dict.json &
sleep 2

make -C adsApp 'USR_CXXFLAGS=-std=c++14 -DCONFIG_DEFAULT_LOGLEVEL=1 -DADS_UNIT_TEST'
make -C tests/unit
make -C tests/unit runtests
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
