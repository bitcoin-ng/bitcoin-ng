# BNG-0003: BNG-Owned Public Network Split ("BNG Signet")

**Status**: Draft
**Created**: 2026-01-24

## Abstract
This GIP proposes introducing a BNG-owned, public, coordinated test network (informally “BNG signet”) that is fully under BNG’s control and identity, and gradually moving away from relying on upstream Bitcoin public networks (notably Bitcoin signet and testnet4) for BNG development, CI, and interoperability testing.

The proposal also documents a path to reduce ongoing upstream-network-specific maintenance inside BNG by isolating upstream network definitions behind explicit build/runtime options.

## Motivation
BNG currently carries upstream public-network definitions (e.g., Bitcoin signet/testnet4) because:

- They are useful for developer workflows and interoperability testing.
- Upstream tooling and assumptions expect known genesis blocks and constants.

However, long-term BNG goals include:

- A fully BNG-owned “public test” environment with predictable governance of block production.
- Reducing accidental coupling to upstream constants and assumptions.
- Minimizing ongoing code churn caused by upstream network changes that are irrelevant to BNG.

A BNG-owned public network provides:

- **Stability** for integration testing (fewer random reorgs vs PoW testnets).
- **Clear branding/identity** (magic bytes, ports, HRPs, seeds) to avoid operator confusion.
- **Operational control** over mining/validation rules in a public environment.

## Background
### What “signet” means in this repo
“Signet” refers to BIP325: a public test network with an additional consensus parameter (`consensus.signet_challenge`). In practice this enables coordinated block production for stability.

BNG currently keeps upstream `SigNetParams` intact for Bitcoin signet compatibility.

### Why the genesis helpers are split
BNG keeps an upstream-compatible genesis builder alongside a BNG-specific builder so upstream networks (signet/testnet4) can remain compatible while BNG networks can brand their genesis.

This GIP does not change that policy; it proposes adding a **BNG-owned public network** rather than repurposing Bitcoin signet.

## Goals
- Create a **BNG-owned public test network** suitable for long-lived integration testing.
- Avoid any ambiguity between “Bitcoin signet” and “BNG signet”.
- Provide a clean migration path for:
  - documentation
  - CI
  - seed infrastructure
  - developer instructions
- Reduce upstream-network maintenance burden by isolating upstream definitions.

## Non-goals
- Changing BNG MAIN/TESTNET/REGTEST identity constants already defined in GIP-0001.
- Changing replay protection mechanisms (see BNG-0002).
- Deciding final governance/participants for block signing/mining (that is an operational policy outside this repo).

## Specification

### 1) Introduce a new chain type: `bngsignet` (name TBD)
Add a distinct chain selection separate from upstream `signet`.

Requirements:
- Must have its own `ChainType` value (or equivalent mechanism) so it can coexist with upstream Bitcoin signet in the codebase.
- Must have a distinct datadir subdirectory (e.g. `bngsignet/`) to avoid collisions.

Rationale:
- Avoid reusing `SIGNET` to prevent confusion and preserve compatibility with upstream tooling when explicitly desired.

### 2) Network identity constants
Define a complete identity for the new network:

- **P2P message start**: unique 4 bytes (must not collide with other networks in-tree).
- **Default P2P port**: unique, non-Bitcoin.
- **Address encodings**:
  - Base58 version bytes (P2PKH, P2SH, WIF)
  - BIP32 extkey versions
  - Bech32 HRP (e.g. `sbng` or `bngs`, TBD)
- **DNS seeds / fixed seeds**: initially empty or placeholder until infrastructure exists.

Notes:
- Values must be documented and checked for collisions with other networks.
- The HRP must be different from `bng`, `tbng`, and `bngr`.

### 3) Consensus behavior
Two acceptable options are defined; choose one for the first implementation.

**Option A (recommended): BIP325 signet-like coordination**
- Enable `consensus.signet_blocks = true`.
- Define a BNG-owned `consensus.signet_challenge`.
- Document the process for rotating/operating the challenge keys (operational policy).

Pros:
- Stable, predictable public test network.
- Reduced reorg chaos compared to PoW testnets.

Cons:
- Requires operational coordination and key management.

**Option B: Conventional PoW testnet-style**
- Disable signet blocks.
- Use PoW testnet parameters with appropriate difficulty rules.

Pros:
- No signing infrastructure required.

Cons:
- Less stable for integration testing; more susceptible to reorgs and griefing.

### 4) Genesis block
The new network MUST have its own genesis block.

Requirements:
- Unique genesis timestamp message.
- Deterministic genesis construction.
- Asserted genesis hash + merkle root in chain params.

### 5) Relationship to upstream networks (Bitcoin signet/testnet4)
This GIP proposes an explicit stance:

- Upstream networks remain supported only as **explicit compatibility targets**, not as the default BNG public test environment.

Concretely:
- Documentation, CI, and developer guides should prefer `bngsignet` for public integration testing.
- Upstream `signet`/`testnet4` can be retained behind an explicit toggle (see Implementation).

## Backward compatibility
- No change to existing BNG MAIN/TESTNET/REGTEST identity or genesis.
- Adds a new selectable network.
- If upstream networks are later gated/disabled by default, that change must be documented and released as a compatibility note.

## Security considerations
- A public BNG test network must still maintain isolation:
  - unique magic bytes/ports
  - unique address prefixes/HRPs
  - replay protection is already handled per BNG-0002
- Seed infrastructure must be treated as security-sensitive; do not ship “real” seeds until they are operated intentionally.

## Implementation plan (proposed)

### Phase 1: Define the network
- Add the new chain type selection.
- Add params in `src/kernel/chainparams.cpp` (or split file/module if that refactor lands first).
- Add a new genesis and asserts.
- Add placeholders for seeds.

### Phase 2: Tooling + tests
- Update Python functional test framework constants if required (magic bytes, HRP).
- Add at least one smoke test to validate the new network identity is selectable and internally consistent.

### Phase 3: Docs + CI migration
- Add docs explaining when to use:
  - `regtest` (local dev)
  - `bngsignet` (public coordinated test)
  - `testnet` (public PoW test)
- Update CI to prefer `bngsignet` for public-network scenarios.

### Phase 4: Reduce upstream maintenance burden
One of:
- Build option (e.g. `-DENABLE_UPSTREAM_NETWORKS=ON/OFF`) to include/exclude upstream signet/testnet4.
- Runtime gating that hides upstream networks unless an explicit `-allow-upstream-networks` flag is provided.
- Longer term: move upstream networks into a separate module maintained as a compatibility layer.

## Open questions
- Final chain name: `bngsignet`, `signetbng`, or `publictest`?
- HRP selection and Base58/extkey versions.
- Whether to ship with Option A (BIP325 signet-like) by default.
- Whether upstream networks remain in default builds or become opt-in.

## References
- BNG-0001: Network Identity and Rebranding
- BNG-0002: Replay Attack Protection
- BIP325 (signet)
