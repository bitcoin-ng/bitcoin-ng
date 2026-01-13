# BNG-0002: Replay Attack Protection

**Status**: Draft
**Created**: 2026-01-13

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
