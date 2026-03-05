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
