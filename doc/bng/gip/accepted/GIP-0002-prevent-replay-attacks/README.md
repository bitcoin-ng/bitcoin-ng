# BNG-0002: Replay Attack Protection

**Status**: Accepted
**Created**: 2026-01-17

## Abstract
This document proposes consensus-level mechanisms to prevent transaction replay attacks between Bitcoin Core and Bitcoin-NG (BNG), as changing address prefixes is insufficient for replay protection.

## Motivation
Since Bitcoin-NG forks the UTXO set (sharing the same outpoints as Bitcoin), a transaction valid on one chain could be valid on the other. Without replay protection, users could accidentally spend funds on both chains, or attackers could grief the network.

## Specification

### Recommended Approach: Fork-Specific Signature Hash
Implement a **ForkID-style** signature hash modification (Option 1).

- **Mechanism**: Introduce a new SIGHASH algorithm or modify the ECDSA/Schnorr digest to include a chain-specific identifier (e.g., specific ForkID or Genesis Hash).
- **Activation**: Height 1 (or Snapshot Activation Height).
- **Effect**: Signatures generated for Bitcoin will be invalid on BNG, and vice versa.
 - **Effect**: Signatures generated for Bitcoin will be invalid on BNG, and vice versa.

### What the current implementation achieves

This repo implements replay protection by domain-separating signature digests with a chain-specific fork identifier.

1) **Legacy + Segwit v0 ECDSA (SIGHASH)**

- **Legacy / pre-segwit**: the signature hash commits to `ForkedSighashType(nHashType)` (a 32-bit value).
- **Segwit v0 (BIP143-style)**: the signature hash also commits to `ForkedSighashType(nHashType)`.

Because `ForkedSighashType()` expands the commitment beyond the 1-byte hashtype (while preserving the script-level hashtype byte encoding), ECDSA signatures from Bitcoin are not valid on BNG and vice versa.

2) **Taproot / Tapscript Schnorr (BIP341/BIP342)**

The taproot `TapSighash` preimage includes `BNG_SIGHASH_FORKID`, domain-separating Schnorr signatures for both key-path and script-path spends.

Result: for standard signature-validated spends (P2PKH/P2WPKH/P2WSH/P2TR), classic cross-chain replay is prevented.

### Consensus-critical completeness checks

Even if the core idea is correct, replay protection can still be incomplete or cause a consensus split if any of the following are missed.

#### A) Ensure *every* consensus sighash path commits to the forkid

Bitcoin-derived codebases historically had multiple implementations and call sites for signature hashing.

- Confirm there is exactly one consensus implementation for each sigversion:
    - ECDSA: `SignatureHash(...)` in `src/script/interpreter.cpp`
    - Schnorr: `SignatureHashSchnorr(...)` in `src/script/interpreter.cpp`
- Audit all references with:
    - `git grep "SignatureHash("`
    - `git grep "SignatureHashSchnorr"`
    - `git grep -i "sighash|SigHash|GetSignatureHash"`

#### B) Ensure signing logic matches validation logic

Validation and signing must compute identical digests on BNG, otherwise wallets will produce signatures the chain rejects.

- Confirm signing uses the same functions as validation:
    - ECDSA signing uses `SignatureHash(...)`.
    - Schnorr signing uses `SignatureHashSchnorr(...)`.

#### C) Forkid constant clarity (endianness / naming)

`BNG_REPLAY_PROTECTION_FORKID` is serialized as a 32-bit integer (little-endian in Bitcoin serialization). The chosen value `0x00474E42` serializes to the byte sequence `42 4e 47 00` (ASCII `"BNG\0"`).

This is fine; just keep the semantics consistent and document it as a 4-byte constant.

#### D) Replay remains possible for **non-signature spends**

Replay protection via signature hashing only applies where a signature is required.

- Anyone-can-spend outputs (e.g., `OP_TRUE`) are replayable by design.
- This is usually acceptable in practice, but should be documented as a limitation.
- Mitigations (defense-in-depth): distinct address encoding/HRP, distinct network magic/ports, and avoiding creation of weird/anyone-can-spend outputs.

#### E) Activation: genesis vs live fork

- If BNG is a **new chain from genesis** (or a snapshot chain where you define new consensus from height 0/1), always-on replay protection is appropriate.
- If BNG is a **live consensus fork** at height $H$, this must be gated behind an activation mechanism (height or versionbits), otherwise old nodes will disagree on validity (consensus split).

### Practical verification checklist

The following checks are intended to conclusively validate replay protection and detect signing/validation mismatches.

1) **Static audit**
- Run the `git grep` commands in (A), and confirm all consensus sighash paths commit to the forkid.

2) **Unit tests (already present)**
- `replay_protection_rejects_bitcoin_style_signatures` verifies that a legacy Bitcoin-style ECDSA signature (no forkid in digest) fails on BNG, and a BNG-style signature succeeds.
- `sighash_tests` validates the updated sighash algorithm against test vectors.

3) **Regtest integration tests (manual)**
- Create and spend:
    - P2PKH (legacy)
    - P2WPKH (segwit v0)
    - P2WSH (segwit v0 multisig)
    - P2TR key-path
    - P2TR script-path (tapscript)

4) **Cross-chain replay attempt (conceptual)**
- Create a transaction on Bitcoin using Bitcoin’s sighash rules.
- Attempt to broadcast the identical raw transaction on BNG.
- Expected outcome: signature validation fails because BNG computes a different digest.

### Risk Analysis (Checklist)

1.  **Transaction Replay**:
    - *Mitigation*: Fork-specific sighash.
2.  **Cross-chain Fee Griefing**:
    - *Mitigation*: Replay protection + distinct fee/dust policy.
3.  **Malleability Divergence**:
    - *Mitigation*: Activate new rules cleanly at height 0/1.

### Alternative Options Considered

- **Mandatory "Chain Tag"**: Requiring a specific OP_RETURN or opcode. (Rejected: UX friction).
- **Script Divergence**: Changing script validation rules. (Rejected: Fragile).

---
*Original discussion content from `doc/bng/archive/replay_attacks.md` follows:*

I just want to avoid the replay attack

Changing the address prefixes / HRP is not replay protection. It only changes how humans encode the same underlying scriptPubKey. A P2PKH/P2WPKH output on your fork is still a perfectly valid Bitcoin output if the hash is the same — so the same raw transaction can be valid on both chains.

With a UTXO snapshot (same outpoints exist on both chains), you must add consensus-level replay protection.

(Rest of the original content is preserved in the archive or can be viewed in the history)
