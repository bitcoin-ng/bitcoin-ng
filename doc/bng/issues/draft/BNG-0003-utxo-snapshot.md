# BNG-0002: UTXO Snapshot and Initial Distribution

**Status**: Draft
**Created**: 2026-01-13

## Abstract
This document proposes a method for initializing the Bitcoin-NG blockchain state using a snapshot of the Bitcoin UTXO set.

## Motivation
To distribute BNG tokens to existing Bitcoin holders, ensuring a fair initial distribution based on a specific block height.

## Specification (Proposal)
The recommended approach is **AssumeUTXO-style Chainstate Injection**.

1.  **Snapshot Creation**:
    - Use `bitcoind dumptxoutset` on a synced Bitcoin Core node.
    - Target a specific block height (to be decided).
2.  **Implementation**:
    - Load the snapshot as the initial chainstate at genesis (or height 0).
    - This avoids the need for creating thousands of "distribution blocks" which would bloat the chain.
3.  **Validation**:
    - Verify snapshot hash matches the agreed-upon state.
    - Ensure consensus parameters accept this initial state.

---
*Original discussion content from `doc/bng/discussion/uxto_snapshot.md` follows:*

I want to make a bitcoin core ledger snapshot . I.e. dump all of the balances for all of the addresses as a single pre-genesis block and start off the new Blockchain but accept the old "phone book". What are my options? Are there any oss projects that help me do that?

What you’re describing is essentially a snapshot-based premine / initial state for a Bitcoin-derived chain. The key design choice is what you snapshot:
...
(Content from original discussion)
...
