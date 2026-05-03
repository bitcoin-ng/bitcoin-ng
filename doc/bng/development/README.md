# Development Guide

## Build

### Ubuntu / WSL notes
- Install common build dependencies:
	- `sudo apt update && sudo apt install -y build-essential pkg-config`
	- SQLite (needed for wallet/sqlite features and for CMake to configure):
		- `sudo apt install -y libsqlite3-dev`
	- Boost (CMake expects a modern Boost with CMake package config files):
		- `sudo apt install -y libboost-all-dev`
- **Multiprocess / IPC**: By default `-DENABLE_IPC=ON` and this requires Cap'n Proto.
	- Install on Ubuntu: `sudo apt update && sudo apt install -y capnproto libcapnp-dev`
### Unix / macOS
```bash
# This repository uses CMake (there is no top-level ./autogen.sh).

# Configure (no GUI)
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_GUI=OFF

# If you don't need multiprocess support (or you don't have Cap'n Proto installed):
# cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_GUI=OFF -DENABLE_IPC=OFF

# Build
cmake --build build -j$(nproc)

# Optional: install into a staging directory
# cmake --install build --prefix "$PWD/stage"
```

### Build outputs
After a successful build, the executables are written to `build/bin/` (relative to the repository root). Common ones include:
- `build/bin/bitcoind`
- `build/bin/bitcoin-node`
- `build/bin/bitcoin-cli`
- `build/bin/bitcoin-wallet`
- `build/bin/bitcoin-tx`
- `build/bin/bitcoin-util`

### Windows (Cross-compile)
See `doc/build-windows-msvc.md` (MSVC) or use `depends` (MinGW-w64 cross-compile):
```bash
make -C depends -j$(nproc) HOST=x86_64-w64-mingw32
cmake -S . -B build --toolchain depends/x86_64-w64-mingw32/toolchain.cmake
cmake --build build -j$(nproc)

# Optional: create an installer (requires NSIS)
# cmake --build build --target deploy
```

## Testing

### Unit Tests (C++)
```bash
# Equivalent to the old `make check` flow
ctest --test-dir build --output-on-failure -j$(nproc)

# List tests without running
ctest --test-dir build -N

# Run a subset
ctest --test-dir build -R wallet --output-on-failure

# Alternative: buildsystem test target
cmake --build build --target test -j$(nproc)

# OR run directly
./build/bin/test_bitcoin
```

### Functional Tests (Python)
```bash
python3 build/test/functional/test_runner.py
```
Run specific tests:
```bash
python3 build/test/functional/test_runner.py p2p_invalid_messages.py
```

## Contribution
- **Code Style**: Follow existing C++17/20 patterns.
- **Formatting**: Run `clang-format -i` on modified files.
- **Pull Requests**: Reference the BNG-XXXX issue number.
