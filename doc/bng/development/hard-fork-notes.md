# Hard fork notes (BNG)

BNG is a hard fork of Bitcoin Core. That means a lot of upstream test vectors and literals are *intentionally wrong* for BNG, because they bake in Bitcoin’s network identity.

This doc is meant to be linked from GIPs and PRs so we don’t repeat the same guidance everywhere.

## Network-identity changes ripple widely

If a change touches any of these, expect follow-up work in tests and tooling:

- P2P message start bytes (“magic bytes”)
- Default ports
- Base58 prefixes (P2PKH, P2SH, WIF)
- Bech32 HRPs
- BIP32 extended key version bytes (the 4-byte version prefix; affects the Base58Check-encoded `xpub`/`xprv`-like strings)
- Genesis block (and any assumptions keyed off it)

## Test guidance for forks

1. Prefer testing *bytes/structures* over network-encoded strings.
   - For example: test BIP32 derivation against raw key material and chain code.
   - Only assert on Base58/Bech32 strings in tests that specifically validate encoding/decoding.

2. When you must test network-encoded strings, make the test parameterized by chain.
   - Encoding functions typically depend on the active chain parameters.
   - Ensure tests select the intended chain params up front.

3. When porting upstream vectors, separate “core math” from “network serialization”.
   - Many upstream vectors (BIP32, address examples) combine both.
   - In a hard fork, the underlying derivation may still be correct while the serialized string differs.

## Common failure mode: BIP32 vectors

Upstream BIP32 test vectors usually assert on the Base58Check-encoded extended key strings.

Those strings include a 4-byte version prefix (Bitcoin’s `xpub`/`xprv` versions), so if BNG defines different version bytes for extended keys, the encoded strings will differ even when the key material is identical.

Fix options (prefer in this order):

- Adjust the unit tests to compare decoded payloads (depth, fingerprint, child number, chain code, key bytes), and only separately test that encoding round-trips for *BNG’s* version bytes.
- Regenerate expected strings for BNG by re-encoding the same payload with BNG’s version bytes.

## Where to look when updating vectors

- C++ unit tests under `src/test/`
- JSON vectors under `src/test/data/`
- Python functional framework constants under `test/functional/test_framework/`

## Quick commands

```bash
# Run just one unit test suite
ctest --test-dir build -R <suite_name> --output-on-failure

# Run a single Boost.Test suite directly
./build/bin/test_bitcoin --run_test=<suite_name> --log_level=test_suite
```
