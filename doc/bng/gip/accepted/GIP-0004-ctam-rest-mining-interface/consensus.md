# Consensus Integration Plan

This note extends BNG-0004 with the consensus-side implementation plan for the first CTAM-backed memory-lane prototype.
[protocol-extension.md](protocol-extension.md) covers REST lifecycle and resize coordination.
[plan.md](plan.md) covers the general implementation direction.
This document focuses on the consensus-owned changes needed to make memory-lane mining and validation real:

- header fields and serialization
- lane selection
- mining from node-owned block templates
- CTAM-backed validation for memory-lane blocks

## Scope

This plan assumes the following boundaries:

- `interfaces::Mining::createNewBlock()` remains the source of truth for candidate work.
- Memory-lane miners hash the header bytes derived from those templates using `csha256`.
- The node remains the final arbiter of block validity.
- CTAM responses remain untrusted input even when CTAM is used as the execution engine.
- Minor CTAM difficulty remains fixed to 16 rounds for now, but it is still serialized and consensus-checked.

This note intentionally does not redefine CTAM resize timing or the operational REST flow. Those remain in [protocol-extension.md](protocol-extension.md) and GIP-0003.

## Core decision: commit the full difficulty tuple in the header

Earlier discussion used `nMemBits` as shorthand for the memory-lane difficulty.
That is not sufficient for the current CTAM model because the node already treats memory-lane difficulty as a three-part tuple:

- energy difficulty: the inherited Bitcoin compact target bits
- major CTAM difficulty: the required `context_size` exponent
- minor CTAM difficulty: CTA-SHA256 round count

For implementation, the activated header should commit the full tuple explicitly.

Proposed post-activation header layout:

```text
version | prev_block | merkle_root | time | nBits | nCtamMajor | nCtamMinor | nonce
```

Field meanings:

- `nBits`
  - The compact energy target.
  - This remains the numeric inequality target checked against the resulting hash value.
  - It continues to carry the chainwork-facing target representation.

- `nCtamMajor`
  - The CTAM major difficulty.
  - Serialized as the expected CTAM `context_size` exponent.
  - Initial floor: `27`.

- `nCtamMinor`
  - The CTAM minor difficulty.
  - Serialized as CTA-SHA256 rounds.
  - Consensus-fixed to `16` for the first implementation, but still committed in the header so future upgrades do not need hidden defaults.

Implementation note:

- use fixed-width serialized integers for all three fields
- keep `nCtamMinor` as a 32-bit serialized field instead of a packed byte to avoid special-case header encoding rules
- if older notes refer to `nMemBits`, treat that as a historical placeholder rather than the final field design

This yields an activated header that is larger than the inherited 80-byte Bitcoin header, which is already consistent with the accepted hard-fork direction.

## Header ownership and template flow

Block templates generated through [src/interfaces/mining.h](../../../../../src/interfaces/mining.h) should remain the only supported source of mining work.

The plan is:

1. `interfaces::Mining::createNewBlock()` constructs a template that already contains the consensus-selected `nBits`, `nCtamMajor`, and `nCtamMinor`.
2. `interfaces::BlockTemplate::getBlockHeader()` returns the full activated header.
3. `interfaces::BlockTemplate::getBlock()` returns a block whose header matches the same tuple.
4. External miners and the internal node mining loop hash only these template-derived bytes.
5. `submitSolution()` preserves the template-supplied difficulty tuple and only fills in miner-controlled values such as timestamp, nonce, and coinbase.

That means miners do not invent their own difficulty values.
They receive the header from the node, vary only the allowed fields, and return a candidate solution for a template the node already created.

## Consensus lane policy

For the BNG consensus path described in this note, memory-lane `csha256` is the only proof-of-work algorithm that matters.

The inherited SHA256d path remains in the codebase only as a temporary compatibility path for Bitcoin Core-style mining tests and related scaffolding.
It should not be treated as an enduring BNG consensus lane.

Consensus consequences:

- the activated header always carries the full CTAM difficulty tuple
- consensus validation hashes memory-lane blocks with `csha256` over the canonical activated header bytes
- the legacy SHA256d path is a test harness compatibility mode, not a competing BNG production rule

Validation rules for the consensus path:

- validate the header tuple against the expected consensus values for that height
- compute the memory-lane `csha256` hash over the full activated header bytes
- compare that hash against the compact target from `nBits`
- require the live CTAM state or proof witness to match `nCtamMajor`
- require the CTA-SHA256 round count to equal `nCtamMinor`

Implementation note:

- if a temporary SHA256d mining path remains wired in `src/rpc/mining.cpp`, document it and gate it as legacy test support only
- do not describe that path as a long-term consensus lane in BNG docs

## Memory-lane hash function

The memory-lane proof-of-work function should be defined as:

```text
memory_pow_hash = csha256(serialized_activated_header)
```

Important constraints:

- this is a mining-only hash path
- it must not replace `CSHA256` globally
- it must hash the exact serialized header bytes returned by the block template
- it must be deterministic for both miners and validators

`csha256` in this context means CTA-SHA256 parameterized by the header's consensus tuple:

- target from `nBits`
- major context size from `nCtamMajor`
- round count from `nCtamMinor`

## How the current CTAM mining API fits the first prototype

The first prototype should use the existing CTAM mining endpoint:

- `POST /crypto/mine_csha256`

That endpoint already performs the nonce-search mechanism on the CTAM server side, so the first consensus prototype does not require CTAM to grow a new dedicated header-mining API before work can begin.

The activated BNG header is larger than one 64-byte compression block, so the node still needs a canonical mapping from the full header to the CTAM mining request.
The intended mapping is:

1. Serialize the full activated header into canonical bytes.
2. Split those bytes into the SHA-256 compression chunks implied by CTA-SHA256.
3. Precompute the invariant chunk-1 state inside the node.
4. Call `POST /crypto/mine_csha256` for the final 64-byte mining chunk, passing:
  - the canonical final chunk as `block_hex`
  - the nonce byte offset inside that final chunk
  - the precomputed chunk-1 state as `initial_state_hex`
  - the full header difficulty tuple as `energy_difficulty_bits`, `major_ctam_difficulty_context_size`, and `minor_ctam_difficulty_rounds`

This keeps the header as the consensus object while still using CTAM's current mining-oriented REST call for nonce search.

The concrete behavior already exists in `ctam/src/server/rest/crypto.rs`:

- `post_mine_csha256(...)` iterates `nonce_start..=nonce_end`
- it writes each nonce into the caller-supplied 64-byte block at `nonce_offset`
- it evaluates CTA-SHA256 with the supplied `initial_state_hex`
- it compares the resulting digest against the target derived from `energy_difficulty_bits`
- it stops at the first winning nonce and returns `winning_nonce`, `digest_hex`, and `attempts`

So the missing implementation is not a server-side nonce loop.
The missing implementation is the BNG-side mapping from the canonical activated header to the request that `POST /crypto/mine_csha256` already understands.

The existing endpoint is therefore sufficient for the first prototype if and only if the node defines that chunk mapping deterministically and validators rebuild the same chunk-1 state from block data.

Lower-level APIs remain optional future optimizations, not blockers:

- a dedicated header-mining endpoint could reduce translation overhead later
- compression or midstate APIs could improve throughput later
- shared-memory transport could replace REST later without changing the consensus definition

Not acceptable for consensus:

- truncating the header to 64 bytes
- mining over an ad hoc transport buffer that is not deterministically derived from the serialized header
- relying on hidden defaults for major or minor difficulty

## Template-derived mining work

The mining work package should be derived from the template header, not rebuilt independently by RPC callers or external workers.

The node-side plan is:

1. Assemble the candidate block through `interfaces::Mining::createNewBlock()`.
2. Extract the full activated header.
3. Serialize the header into canonical bytes.
4. Identify the byte offset of the nonce inside those bytes.
5. Pass the canonical bytes plus the difficulty tuple to the `csha256` mining path.

That same canonical byte sequence should drive:

- internal node mining
- CTAM REST delegation
- `checkBlock()` validation
- any later Stratum or external template provider integration

This keeps the header as the single source of truth and avoids consensus drift between internal and external miners.

## Actual mining code patch plan

The missing implementation detail is the concrete call path where the node stops incrementing `nNonce` locally and starts calling `CtamRestClient` for memory-lane nonce search.

Today the built-in mining loop is still the local loop in `src/rpc/mining.cpp`:

- `GenerateBlock(...)` recomputes the merkle root
- it increments `block.nNonce` in a `while` loop
- it calls `CheckProofOfWork(block.GetHash(), block.nBits, ...)`
- it returns a solved block to `generateBlocks()` or `generateblock`

That is the first concrete call site to patch.

### Recommended patch shape

Add a mining helper in `src/rpc/mining.cpp` that makes the memory-lane CTAM path the normal BNG mining path:

```text
GenerateBlock(...)
  -> if legacy test mode: optionally keep the existing local SHA256d nonce loop
  -> otherwise: call a new CTAM-backed memory-lane helper
```

Suggested helper split:

- `GenerateMemoryBlockWithCTAM(...)`
  - derives the canonical activated header bytes from the template block
  - computes the invariant chunk-1 state
  - prepares the final 64-byte mining chunk and nonce offset
  - calls `CtamRestClient::MineCSHA256(...)`
  - writes the winning nonce back into `block.nNonce`
  - rebuilds the final canonical header bytes
  - hands the solved block to `ProcessNewBlock(...)`

- `GenerateLegacySha256dBlock(...)`
  - preserves today's `while (++nonce)` loop only where Bitcoin Core compatibility tests still need it
  - should be clearly gated so it is not mistaken for BNG consensus mining

### Where `ctam_client` should be instantiated

The node should not construct a fresh REST client for every nonce attempt.
The recommended ownership is one mining-scoped client per process or per mining interface instance.

Practical options:

- add an optional `std::unique_ptr<CtamRestClient>` to `node::NodeContext`
- or add an optional `CtamRestClient` member to `node::MinerImpl` in `src/node/interfaces.cpp`
- or add a small accessor that lazily constructs and caches the client from `ArgsManager`

For the first patch, the simplest workable plan is:

1. promote `-ctamresturl` and `-ctamtimeoutms` from test-only args to normal runtime args
2. build `CTAMClientOptions` from `ArgsManager`
3. cache the resulting `CtamRestClient` in a mining-owned object instead of recreating it inside the hot path

### How the patched memory-lane path should run

For a block template returned by `interfaces::Mining::createNewBlock()`:

1. Read the template block and determine that the lane is `memory`.
2. Serialize the full activated header including `nBits`, `nCtamMajor`, and `nCtamMinor`.
3. Split the header into the CTA-SHA256 chunk layout.
4. Compute the invariant chunk-1 state locally.
5. Call `m_ctam_client->MineCSHA256(...)` with:
   - the final 64-byte chunk as `block`
   - the nonce offset inside that chunk
   - the caller's nonce range derived from `max_tries`
   - the `CTAMDifficultyTriple` derived from the template header
   - `initial_state` set to the chunk-1 state
6. If CTAM reports `found = false`, return unsolved work just like the current loop does when `max_tries` is exhausted.
7. If CTAM reports `found = true`, copy `winning_nonce` back into `block.nNonce`.
8. Re-run the node's own proof check over the rebuilt canonical header bytes before block submission.
9. Submit the solved block through the existing `ProcessNewBlock(...)` path.

This keeps the node as the consensus owner while making CTAM the nonce-search worker for BNG consensus blocks.

### Files that need the concrete mining patch

- `src/rpc/mining.cpp`
  - replace the normal BNG nonce loop with a CTAM-backed helper
  - keep any SHA256d loop only behind an explicit legacy test-mode path

- `src/node/interfaces.cpp`
  - optionally cache the mining-scoped `CtamRestClient`
  - keep `createNewBlock()` as the template source of truth

- `src/node/context.h`
  - if `NodeContext` owns the cached client or CTAM mining configuration

- `src/common/args.cpp` or the relevant init argument registration surface
  - register production `-ctamresturl` and `-ctamtimeoutms` args for node mining, not just tests

### Relationship to `src/interfaces/mining.h`

`src/interfaces/mining.h` remains the right abstraction boundary for template creation.
The actual `ctam_client` call does not belong in `BlockTemplate` itself.
Instead:

- `interfaces::Mining::createNewBlock()` creates the canonical template
- the built-in miner path in `src/rpc/mining.cpp` consumes that template and calls `ctam_client`
- external miners may consume the same template bytes and call CTAM themselves
- `submitSolution()` remains a submission path, not a mining worker

That separation avoids burying network I/O inside the passive template object while still ensuring all miners work from the same template data.

## Validation plan

Consensus validation should stay node-owned even when the node uses CTAM as the execution engine.

Memory-lane validation steps:

1. Determine the active lane from the parent block median time past.
2. Derive the expected difficulty tuple for the candidate height:
   - expected `nBits`
   - expected `nCtamMajor`
   - expected `nCtamMinor`
3. Reject the block if any header difficulty field differs from the expected tuple.
4. Reconstruct the canonical activated header bytes.
5. Compute the memory-lane hash with the same `csha256` algorithm miners use.
6. Compare the resulting hash against the target from `nBits`.
7. Reject malformed CTAM proof data, malformed branch identifiers, or mismatched CTAM state transitions.

If a temporary SHA256d validation or mining path remains for legacy test coverage, keep it explicitly outside the BNG consensus description and test it as compatibility scaffolding only.

## Prototype verifier versus mining worker

The first consensus prototype should land directly in the node process.
That means:

- lane selection is implemented inside BNG
- header tuple validation is implemented inside BNG
- memory-lane proof checking is implemented inside BNG
- block acceptance never depends on an out-of-process yes or no verdict from CTAM

CTAM remains the execution engine for mining-side `csha256` work through the REST client.
In other words:

- miners may call CTAM through `CtamRestClient` for hashing and nonce search
- validators do not outsource consensus decisions to CTAM
- the node patches its own process to understand and verify memory-lane blocks

The node may still reuse the same request construction logic as the mining client when it needs parity checks or prototype instrumentation, but consensus acceptance must remain process-local.

That gives the prototype the desired behavior: mining blocks works with `csha256` through the CTAM REST client, while consensus logic itself lives in the BNG process.

## File-level implementation plan

The following code surfaces need to change together.

### Header and serialization

- `src/primitives/block.h`
- `src/primitives/block.cpp`
- any header JSON and hex serialization surfaces in RPC and REST

Changes:

- add `nCtamMajor` and `nCtamMinor` to `CBlockHeader`
- update `SERIALIZE_METHODS`
- update `SetNull()` and `GetBlockHeader()`
- update block and header stringification and JSON exposure

### Template generation and mining interfaces

- `src/interfaces/mining.h`
- `src/node/interfaces.cpp`
- `src/node/miner.h`
- `src/node/miner.cpp`

Changes:

- populate the full difficulty tuple in new templates
- expose the activated header bytes to the mining path
- keep `submitSolution()` anchored to template-owned difficulty fields
- ensure the internal mining loop switches algorithm by lane

### Consensus and proof checks

- `src/pow.h`
- `src/pow.cpp`
- `src/validation.h`
- `src/validation.cpp`

Changes:

- make proof-of-work checks lane-aware
- validate the full header tuple before hashing
- add memory-lane `csha256` validation path
- update `HasValidProofOfWork()` for header sync
- update rejection reasons so failures are explicit about lane or tuple mismatch

### Consensus parameters

- `src/consensus/params.h`
- `src/kernel/chainparams.cpp`

Changes:

- add activation constants for the memory lane
- add the initial CTAM major floor and fixed minor rounds
- add lane selection epoch parameters
- derive expected tuple values per height or parent MTP

### RPC, diagnostics, and tests

- `src/rpc/mining.cpp`
- `src/rpc/blockchain.cpp`
- relevant unit, functional, and regtest helpers

Changes:

- expose the full difficulty tuple in templates and header RPC output
- report the active lane in mining diagnostics
- update test helpers that currently assume an 80-byte header

## Recommended implementation sequence

### Phase 1. Activated header shape

- add `nCtamMajor` and `nCtamMinor` to the header behind a regtest or feature flag
- update serialization round-trip tests first
- ensure template and RPC output include the new fields

### Phase 2. Lane selection and expected tuple derivation

- implement parent-MTP lane selection
- teach chainparams or consensus helpers to derive the expected tuple for a candidate block
- reject tuple mismatches even before memory-lane hashing is enabled

### Phase 3. Template-owned memory-lane mining work

- derive canonical mining bytes from `interfaces::Mining` templates
- add a `csha256` mining helper that consumes those bytes and the header tuple
- keep any SHA256d path isolated as temporary Bitcoin Core test support only

### Phase 4. Memory-lane validation

- make proof-of-work checks lane-aware
- compute `csha256` on the validator side for memory-lane blocks
- reject blocks whose memory-lane hash fails `nBits`

### Phase 5. CTAM worker integration

- upgrade CTAM execution support so delegated mining operates on the canonical activated header bytes
- ensure the node can rebuild the exact same work locally for validation
- keep CTAM REST optional for mining, never optional for validation correctness

## Testing plan

Minimum coverage for this work should include:

- header serialization tests before and after activation
- tests that prove `nBits`, `nCtamMajor`, and `nCtamMinor` round-trip through block, header, RPC, and template code
- lane-selection tests using parent median time past across week boundaries
- legacy compatibility tests confirming any temporary SHA256d path still works where Bitcoin Core-derived test scaffolding requires it
- memory-lane tests confirming `csha256` is used only on memory-lane blocks
- rejection tests for wrong `nCtamMajor`, wrong `nCtamMinor`, wrong lane, and wrong hash
- template tests proving external miners get the same canonical bytes as the internal mining path
- regtest functional tests mining across an odd/even boundary and validating both acceptance and rejection behavior

## First-prototype decisions

- the first consensus prototype lands directly with an in-process verifier in BNG; CTAM REST is used for mining-side `csha256` execution, not as an external consensus oracle (think of it as a miner-side library call rather than a yes/no verdict from an out-of-process service)
- the first nonce-search implementation uses CTAM's existing `POST /crypto/mine_csha256`; that endpoint already does server-side nonce enumeration over a caller-supplied 64-byte block plus optional initial state, so the missing work is the BNG-side header-to-request mapping rather than a new CTAM search primitive
- the inherited SHA256d mining loop is retained only as temporary Bitcoin Core compatibility scaffolding and should not be documented as an enduring BNG consensus lane

## Summary

The consensus implementation should not treat memory-lane difficulty as a single opaque `nMemBits` value.
It should make the full tuple explicit in the header:

- `nBits` for the energy target
- `nCtamMajor` for the CTAM `context_size`
- `nCtamMinor` for CTA-SHA256 rounds

Mining should consume only block templates produced through `src/interfaces/mining.h`.
BNG consensus proof-of-work should be `csha256` over the canonical activated header bytes derived from those templates.
Validation should remain node-owned and deterministic, while any leftover SHA256d path is treated as temporary legacy test scaffolding rather than consensus.