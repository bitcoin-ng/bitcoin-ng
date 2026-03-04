# Unit test adjustments (GIP-0001)

This note records test-suite changes made to keep the upstream unit tests meaningful after BNG’s network identity changes (GIP-0001).

**Date**: 2026-02-18

## Summary

- Updated tests that previously asserted Bitcoin-encoded strings (Base58/Bech32/WIF) to instead assert:
  - decoded payload/structures, and/or
  - encode/decode roundtrips under the active BNG chainparams.
- Added dedicated BNG unit tests under `src/test/bng/` that pin BNG network identity constants and guard against accidental drift.
- Kept descriptor unit tests “happy” on BNG by updating BNG-specific expectations (including `DescriptorID()` outputs) and enabling all split descriptor-test chunks.

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

- **Descriptor tests**
  - Updated expected `DescriptorID()` values where BNG diverges from upstream.
  - Enabled all split descriptor-test chunks so `descriptor_tests` remains a reliable “happy path” signal.

- **Peer connection tests**
  - `net_peer_connection_tests` previously hardcoded Bitcoin’s default port (`8333`) in debug-log expectations.
  - Updated to use `Params().GetDefaultPort()` so the test remains correct under BNG’s chainparams (e.g. `9333` on main).

## Files

- `src/test/bloom_tests.cpp`
- `src/test/key_io_tests.cpp`
- `src/test/key_tests.cpp`
- `src/test/bng/network_identity_values_tests.cpp`
- `src/test/bng/bloom_tests.cpp`
- `src/test/CMakeLists.txt`
- `src/test/descriptor_tests.cpp`
- `src/test/net_peer_connection_tests.cpp`
- `doc/bng/gip/accepted/GIP-0001-network-identity/test/happy-tests.md`

---

**Date**: 2026-02-27

## Summary

- Updated unit tests to stay correct under BNG’s network identity (Bech32 HRP) and BNG’s modified regtest genesis/chain.
- Aligned regtest AssumeUTXO snapshot lookup tests with the chainparams-provided blockhash at height 110 (avoid hard-coding upstream regtest hashes).

## Changes

- **`script_standard_tests`**
  - Upstream Taproot vector expectations include Bitcoin Bech32m addresses (`bc1p...`).
  - Tests now re-encode expected addresses under the active chainparams (so BNG HRPs like `bng...` are accepted where appropriate).

- **`validation_tests` (AssumeUTXO)**
  - Avoid hard-coding an upstream regtest base blockhash.
  - The test now looks up the AssumeUTXO entry by using the `blockhash` returned from `AssumeutxoForHeight(110)`.

## Files

- `src/test/script_standard_tests.cpp`
- `src/test/validation_tests.cpp`
