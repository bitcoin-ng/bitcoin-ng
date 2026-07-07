# Bitcoin Next Generation: A Dual-Scarcity, State-Retentive Proof-of-Work Chain

**Draft v0.1 — working paper**

*This is a first working draft assembled from the project's accepted Generation
Improvement Proposals (GIP-0001 through GIP-0004) and their implementation notes.
It describes the design as currently specified and prototyped. Sections marked
"open" are not yet frozen. Where a rule is not yet consensus-final, this document
says so rather than implying otherwise.*

## Abstract

Bitcoin secures its ledger with a proof-of-work function, SHA256d, that is pure
and stateless: a block header is valid or invalid on its own, independent of
where it sits in the block tree. That purity prices security in a single
resource — energy — and lets any observer verify a proof with nothing but the
header. Bitcoin Next Generation (BNG) is a fork of Bitcoin Core that keeps
Bitcoin's economic and scripting model but replaces the single, stateless energy
proof with a **dual-scarcity** proof-of-work: an inherited energy lane and a new
**memory-hard, context-retentive** lane whose scarce resource is sustained
possession of a large in-memory state. The memory lane's proof, CTA-SHA256, is
executed against a persistent context machine (CTAM) whose state advances with
the chain. This converts proof-of-work from a position-independent check into a
**path-dependent, stateful** one, and that single change ripples through mining,
validation, and — most consequentially — chain reorganization. This paper
describes BNG's extended block header, its two difficulty lanes, the CTAM
execution model, and the consensus machinery that a stateful proof requires but
Bitcoin never needed: an explicit way to move proof state *backward* when the
chain does.

## 1. Introduction

Bitcoin's proof-of-work has a property that is easy to take for granted:
`CheckProofOfWork(hash, nBits)` depends only on the block header. A header that
satisfies the target is equally valid on the main chain, on a stale side branch,
in isolation, while being connected, or while being disconnected during a reorg.
Bitcoin's entire chain-selection machinery — evaluating competing branches,
reorganizing to the heaviest chain, connecting and disconnecting blocks freely —
rests on the fact that validity carries no state.

That statelessness is also what makes Bitcoin's security one-dimensional. The
only scarce input is hashing energy. Specialized hardware (ASICs) drives the
marginal cost of that energy toward a single global optimization frontier. BNG
does not try to abolish that model; it treats energy-priced work as a proven
baseline and adds a *second, orthogonal* scarcity: the ability to hold and
manipulate a large, bounded memory context and to prove that retention while
producing and verifying blocks.

BNG is therefore not a redesign of Bitcoin from first principles. It is Bitcoin
Core with four deliberate departures, each captured as an accepted GIP:

- **Network identity** (GIP-0001): distinct magic bytes, address prefixes,
  ports, and genesis, so BNG is a separate network that cannot be confused with
  or accidentally connected to Bitcoin.
- **Replay protection** (GIP-0002): fork-specific signature hashing so
  transactions cannot be replayed across the Bitcoin/BNG UTXO boundary.
- **Dual mining difficulty** (GIP-0003): two independently retargeted
  proof-of-work lanes, energy and memory, both committed in every header.
- **CTAM proof execution** (GIP-0004): the memory lane's proof is executed by an
  external context machine over a well-defined interface, with the node
  remaining the sole authority on validity.

The remainder of this paper focuses on what is genuinely new: the memory lane,
its state model, and the consensus consequences of statefulness.

## 2. Relationship to Bitcoin

BNG inherits, unchanged, the parts of Bitcoin that are orthogonal to
proof-of-work: the UTXO model, transaction format, Script (including SegWit and
Taproot), the mempool, the 21-million issuance schedule and halving cadence, and
the peer-to-peer gossip protocol. What changes is the block header, the
proof-of-work function, difficulty adjustment, network identity, and the
signature-hash domain.

Because BNG shares Bitcoin's outpoint structure (it may be bootstrapped from a
UTXO snapshot), a transaction valid on one chain could otherwise be valid on the
other. GIP-0002 closes this by domain-separating every consensus signature-hash
path with a 4-byte fork identifier, `BNG_SIGHASH_FORKID = 0x00474E42`
(the bytes `42 4E 47 00`, ASCII `"BNG\0"`), across legacy ECDSA, SegWit v0
BIP143, and Taproot/Tapscript BIP341/342 Schnorr digests. A signature produced
under Bitcoin's rules does not validate under BNG's, and vice versa. GIP-0001
provides defense in depth with distinct network magic (`"BNG\x01"` mainnet,
`"BNG\x02"` testnet, `"BNG\x03"` regtest), address encodings (Bech32 HRPs `bng` /
`tbng` / `bngr`), ports, and a fresh genesis block.

## 3. The extended block header

Bitcoin's 80-byte header cannot express BNG's additional commitments, so BNG
replaces it outright with a fixed **115-byte** header. There is no parallel
header type and no 80-byte compatibility mode; the legacy header simply ceases
to exist in BNG. The serialized layout is:

```
version                  4 bytes    offset  0
prev_block_hash         32 bytes    offset  4
tx_merkle_root          32 bytes    offset 36
history_mmr_root        32 bytes    offset 68
time                     4 bytes    offset 100
energy_nbits             4 bytes    offset 104
major_work_ctx_size      2 bytes    offset 108
minor_work_rounds        1 byte     offset 110
nonce                    4 bytes    offset 111
--------------------------------------------
total                  115 bytes
```

The block hash is computed over all 115 bytes. Three fields are new relative to
Bitcoin:

- **`history_mmr_root`** commits to an accumulator (a Merkle Mountain Range) over
  prior block history, giving each block a compact commitment to the chain that
  precedes it. (The accumulator's exact construction is an area of ongoing work;
  its presence in the header reserves the commitment.)
- **`major_work_ctx_size`** encodes the memory lane's *major* difficulty as an
  exponent: the base-2 logarithm of the required CTAM context size. The
  consensus floor is `27`, i.e. `2^27` bits ≈ 16 MiB of retained state.
- **`minor_work_rounds`** encodes the memory lane's *minor* difficulty as a
  count of CTA-SHA256 injection rounds; it is bounded at 64 and fixed at 16 for
  initial consensus.

`energy_nbits` is Bitcoin's familiar compact target, retained for the energy
lane. The nonce is the final field (offset 111) so a miner mutates only the last
four serialized bytes while searching. The header thus commits, in every block,
to *both* proof lanes' difficulty state simultaneously.

## 4. Dual mining difficulty

BNG runs two proof-of-work lanes on an alternating schedule (GIP-0003). The
active lane for a block is chosen from the parent's median-time-past — not the
candidate's own timestamp, which a miner controls — via a week-parity rule:

```
week_number = 1 + floor((parent_mtp - dual_pow_epoch) / 604800)
```

Odd weeks are **energy blocks**, validated with the inherited SHA256d check
against `energy_nbits`. Even weeks are **memory blocks**, validated with the
CTAM memory-hard proof against the context-size difficulty. Whichever lane is
inactive for a given block must still carry its correct, expected target in the
header, so both difficulty states remain continuously committed and auditable.

The two lanes retarget **independently**: the energy target readjusts at the
first odd-week block after an even week, the memory target at the first even-week
block after an odd week, and each inactive lane carries its previous target
forward unchanged. The goal is to prevent a hashrate shock in one lane from
destabilizing the other.

Chain selection must then place a total order over a history that mixes the two
block types. BNG accumulates a per-lane work contribution and compares a combined
score, `dual_work = energy_work_sum + memory_work_sum`. **The normalization
between an energy-work unit and a memory-work unit is an open consensus
question** (Section 9): a naïve reuse of Bitcoin's chainwork accumulator is
insufficient unless the memory verifier exposes a comparable monotonic work
metric. This is the single most important unfinished piece of the dual-lane
design, because chain selection is where the two scarcities must be reconciled
into one ordering.

## 5. CTAM and the memory-hard proof

The memory lane's proof-of-work is **CTA-SHA256** (also written `csha256`), a
memory-hard function whose difficulty is a triple, all three components of which
the header commits to:

- **energy difficulty** — the compact target the digest must meet (`energy_nbits`);
- **major / context-size difficulty** — the size of the in-memory context that
  must be retained (`major_work_ctx_size`, as a `2^n`-bit exponent);
- **minor / round difficulty** — the number of injection rounds
  (`minor_work_rounds`).

CTA-SHA256 is executed by a **Context Retention & Addressing Machine (CTAM)**: a
service that owns a large, bounded memory buffer and performs the memory-hard
hashing and nonce search over node-supplied header bytes. BNG deliberately does
**not** implement CTA-SHA256 inside the node. The node's boundary is narrow and
strict:

- BNG owns block assembly, the requested difficulty, final block validation, and
  the decision to accept a result.
- CTAM owns CTA-SHA256 execution, nonce enumeration, and the mining-job state.
- CTAM is treated as an **untrusted execution aid**. Every proof CTAM returns is
  re-checked by the node against the header's committed difficulty. CTAM is never
  a consensus oracle; it is closer to a specialized coprocessor invoked over a
  well-defined interface (in the reference deployment, a local REST service).

Growing the context (a *major* difficulty increase) is deterministic: CTAM
extends its state buffer by repeatedly copying a consensus-fixed 16 MiB
transition blob, `minranseed.blob`, whose SHA256 is pinned to
`b3f09ab7f70f41be49037cb9bf9901ce45066e66b59e2590e5bb06d74edf63e1`. Fixing this
material by digest is what lets independent validators replay the same resize
history rather than depending on process-local randomness.

## 6. Statefulness: the trunk and branch model

Here is the conceptual departure that shapes everything downstream.

Bitcoin's proof is a pure function of the header. CTA-SHA256 is not. Its result
depends on the **memory state** the machine holds when it runs. To mine or verify
the block at height *N*, CTAM's memory must equal the cumulative state produced
by applying blocks *1..N-1*. CTAM therefore maintains a **trunk**: a single
linear state that represents the chain prefix it currently stands on.

Mining a block produces a candidate **branch** off the trunk. When a branch wins
the proof search, it is **committed**, promoting it to the new trunk tip and
advancing the trunk's height by exactly one. Commit is the "redo" primitive of a
block *connection*: it is the operation that moves proof state forward.

Verification is stateful too, but non-committing: to verify block *N*, the node
requires CTAM's trunk to already sit at *N-1*, replays the solved header against
that state without retaining a branch, and checks that the digest meets the
target. A block the node itself just mined is already the trunk tip, so its
verification is a trivial match; a block received from a peer requires the trunk
to be positioned at the block's parent.

The essential invariant is therefore:

> **CTAM's trunk state must correspond to the same chain prefix the node's active
> chain is on, or mining and memory-lane verification are undefined.**

## 7. Reorganizations and the rewind requirement

Bitcoin's chainstate is inherently *reversible*. Blocks are connected and
disconnected as the node reorganizes to the heaviest chain. This happens
routinely: a competing block arrives at the same height, a self-mined block is
orphaned, a deeper reorg switches branches, or an operator invalidates a block.

A stateful proof shadowing a reversible chainstate needs to move in **both**
directions. Commit provides the forward direction (the redo of a connection).
The backward direction — the undo of a *disconnection* — is the piece a stateless
proof never required and that BNG must therefore add. Without it, the trunk can
only ever move forward, which no proof-of-work chain does.

The consequence of missing that primitive is concrete. Suppose CTAM's trunk is
at height 100. A candidate block for height 101 is assembled, its header is
solved, and the winning branch is committed — advancing the trunk to 101 — before
the node's full body validation runs. If that block is then rejected (a
double-spend, an invalid signature, or simply losing a reorg race), the node's
active tip stays at 100 while CTAM's trunk is stranded at 101. Every subsequent
mining or verification call fails its precondition, because nothing can move the
trunk back down.

The only recovery available with today's primitives is a full reset of CTAM to an
empty state followed by replaying the entire chain forward, block by block,
through the memory-hard function. That costs work proportional to the **whole
chain height**, not to the depth of the disturbance. BNG's answer is an explicit
**rewind** primitive that moves the trunk backward to a previously committed
state, identified by the `(height, block_hash)` the node already holds in its
block index. Reorg recovery then becomes:

1. find the common ancestor of CTAM's trunk and the node's active chain;
2. **rewind** the trunk to that ancestor;
3. fast-forward along the new branch, committing only the blocks that changed.

This bounds recovery to **O(reorg depth)** — matching Bitcoin Core's own
disconnect cost — instead of O(chain height).

### 7.1 Why this is a security property, not a convenience

The forward/backward asymmetry is a denial-of-service surface. If the only way to
undo a commit is reset-and-replay-from-genesis, an adversary who can cheaply
induce shallow reorgs at the tip — for O(1) attacker cost — forces the victim to
redo O(chain height) of *memory-hard* work before it can mine or validate again.
The deeper the chain grows, the cheaper the attack becomes relative to the damage,
and a miner poked this way is repeatedly knocked out of consensus. Bitcoin has no
analogue because its disconnect cost is already O(reorg depth): there is no proof
state to rebuild. Rewind restores that bound for BNG and removes the asymmetry.

For some node classes, reset-and-replay is not merely slow but **impossible**,
turning the DoS into a hard liveness failure:

- **Pruned nodes** have discarded historical block bodies and cannot recompute
  the inputs needed to replay from genesis. They can supply only the recent
  blocks around a fork point — exactly what rewind-to-fork needs, and exactly
  what genesis replay cannot use.
- **Snapshot / assumeutxo-synced nodes** never validated pre-snapshot history and
  may not hold those bodies at all, so genesis replay is unavailable by
  construction.

Rewind operates on CTAM's own retained state rather than on block bodies the node
no longer has, so it keeps these nodes recoverable. Rewind never weakens
validation: the node still independently validates every block and still treats
CTAM as untrusted. Rewind only changes *how cheaply* CTAM's trunk can be kept
congruent with the chain the node has already accepted on its own. A full reset
plus replay remains the correct fallback when no valid rewind target exists
(first-time bootstrap, or a target CTAM has no record of); rewind is the fast
path, not the only path.

## 8. Difficulty adjustment and bootstrap

Each lane retargets on its own schedule (Section 4). The memory lane's retarget
algorithm, and the precise normalization of memory-work into a chain-selection
metric, are consensus-critical and not yet frozen.

At node startup — and after any reorg — the node reconciles CTAM's persisted
trunk against its active chain before enabling memory-lane mining or
replay-dependent validation. It compares CTAM's reported `(height, hash)` with
its own tip and then: reuses the state if they match; fast-forwards CTAM if it is
behind on the same prefix; rewinds (Section 7) if CTAM stands on a stale prefix;
and falls back to reset-and-replay only when no cheaper reconciliation is
possible. CTAM-backed mining is refused until this synchronization proves the
trunk matches the node's active prefix.

## 9. Security considerations

- **Two attack surfaces, one ordering.** Adding a second proof lane means chain
  selection must fairly weigh two incommensurable resources. An incorrect
  work-normalization rule could let one lane cheaply dominate the other; fixing
  the normalization is a prerequisite for mainnet.
- **Bounded validation cost.** A memory block declares its required context size
  in the header. Validators must sanity-check that declared size before
  allocating or streaming proof material, so a malicious block cannot force an
  unreasonable allocation.
- **Untrusted execution.** CTAM output is never accepted on faith; the node
  re-derives and re-checks every proof. A compromised or buggy CTAM can waste the
  local miner's effort but cannot make the node accept an invalid block.
- **Reorg-driven state-recovery DoS.** Addressed by the rewind primitive
  (Section 7.1); without it the memory lane would be strictly more DoS-exposed
  than Bitcoin.
- **Replay and network isolation.** Fork-specific sighashing (GIP-0002) plus
  distinct identity (GIP-0001) prevent cross-chain replay for signature-validated
  spends. Anyone-can-spend outputs remain replayable by construction, as in any
  UTXO-sharing fork, and should be documented as a known limitation.

## 10. Implementation status

BNG is implemented as a hard-fork patch of Bitcoin Core rather than a
green-field client, reusing the same class names and call graph where possible:

- **Implemented:** network identity and rebranding (GIP-0001); fork-specific
  signature hashing across all consensus sighash paths (GIP-0002); the 115-byte
  extended header with serialization, hashing, and validation; a CTAM REST client
  and mining backend that assembles blocks, solves headers via CTAM mining jobs,
  and re-verifies solved headers; and a startup/replay synchronization path.
- **Prototype / in progress:** the memory-lane consensus verifier and its
  work metric; the branch/commit lifecycle wired through the chainstate connect
  path; the history-MMR accumulator behind `history_mmr_root`.
- **Specified but not yet built:** the CTAM **rewind** primitive (Section 7) and
  its integration into reorg handling; independent per-lane retargeting.

The reference deployment runs CTAM as a local service (e.g. Docker Compose)
alongside the node, and development networks (regtest first) gate every step
before any wider activation. Because the header, sighash domain, and genesis all
differ, BNG is not consensus-compatible with Bitcoin and requires its own network.

## 11. Open problems

1. **Work normalization.** How to convert memory-lane proof quality into a
   cumulative-work unit commensurable with energy work, so `dual_work` yields a
   single sound chain ordering.
2. **CTAM rewind semantics.** How far back a CTAM instance can rewind (full
   history vs. a bounded checkpoint window), and how a node behaves when a rewind
   target is unavailable.
3. **Proof placement.** Whether the memory-lane proof witness is header-adjacent,
   coinbase-committed, or witness-committed.
4. **History accumulator.** The exact MMR construction committed by
   `history_mmr_root` and what validation leverage it provides (fast sync,
   fraud proofs, offline verification).
5. **Trunk lifecycle ownership.** Whether the CTAM trunk should shadow the
   chainstate's connect/disconnect lifecycle generally (commit on connect, rewind
   on disconnect), or remain miner-driven with verification as a separate path.
6. **Activation.** Whether BNG launches from genesis or activates on a live fork
   at a fixed height, and (per GIP-0003) whether the first activated week is
   forced to the energy lane.

## 12. Conclusion

Bitcoin priced security in a single resource and got a stateless proof for free.
BNG's thesis is that a second, orthogonal scarcity — sustained retention of a
large memory context — can be added on top of energy work without discarding
Bitcoin's economic model. The cost of that thesis is precise and unavoidable: a
context-retentive proof is stateful, and a stateful proof shadowing a reversible
chainstate must be able to move in both directions. Most of BNG's genuinely new
consensus machinery — the trunk model, the commit lifecycle, and the rewind
primitive — exists to keep proof state congruent with a chain that can, and
routinely does, reorganize. Getting that bidirectional state management right,
and defining a sound cross-lane work metric, are the two problems on which a
production BNG network depends.

---

### References

- GIP-0001 — Network Identity and Rebranding (accepted)
- GIP-0002 — Replay Attack Protection (accepted)
- GIP-0003 — Dual Mining Difficulty (accepted)
- GIP-0004 — CTAM REST Mining Interface (accepted), incl. `consensus.md`,
  `bng-header.md`, `protocol-extension.md`, and the implementation notes for the
  branch-managed mining backend
- Nakamoto, S. *Bitcoin: A Peer-to-Peer Electronic Cash System* (2008)