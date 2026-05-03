# BNG-0006: Pre-Genesis Checks

**Status**: drafted
**Created**: 2026-03-23

## Abstract
This GIP defines the minimum pre-genesis parameter checks that MUST be completed before BNG mainnet launch. Its current scope is intentionally narrow: freeze the final genesis block timestamp, replace placeholder DNS seeds with production-operated seeds, and document which launch-time items are explicitly out of scope for this proposal.

## Motivation
BNG already defines its network identity and replay-protection direction, but mainnet launch still depends on a small set of operational constants being finalized at the correct time.

Those constants should not be left as informal TODOs because they directly affect node bootstrap and chain identity:

- the genesis timestamp becomes part of the permanent chain definition
- DNS seeds influence first-boot peer discovery and operator experience
- unclear launch-time responsibilities create avoidable confusion around what this repo is and is not standardizing before genesis

This proposal exists to make the pre-genesis checklist explicit, reviewable, and easy to close before launch.

## Specification

### 1. Final genesis timestamp
Before BNG mainnet launch, the genesis block timestamp MUST be updated from any placeholder value to the actual intended launch timestamp.

Requirements:

- the chosen timestamp MUST be fixed before release artifacts are finalized
- the final `genesis.nTime` value MUST match the launch decision recorded by the project
- any asserted genesis hash or derived constants affected by the timestamp MUST be regenerated and rechecked

This GIP does not itself choose the timestamp. It requires that the placeholder be replaced with the final launch value before genesis is considered frozen.

### 2. Production DNS seeds
Before BNG mainnet launch, placeholder seed entries MUST be replaced or removed.

Requirements:

- any shipped DNS seed hostname MUST be intentionally operated for BNG mainnet
- seed names MUST resolve only to BNG mainnet peers, not upstream Bitcoin infrastructure
- if production seeds are not ready, the release decision MUST explicitly choose between delaying launch or shipping with no DNS seeds

At minimum, the final seed list must not contain illustrative placeholders such as `seed1.bng.org` or `seed2.bng.org`.

### 3. Scope boundary
This GIP is limited to launch-blocking pre-genesis checks that can be decided before the chain starts.

Included in scope:

- final genesis timestamp selection and verification
- final DNS seed readiness review
- increase the total mining limit to 42 million coins and reset the block-reward schedule to 100 coins per block

Explicitly out of scope for this draft:

- post-genesis checkpointing such as `nMinimumChainWork` and `defaultAssumeValid`
- consensus changes unrelated to launch readiness
- any undefined or incomplete launch ideas that have not yet been specified well enough for normative inclusion
- see `pre-mining.md` for points related to preallocating UTXO state or pre-mining coins, which are currently out of scope but may be revisited in the future if they become more concrete

## Backwards Compatibility
- There is no backwards-compatibility impact on an already-launched network because this GIP is about values that must be finalized before genesis.
- Failing to complete these checks before launch risks avoidable incompatibility between released binaries, documentation, and operator expectations.

## Security Considerations
- Freezing the wrong genesis timestamp can create permanent chain-identity divergence.
- Shipping placeholder or misconfigured DNS seeds can misdirect initial peer discovery or create the appearance of a functioning bootstrap path when none exists.
- Keeping unspecified launch-time ideas out of scope reduces the risk of accidental consensus commitments hidden inside operational checklists.

## Deployment / Activation
This GIP has no post-launch activation logic.

It is satisfied when all of the following are true before mainnet release:

1. the final genesis timestamp has been chosen and committed
2. affected genesis assertions have been regenerated and verified
3. placeholder DNS seeds have been replaced with real operators or intentionally removed
4. release documentation reflects the final launch-state decision

## Reference Implementation
- `src/kernel/chainparams.cpp` for final genesis timestamp and DNS seed definitions
- any genesis-generation helper or assertion code affected by the final timestamp
- documentation under `doc/bng/` that refers to launch readiness or seed availability

Use the shared build/test workflow in `doc/bng/development/README.md` and the fork-specific guidance in `doc/bng/development/hard-fork-notes.md` when verifying network identity changes.

## Test Plan
Use the shared build and test commands in `doc/bng/development/README.md`.

Minimum verification for this GIP should include:

- confirm the finalized genesis timestamp is reflected in chain parameters
- verify any genesis hash assertions still pass after the timestamp is frozen
- confirm placeholder DNS seeds are absent from the final configuration
- start a node with the finalized configuration and verify peer discovery behavior matches the intended launch setup

## Open Questions
- What exact UTC timestamp should be recorded as the mainnet launch time?
- Which operators will provide the initial production DNS seeds?
- Should mainnet launch be blocked if production DNS seeds are not ready, or is a seedless launch acceptable?

## Changelog
- 2026-03-23: drafted