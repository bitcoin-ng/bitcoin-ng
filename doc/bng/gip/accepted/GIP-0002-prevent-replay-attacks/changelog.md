# Changelog — BNG-0002 (Replay Attack Protection)

This file tracks intended edits/decisions without modifying the accepted GIP text or its implementation plan.

## 2026-03-05

### ForkID constant: lock the decision and semantics

Proposed doc-only clarifications (kept here instead of editing `README.md` / `plan.md`):

- **Constant (consensus-critical):** `BNG_REPLAY_PROTECTION_FORKID = 0x00474E42`
- **Serialization:** 32-bit little-endian, bytes `42 4e 47 00` (ASCII `"BNG\0"`)

**How the forkid is committed (domain separation):**

- **ECDSA legacy + segwit v0**
  - Commit a 32-bit hashtype value derived from the script-level sighash byte:
    - `ForkedSighashType(sighash_byte, forkid) = uint32_t(sighash_byte) | (forkid << 8)`
  - With `0x00474E42`, the committed (little-endian) hashtype bytes are:
    - `[sighash_byte, 0x42, 0x4e, 0x47]`
- **Schnorr taproot/tapscript**
  - Include the 4-byte forkid in the `TapSighash` preimage as little-endian bytes:
    - `42 4e 47 00`

### Naming consistency

- Standardize the taproot commitment name to `BNG_REPLAY_PROTECTION_FORKID` (avoid also using `BNG_SIGHASH_FORKID`).

### Minor doc cleanup

- Remove duplicated bullet under “Effect” in the accepted GIP README.

## 2026-03-14

### Repo audit: implementation status vs accepted GIP text

Quick check of current code indicates the GIP README’s “What the current implementation achieves” section is ahead of the codebase.

**Observed in `src/` (consensus + signing):**

- **ECDSA legacy + segwit v0**: `SignatureHash(...)` appends/commits the plain `nHashType` (no forkid / no 32-bit forked mapping).
- **Taproot/tapscript**: `SignatureHashSchnorr(...)` commits the plain 1-byte `hash_type` (no 4-byte forkid field in the `TapSighash` preimage).
- **Wallet signing**: signing paths call the same hashing functions, so they currently produce Bitcoin-style signatures (not forkid-separated).

**Observed in `src/test/`:**

- `sighash_tests` asserts `SignatureHash(...) == SignatureHashOld(...)` for BASE spends.
- `sighash_from_data` uses upstream-style sighash vectors (`src/test/data/sighash.json`).

**Non-cryptographic separation that *is* present (defense-in-depth):**

- Network identity differences exist (message magic bytes, ports, Base58 prefixes, bech32 HRPs). These reduce accidental cross-network broadcast/relay but do not prevent raw transaction replay when UTXOs overlap.

### Doc follow-ups implied by the audit (doc-only)

- In the accepted GIP README, reword “What the current implementation achieves” to either:
  - explicitly describe *target behavior* (“intended implementation”), or
  - describe the *current state* (replay protection not yet active at consensus level).
- The duplicated “Effect” bullet in the accepted GIP README still exists as of this audit.

### Next implementation step (tracking only; no code change in this file)

- Implement forkid domain separation in both ECDSA (`SignatureHash`) and taproot (`SignatureHashSchnorr`) as described in `plan.md`, then update/replace sighash vectors and the `SignatureHashOld` equivalence assertions so tests reflect BNG consensus.

## 2026-03-15

### Consensus/signing implementation landed

The core replay-protection implementation is now present in code.

**Implemented in `src/script/`:**

- **ECDSA legacy + segwit v0** now commit the forked 32-bit hashtype:
  - `BNG_REPLAY_PROTECTION_FORKID = 0x00474E42`
  - `ForkedSighashType(sighash_byte) = uint32_t(sighash_byte) | (BNG_REPLAY_PROTECTION_FORKID << 8)`
- `SignatureHash(...)` appends the forked 32-bit value in both cache-hit and cache-miss paths.
- **Taproot/tapscript** now commit the 4-byte forkid in `SignatureHashSchnorr(...)` immediately after the epoch byte.
- Wallet signing paths inherit the new behavior automatically because they already call the consensus hashing functions.

### Test/vectors updated to BNG behavior

**Updated in `src/test/`:**

- `src/test/sighash_tests.cpp`
  - removed the blanket assumption that BASE `SignatureHash(...) == SignatureHashOld(...)`
  - updated the cache reconstruction logic to append the forked 32-bit hashtype
  - added explicit replay-protection proofs for:
    - legacy ECDSA
    - segwit v0 ECDSA
    - taproot key-path Schnorr
- `src/test/data/sighash.json`
  - regenerated to BNG digest outputs
- `src/test/data/bip341_wallet_vectors.json`
  - updated key-path `sigMsg`, `sigHash`, and expected witness signatures to match BNG taproot sighashes

### Functional test framework follow-up

`test/functional/test_framework/script.py` was updated so the Python sighash helpers follow BNG consensus rules for:

- legacy ECDSA
- segwit v0 ECDSA
- taproot/tapscript

Old Bitcoin-style helper variants were also added there for future negative testing.

### Verification completed

Confirmed locally:

- `cmake --build build --target test_bitcoin -j4`
- `build/bin/test_bitcoin --run_test=sighash_tests,script_tests/bip341_keypath_test_vectors --catch_system_error=no`
- `python3 -m py_compile test/functional/test_framework/script.py`

### Still open

The remaining open item from the implementation plan is the **end-to-end functional replay test**:

- a draft `feature_replay_protection.py` was attempted but not kept
- the "old Bitcoin-style segwit spend is rejected" path behaved inconsistently in the Python functional harness and needs a separate debugging pass before adding a default functional test

So as of 2026-03-15:

- **Consensus-level replay protection is implemented**
- **unit/vector coverage is updated and passing**
- **the dedicated functional rejection/acceptance test remains outstanding**
