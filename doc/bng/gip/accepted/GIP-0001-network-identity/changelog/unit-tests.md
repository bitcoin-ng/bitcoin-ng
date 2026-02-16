# Unit test adjustments (GIP-0001)

This note records test-suite changes made to keep the upstream unit tests meaningful after BNG’s network identity changes (GIP-0001).

**Date**: 2026-02-16

## Summary

- Updated tests that previously asserted Bitcoin-encoded strings (Base58/Bech32/WIF) to instead assert:
  - decoded payload/structures, and/or
  - encode/decode roundtrips under the active BNG chainparams.
- Added dedicated BNG unit tests under `src/test/bng/` that pin BNG network identity constants and guard against accidental drift.

## Changes

- **Bloom filter tests**
  - `DecodeSecret()` is network-prefix sensitive; tests now avoid depending on Bitcoin WIF literals.
  - Tests construct keys from raw 32-byte secret material (decoded via Base58Check payload) so behavior is stable under BNG prefixes.

- **Key and key I/O tests**
  - Upstream vectors embed Bitcoin address/WIF strings. Under BNG these are not valid “golden strings”.
  - Tests were adjusted to validate correctness via payload/scripts and then perform BNG encode/decode roundtrips.

- **BNG-specific identity tests**
  - Added suites to pin:
    - message start bytes (magic),
    - default ports,
    - Bech32 HRPs,
    - Base58 version bytes (including WIF),
    - BIP32 version bytes,
    - genesis `nBits` compatibility with `powLimit`.

## Files

- `src/test/bloom_tests.cpp`
- `src/test/key_io_tests.cpp`
- `src/test/key_tests.cpp`
- `src/test/bng/network_identity_values_tests.cpp`
- `src/test/bng/bloom_tests.cpp`
- `src/test/CMakeLists.txt`
- `doc/bng/gip/accepted/GIP-0001-network-identity/test/happy-tests.md`
