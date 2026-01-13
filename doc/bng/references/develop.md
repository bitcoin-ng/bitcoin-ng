# Development Guide

## Build

### Unix / macOS
```bash
./autogen.sh
./configure --without-gui  # Add --with-gui for QT wallet
make -j$(nproc)
```

### Windows (Cross-compile)
See `build_msvc/README.md` or use `depends`:
```bash
make -C depends -j$(nproc) HOST=x86_64-w64-mingw32
./configure --prefix=$PWD/depends/x86_64-w64-mingw32
make -j$(nproc)
```

## Testing

### Unit Tests (C++)
```bash
make check
# OR
./src/test/test_bitcoin
```

### Functional Tests (Python)
```bash
./test/functional/test_runner.py
```
Run specific tests:
```bash
./test/functional/test_runner.py p2p_invalid_messages.py
```

## Contribution
- **Code Style**: Follow existing C++17/20 patterns.
- **Formatting**: Run `clang-format -i` on modified files.
- **Pull Requests**: Reference the BNG-XXXX issue number.
