# `descriptor_tests` step-by-step bring-up (GIP-0001)

Goal: make `descriptor_tests` green by enabling the test file in **small chunks**, fixing failures per chunk, then un-commenting the next chunk.

## How to run

- From an existing build dir:
  - `cd build`
  - `ctest -R '^descriptor_tests$' --output-on-failure`

## Chunk plan (starting around `descriptor_test`)

The test was split into multiple Boost test cases to make chunk bring-up easier.

- `descriptor_test_1`: Chunks 1–4 (currently enabled)
- `descriptor_test_2`: Chunk 5 (currently disabled behind `#if 0`)
- `descriptor_test_3`: Chunk 6 (currently disabled behind `#if 0`)
- `descriptor_test_4`: Chunk 7 (currently disabled behind `#if 0`)

- [x] Chunk 1: Basic single-key compressed (combo/pk/pkh/wpkh/sh(wpkh)/tr) + one invalid pubkey case.
- [x] Chunk 2: Key-origin parse errors + single-key uncompressed + hybrid-key rejection + unconventional single-key constructions.
- [x] Chunk 3: BIP32 derivations (xprv/xpub and mixed xpub/const-pubkey cases).
- [x] Chunk 4: Multipath BIP32 derivations.
- [ ] Chunk 5: Multisig constructions (first post-multipath block; expect DescriptorID changes due to WIF re-encoding).

## Continuation prompt (Chunk 5+)

1) Enable the next test case (`descriptor_test_2`) by changing its `#if 0` guard to `#if 1` (or removing the guard).
2) Rebuild and run:
  - `cmake --build . -j $(nproc)`
  - `ctest -R '^descriptor_tests$' --output-on-failure`
3) For failures:
  - If it’s a `DescriptorID` mismatch: record the new BNG expected `uint256{...}` constant.
  - If it’s a checksum mismatch: decide whether the test is validating the checksum itself (update expected) vs. it’s incidental (use the helper that preserves checksum).
4) Repeat for `descriptor_test_3` (Miniscript) and `descriptor_test_4` (inference + MuSig2).

## Notes on likely failure modes

- **DescriptorID mismatches**: these are expected when descriptors embed Base58Check-encoded xpub/xprv/wif that are re-encoded for BNG. Fix approach: ensure the ID is checked against the **post-fixup descriptor string**, or compute/record BNG-specific expected IDs.
- **Descriptor checksums**: rewriting keys changes payload → checksum changes. For tests involving explicit checksums, prefer `ReencodeWifKeysPreserveChecksum()` only when the checksum itself is not being validated, or avoid rewriting in those cases.

## Progress log

- 2026-02-18: Create chunking doc; start with chunk 1 enabled.
- 2026-02-18: Fixed chunk guard placement, rebuilt `test_bitcoin`, and confirmed `ctest -R '^descriptor_tests$' --output-on-failure` passes with only Chunk 1 enabled.
- 2026-02-18: Enabled the next single-key blocks (key-origin parse errors, uncompressed key coverage, hybrid-key rejection, and unconventional single-key constructions). `descriptor_tests` still passes.
- 2026-02-18: Enabled BIP32 derivation descriptors; updated expected `DescriptorID()` values for BNG re-encoded vprv/vpub extended keys.
- 2026-02-18: Enabled multipath BIP32 derivation descriptors (Chunk 4). `descriptor_tests` still passes.
- 2026-02-18: Refactored `descriptor_test` into `descriptor_test_1..4`; fixed a preprocessor/brace mismatch; rebuilt and confirmed `descriptor_tests` passes with only `descriptor_test_1` enabled.
