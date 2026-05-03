# BNG-0003: Dual Mining Difficulty

**Status**: accepted
**Created**: 2026-03-23

## Abstract
This GIP proposes an alternating mining schedule with two independently tracked difficulty lanes: an "energy" lane that preserves the inherited Bitcoin Core SHA256d proof-of-work rules, and a new memory-hard lane that uses CTAM-style context retention as the scarce resource. Odd-numbered weeks use the energy lane, even-numbered weeks use the memory lane, and the block header is extended to carry both compact targets so every block commits to both difficulty states.

## Motivation
BNG wants to explore a mining model that does not rely on a single hardware optimization path forever.

The inherited Bitcoin Core difficulty logic remains useful because it is well understood, already implemented, and provides a baseline energy-priced security model. The proposed CTAM lane adds a second scarcity axis by requiring miners to maintain a bounded in-memory context while producing and validating blocks.

Keeping both lanes live serves three purposes:

- preserve continuity with the existing SHA256d chainwork model
- introduce a memory-hard phase without forcing a one-time permanent cutover
- let the chain adjust each lane independently as miner participation moves between energy-heavy and memory-heavy hardware

Shared build and test workflow guidance lives in `doc/bng/development/README.md`. Hard-fork vector and serialization guidance lives in `doc/bng/development/hard-fork-notes.md`.

## Specification

### 1. Week-parity lane selection
Define the active mining lane from the parent block median time past:

```text
week_number = 1 + floor((parent_mtp - dual_pow_epoch) / 604800)
```

- If `week_number` is odd, the next block is an **energy block**.
- If `week_number` is even, the next block is a **memory block**.

`dual_pow_epoch` is a consensus constant set by the activation mechanism.

Using parent median time past instead of the candidate header timestamp avoids making lane selection depend on a miner-controlled field.

### 2. Two compact difficulty fields in the block header
The serialized block header is extended with a second compact target field:

```text
version | prev_block | merkle_root | time | nBits | nMemBits | nonce
```

- `nBits` continues to encode the compact target for the energy lane.
- `nMemBits` encodes the compact target for the memory lane.

Rules:

- All post-activation blocks MUST serialize both fields.
- Pre-activation headers remain unchanged.
- Header hashing, block relay, disk serialization, and RPC hex encoding MUST use the post-activation header format for activated blocks.

### 3. Energy lane
On odd-numbered weeks:

- Block validity uses the inherited Bitcoin Core SHA256d proof-of-work check against `nBits`.
- `nMemBits` MUST still be present in the header and MUST equal the currently expected memory-lane target, but it is not the active acceptance rule for that block.
- Energy-lane cumulative work is used when comparing odd-week blocks within the same chain history.

### 4. Memory lane
On even-numbered weeks:

- Block validity uses CTAM memory-hard verification against `nMemBits`.
- The CTAM proof MUST demonstrate retention of a context state whose minimum size is derived from `nMemBits`.
- `nBits` MUST still be present in the header and MUST equal the currently expected energy-lane target, but it is not the active acceptance rule for that block.

The exact CTAM proof encoding, witness placement, and verifier algorithm are part of the same consensus change and must be specified before this GIP moves from drafted to accepted.

### 5. Difficulty adjustment cadence
The two lanes adjust independently.

- At the first odd-week block after an even week completes, recompute the next expected `nBits` using the energy-lane retarget algorithm.
- At the first even-week block after an odd week completes, recompute the next expected `nMemBits` using the CTAM retarget algorithm.
- The inactive lane carries forward its prior compact target unchanged until its next scheduled adjustment.

The design goal is to avoid cross-coupling where a sudden hashrate shift in one lane immediately destabilizes the other.

### 6. Chain selection and cumulative work
Consensus must define a total ordering across mixed energy and memory weeks.

This draft proposes maintaining two per-block contributions:

- `energy_work(block)` for odd-week blocks
- `memory_work(block)` for even-week blocks

Chain selection then compares a combined score:

```text
dual_work = energy_work_sum + memory_work_sum
```

The exact normalization between the two work units remains an open question and must be fixed before implementation. A simple byte-for-byte reuse of Bitcoin's chainwork accumulator is not sufficient unless the memory-lane verifier also exposes a monotonic work metric.

### 7. RPC and diagnostics
Block and mining RPCs MUST surface both fields after activation.

Minimum changes:

- block header JSON exposes `bits` and `membits`
- mining template data identifies the current lane as `energy` or `memory`
- validation logs report which lane rejected a block

## Backwards Compatibility
This is not backwards compatible with pre-activation consensus.

- Legacy nodes that only understand the 80-byte Bitcoin header will reject activated blocks.
- Storage, P2P relay, and RPC tooling that assumes a single difficulty field will need updating.
- Wallet behavior is largely unaffected, but block parsers and explorers must understand the new header format.

## Security Considerations
- Lane selection must not be timestamp-gameable; parent median time past is specified for that reason.
- Chain selection must not let one lane dominate by exploiting a weaker work normalization rule.
- The memory lane adds a new verifier surface and must be bounded so malicious blocks cannot force unreasonably large allocations during validation.
- Nodes should validate the claimed context-size requirement from `nMemBits` before allocating or streaming auxiliary proof material.

## Deployment / Activation
This change requires a hard-fork style activation because it modifies block header serialization and consensus validation.

Recommended rollout:

1. implement dual-field header parsing and RPC exposure behind a feature flag on regtest
2. deploy CTAM proof verification and deterministic test fixtures
3. activate on a dedicated BNG network at a fixed height/time
4. only then consider mainnet activation

## Reference Implementation
- `src/primitives/block.*` for the extended header structure
- `src/pow.*` and `src/validation.*` for lane selection and target checks
- `src/node/miner.*` for current-lane template creation
- RPC surfaces that serialize header fields and mining state
- functional and unit tests covering header round-trips, lane parity, retargeting, and reorgs across week boundaries

## Test Plan
Use the shared build and test instructions in `doc/bng/development/README.md`.

Targeted validation should include:

- unit tests for header serialization before and after activation
- unit tests for odd/even week lane selection from parent median time past
- deterministic retarget fixtures for `nBits` and `nMemBits`
- functional tests that mine across an odd/even boundary and verify rejection of the wrong proof type
- reorg tests where competing branches cross a week boundary differently

## Open Questions
- How is CTAM proof data serialized: header-adjacent, coinbase-committed, or witness-committed?
- What normalization formula converts memory-lane proof quality into cumulative work?
- Should the first activated week be forced to an odd week so the chain begins with the legacy energy lane?
- Should `nMemBits` encode only difficulty, or both difficulty and required context size?

## Changelog
- 2026-03-23: drafted
- 2026-03-23: accepted