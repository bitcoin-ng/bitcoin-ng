# Configuring and Developing with BNG

To configure and build BNG from source, you can use the following commands in your terminal:

```
cd bitcoin-ng
./autogen.sh && ./configure && make
```

This will set up the build environment and compile the BNG codebase.

# Running tests


## Build

Recommended build commands (without GUI):

```zsh
./autogen.sh
./configure --without-gui
make -j"$(nproc)"
```

Optional reproducible toolchain via `depends`:

```zsh
make -C depends -j"$(nproc)"
CONFIG_SITE="$PWD/depends/x86_64-pc-linux-gnu/share/config.site" ./configure --prefix="$PWD/depends/x86_64-pc-linux-gnu" --without-gui
make -j"$(nproc)"
```

## Unit tests

Run C++ unit tests:

```zsh
make check
# or directly
./src/test/test_bitcoin --log_level=test_suite
```

## Functional tests (magic bytes)

These Python tests exercise P2P headers and network magic handling.

Run specific tests:

```zsh
./test/functional/test_runner.py p2p_invalid_messages.py
./test/functional/test_runner.py p2p_v2_transport.py
./test/functional/test_runner.py p2p_v2_misbehaving.py
```

Additional relevant checks:

```zsh
./test/functional/test_runner.py feature_reindex.py
./test/functional/test_runner.py feature_signet.py
```

Tip: to run multiple tests in parallel, add `-j"$(nproc)"` to `test_runner.py`.

## If you changed network magic

- Update test constants in `test/functional/test_framework/messages.py` (`MAGIC_BYTES` for `main`, `testnet`, `signet`, `regtest`).
- Adjust tests that hardcode magic where applicable (e.g., `feature_loadblock.py` uses `netmagic=fabfb5da`).
- Affected helpers: `test_framework/p2p.py` reads `MAGIC_BYTES` and validates headers; mismatches will cause intentional disconnects in tests.

