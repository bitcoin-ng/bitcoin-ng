# Mining Control Protocol

This document extends BNG-0004 with an end-to-end control flow for CTAM-backed mining over REST.
It focuses on the admin-side coordination needed to move CTAM between major-difficulty epochs while keeping mining requests simple and strictly validated.

This is intentionally a transport and workflow note, not the final consensus specification.
The rule that decides when the major CTAM difficulty grows or shrinks, and any block-level commitment that identifies a resize transition, belongs in BNG-0003.

## Scope

- BNG remains the client that decides which mining difficulty is active for the next block.
- CTAM remains the server that owns the in-memory state buffer and performs the actual resize.
- The node remains responsible for block template construction and final candidate validation.
- CTAM is responsible for the delegated memory-hard hashing and proof-search work (mining) over node-supplied inputs.
- Mining requests must never silently resize the CTAM machine.
- The node must be aware of every resize event because it determines whether outstanding mining work is still valid.

## Existing low-level primitive

CTAM already exposes a low-level resize primitive:

- `GET /admin/context-size`
- `POST /admin/context-size`
- `GET /status`

That surface is enough to describe the resize lifecycle if the node treats it as a small state machine:
initiate the resize with `POST /admin/context-size`, monitor server liveness with `GET /status`, and treat `GET /admin/context-size` as the completion check.

## Using the existing admin API

This note should not invent new admin endpoints.
The existing CTAM admin surface is enough for the protocol described here:

- `GET /admin/context-size` to read the live CTAM major difficulty
- `POST /admin/context-size` to apply the consensus-required resize on the CTAM server side
- `GET /status` to confirm that CTAM is alive while the node is waiting for the resize to complete

The node should treat those endpoints as operational primitives, not as a place where consensus is decided.
Consensus still determines when a resize must happen; CTAM only reports and applies the resulting `context_size`.

## Blocking resize behavior and client polling

On the CTAM server side, resize is synchronous and blocks other operations until the grow or shrink step has completed and the new state has been persisted.
The asynchronous part of the workflow exists on the node side: the node may poll CTAM while waiting to observe that the target major difficulty is live.

The intended node-side behavior is:

1. The node calls `POST /admin/context-size` with the target consensus `context_size`.
2. The node immediately marks all work built for the old major difficulty as stale.
3. While waiting for the resize call to finish or after reconnecting, the node may poll `GET /status` to confirm that CTAM is responsive.
4. The node polls `GET /admin/context-size` until the reported value equals the requested target.
5. Only after `GET /admin/context-size` returns the new value does the node resume memory-lane mining work.
6. In case of failure, `GET /status` will report a broken server state.

This keeps the transport simple.
The node does not need a dedicated resize job API because CTAM does not expose resize as a background job. The existing endpoints are enough to distinguish the useful operational states:

- CTAM is alive and still on the old `context_size`: the node has not yet observed the completed resize
- CTAM is alive and now reports the new `context_size`: resize finished
- CTAM is not responding cleanly: resize failed or the service is unavailable

For protocol purposes, `GET /admin/context-size` is the authoritative completion signal.
`GET /status` is only the liveness and observability check.

## End-to-end difficulty update flow

1. The node derives the active lane and target CTAM difficulty from the consensus rules defined by BNG-0003.
2. The node queries `GET /admin/context-size` to learn the live CTAM state.
3. If the live `context_size` does not match the consensus-required major difficulty, the node calls `POST /admin/context-size` with the required value.
4. Both the node and the CTAM server invalidate any outstanding mining work that was prepared under the old major difficulty.
5. The node polls `GET /status` and `GET /admin/context-size` until CTAM is responsive and the target `context_size` is live.
6. CTAM persists the resized state before reporting the new `context_size` as active.
7. The node starts fresh mining work and includes the updated `context_size_difficulty` in `POST /crypto/cta_sha256` or `POST /crypto/mine_csha256`.
8. The crypto endpoint rejects the request if the caller's major difficulty still does not match the live CTAM state.

This keeps resizing explicit, observable, and separate from the hot mining path.

## Growing

The initial floor for the major CTAM difficulty is `context_size = 27`, which corresponds to about 16 MiB of CTAM state.
That 16 MiB window should be treated as the minimum state chunk used by the growth logic.

For a grow event from `old_context_size` to `new_context_size` where `new_context_size > old_context_size`:

1. CTAM allocates the new state buffer implied by `2^new_context_size` bits.
2. CTAM copies the full old state into the low-address prefix of the new buffer unchanged.
3. CTAM fills the newly exposed address range by copying a deterministic 16 MiB transition blob repeatedly until the new range is covered.
4. If the final copy would exceed the target size, CTAM truncates the last copy to fit exactly.
5. CTAM persists the new state before accepting mining work for the new difficulty.

The 16 MiB transition blob is the consensus-defined `minranseed.blob` from BNG-0003.
Nodes and CTAM instances should verify that local file against the pinned SHA256 digest `b3f09ab7f70f41be49037cb9bf9901ce45066e66b59e2590e5bb06d74edf63e1` before using it for a grow event.
Reference deployments may ship the companion checksum artifact `minranseed.blob.sha256` alongside the blob.
It cannot be process-local randomness if nodes are expected to replay the same resize history during validation or offline verification.
On the CTAM deployment side, `config.toml` should point at the local `minranseed.blob` copy that provides this seed material for grow operations.
The consensus schedule that decides when that blob is applied still belongs in BNG-0003 rather than in this transport note.

The node does not perform the copy itself, but it must know that the grow event happened.
While CTAM is still reporting the old `context_size`, the node must treat the resize as in progress and keep memory-lane work paused.
Once CTAM reports the new `context_size`, any old mining work remains stale because the memory lane has changed underneath it.

## Shrinking

For a shrink event from `old_context_size` to `new_context_size` where `new_context_size < old_context_size`:

1. CTAM allocates the smaller state buffer implied by `2^new_context_size` bits.
2. CTAM keeps the low-address prefix that still fits inside the new buffer.
3. CTAM discards the truncated high-address range.
4. CTAM drops any branch or overlay entries that reference addresses outside the new range.
5. CTAM persists the smaller state before accepting mining work for the new difficulty.

Shrinking must also invalidate in-flight work on the node side.
Even if the block template is otherwise unchanged, the proof search is no longer targeting the same memory state.

## Mining endpoint rules after resize

After a resize has been applied:

- `POST /crypto/cta_sha256` may still be used as a single-evaluation helper.
- `POST /crypto/mine_csha256` remains the mining-oriented endpoint.
- Both endpoints must validate `context_size_difficulty` against the live machine state.
- Neither endpoint should trigger a resize implicitly.

This matches the rule already described in CTAM's mining note: disagreement about major difficulty is a protocol mismatch, not a signal to mutate local state.

## Error handling

Suggested failure cases for `POST /admin/context-size` in this workflow:

- `422 Unprocessable Entity` if the requested major difficulty is below the protocol floor or otherwise invalid
- `503 Service Unavailable` if CTAM cannot complete the resize and persist the new state

During an in-progress resize, mining endpoints should continue returning a bad-argument style error when the requested major difficulty does not match the live machine state.
The node should interpret a persistent mismatch after a resize request as evidence that the resize has not completed yet, rather than as a signal to keep mining on stale state.

## Missing primitive: state rewind

CTAM mining is stateful and sequential. Each `POST /branches/{branch_key}/commit`
(wrapped by BNG as `CtamRestClient::CommitBranch`) promotes one branch to trunk
and advances CTAM's live state forward by exactly one block. There is currently
no server-side operation that moves the trunk backward. The only tool BNG has
today to make CTAM's trunk represent an earlier chain state is
`POST /admin/reset` (`CtamRestClient::AdminReset`), which wipes CTAM back to an
empty, height-less trunk. Recovering any non-trivial prior height then requires
replaying every block from height 1 forward through `POST /crypto/cta_sha256`
(`keep_branch = true`) plus `CommitBranch` again, one block at a time.
`TestChain100Setup::SyncCtamToActiveChain()` in `src/test/util/setup_common.cpp`
already implements exactly this reset-and-replay loop, because it is the only
mechanism available.

This gap was already flagged, but left unresolved, earlier in this GIP's
`consensus.md`:

> `EnsureCtamStateAtHeight(...)` should run before memory-lane mining starts and
> before any validation path that depends on live CTAM state. It may be called
> again after reorgs, but reorg handling should be explicit: either rewind to a
> trusted CTAM snapshot/checkpoint and replay the new branch, or rebuild from
> genesis if no rewind support exists.

This section originally proposed a new endpoint to fill that gap. It is now
implemented on the CTAM server as `POST /admin/trunk/rewind`; the shape below
reflects the implemented transport contract.

### Concrete failure this causes today

`src/test/txvalidationcache_tests.cpp`'s `tx_mempool_block_doublespend` case
deliberately builds a block containing two conflicting spends of the same
coinbase output, to verify BNG's consensus layer rejects it. The sequence is:

1. BNG assembles the invalid candidate block at height 101 (tip is at height 100).
2. `CtamMiningBackend::SolveHeader()` finds a winning nonce and calls
   `CommitBranch(...)`. CTAM's trunk is now at height 101.
3. `ProcessNewBlock()` runs full body validation and rejects the block for the
   double-spend. BNG's active chain tip stays at height 100.
4. CTAM's trunk (101) and BNG's real tip (100) are now permanently out of sync.
   The next `SolveHeader()` call fails its precondition check
   (`RequireCtamTrunkState`) with `CTAM trunk height 101 does not match expected
   BNG height 100`, because nothing can move CTAM's trunk back down to 100
   short of a full reset and 100-block replay.

This is not specific to the test. Any real reorg, or any case where a solved
header is committed to CTAM but the corresponding block is later rejected or
disconnected by BNG consensus, hits the same problem: CTAM's PoW-level commit
happens before BNG's body-level validation completes, and there is no cheap way
to undo it.

### Why commit happens before body validation

This ordering is intentional, not a bug to route around by reordering the
mining code. Branch commit is how CTAM's mining-job protocol records "this
branch won the CTAM-side proof search"; it is a statement about proof-of-work,
not about full block validity. BNG still validates the block body locally and
independently after commit (see `impl-mining-flow.md` and
`impl-branch-managed-mining.md`: CTAM is only ever the executor of BNG block
proof-of-work, never the arbiter of consensus validity). Making commit
provisional on body validation would require CTAM to understand BNG consensus
rules, which is exactly the boundary this GIP was designed to avoid crossing.
The correct fix is therefore giving BNG a way to tell CTAM "go back to the state
before that commit," not delaying the commit itself.

### Endpoint: `rewind` (implemented)

```text
POST /admin/trunk/rewind
```

This endpoint is now implemented on the CTAM server. It rewinds the current
trunk tip by one or more committed blocks in a single jump, bounded by a
retained rewind history window rather than by full chain history (see "Bounded
rewind window" below). The transport body carries chain metadata for both
endpoints of the jump — the current tip and the target — so CTAM can validate
that its retained history actually chains from where BNG believes the trunk is
down to where BNG wants it.

Request:

```json
{
  "tip_height": 103,
  "tip_block_hash": "0000000000000000000000000000000000000000000000000000000000000103",
  "back_to_height": 100,
  "back_to_block_hash": "0000000000000000000000000000000000000000000000000000000000000100",
  "num_of_commits": 3
}
```

- `tip_height` / `tip_block_hash`: the trunk tip BNG believes CTAM is currently
  on. CTAM rejects the request unless its live `/status` metadata matches these
  exactly, so a stale caller cannot rewind from a state it has misidentified.
- `back_to_height` / `back_to_block_hash`: the checkpoint the trunk should be
  rewound to. The same values BNG recorded for that height at commit time (the
  `bng_height`/`bng_block_hash` arguments to `CommitBranch`).
- `num_of_commits` (optional): the number of committed blocks to undo. When
  present it must equal `tip_height - back_to_height`, acting as an explicit
  cross-check of the intended jump size.

The request does not include a branch key. Rewind walks back from the current
trunk tip, and the branch states needed to undo those blocks are an internal
CTAM detail. This mirrors why `CommitBranch` already takes
`(bng_height, bng_block_hash)`: BNG's own bookkeeping, identified by
height/hash rather than CTAM-internal branch numbering, is the source of truth
for which chain state is being referenced, and BNG does not retain CTAM's
branch keys once a branch has been committed and superseded.

Response:

```json
{
  "status": "rewound",
  "old_height": 103,
  "old_block_hash": "0000000000000000000000000000000000000000000000000000000000000103",
  "new_height": 100,
  "new_block_hash": "0000000000000000000000000000000000000000000000000000000000000100",
  "trunk_key": 41,
  "applied_entries": 12345,
  "num_of_commits": 3,
  "remaining_rewind_depth": 5
}
```

- `old_*` / `new_*`: the tip before and after the jump.
- `applied_entries`: total number of trunk bits XORed across the popped branches.
- `remaining_rewind_depth`: how many rewind operands are still retained after
  the call, i.e. how many further steps are possible before the client must
  reconstruct/fast-forward instead.

Error cases:

- `404 Not Found`: rewind history is unavailable or too shallow for the
  requested jump (the retained window does not reach `back_to_height`). The
  failed jump consumes no history. The caller falls back to a full `AdminReset`
  plus replay, exactly as it does today.
- `409 Conflict`: the live trunk metadata does not match `tip_height` /
  `tip_block_hash`; or the retained history does not chain contiguously to the
  requested target (e.g. the target is below the first retained entry, at the
  genesis boundary); or an active mining job prevents trunk mutation.
- `422 Unprocessable Entity`: invalid height/hash/context-size, or
  `num_of_commits` not equal to `tip_height - back_to_height`.
- `503 Service Unavailable`: persistence failure after applying the branches.

Required behavior:

- Rewind undoes committed blocks using GF(2) (XOR) algebra: each committed
  branch retains the sparse state it XORed into the trunk, and re-applying that
  same branch (`trunk[idx] = trunk[idx] ^ branch_bit`) undoes exactly that
  block, because XOR is its own inverse. A jump pops the top
  `tip_height - back_to_height` branches newest-first and XORs each back out.
- Rewind must restore `prev_ctx_seed` to the seed in effect at the target
  checkpoint, not only `trunk.states`: CTA/CXOR uses the seed as part of the
  next operation's input context, so restoring trunk bits alone would not
  recreate the previous trunk context. It also moves `trunk_key` back to the
  target and sets the trunk metadata to `back_to_height`/`back_to_block_hash`.
- Rewind must drop or orphan every branch state above the target, so that a
  subsequent commit at `back_to_height + 1` behaves identically to a commit
  that had never been superseded.
- Rewind must be a privileged/admin operation, not reachable from the ordinary
  mining hot path (`/mining`, `/crypto/cta_sha256`). It is orchestration, not
  mining, exactly like `/admin/context-size` above.

#### Bounded rewind window

CTAM retains a bounded LIFO stack of committed branches as rewind operands,
depth-limited by `rewind_history_depth` (`config.toml`, default 16). On every
checkpointed commit the committed branch is pushed onto the stack; when the
depth is exceeded the oldest entry is evicted. The stack is persisted
(`committed_branches.bin`) atomically with the trunk checkpoint, so the retained
window survives restart.

Branches are compact but still sparse state maps, so an unbounded journal has no
clear growth bound. The fixed-depth window matches short reorg handling: BNG
rewinds the tip by a few blocks and immediately commits replacements. Two things
invalidate the retained window and force a full reset + replay instead of a
rewind: a jump deeper than the window (404), and a plain commit with no
checkpoint metadata, which clears the stack because the trunk mutated without a
replayable undo record. BNG-side callers must therefore always treat "target
not available" as possible and keep the full-reset-plus-replay fallback.

### Rewind and reorg/error-recovery flow

This does not replace the resize primitive or the existing fast-forward path;
it fills the missing backward-motion gap:

1. Compare CTAM's live `(bng_height, bng_block_hash)` against BNG's active
   chain.
2. If CTAM's trunk is on a stale or now-invalid branch, rewind to the common
   ancestor height/hash instead of resetting to genesis.
3. Fast-forward from the common ancestor along BNG's current best chain, using
   the existing replay loop (`POST /crypto/cta_sha256` with `keep_branch = true`
   plus `CommitBranch`, repeated per block).

This turns an O(chain height) recovery (full reset + replay from genesis) into
an O(reorg depth) recovery (rewind to the fork point + replay only the new
blocks), which matters once chain height is large enough that a full replay is
not a viable response to every reorg or rejected-body commit.

Rewind is a CTAM-local operational primitive, not a consensus rule. It does not
change what BNG accepts as valid; it only changes how cheaply CTAM's mining and
verification state can be kept congruent with BNG's already-independently-
validated chain. BNG must keep validating blocks locally regardless of what
CTAM reports, exactly as the fast-forward section of `consensus.md` already
states. Whether a rewind event itself needs any header-level or block-level
commitment is a BNG-0003 question, not one this note answers; today's answer is
"no, because CTAM state is not part of consensus, only an execution aid for
PoW."

### Reference implementation touch points

- CTAM server (separate Rust project, outside this repository's `src/` tree):
  the `/admin/trunk/rewind` endpoint, the retained committed-branch LIFO stack
  and its XOR-based rewind logic, and `rewind_history_depth` wiring are
  implemented there. This is the part of this note that is not implementable
  from the BNG side alone.
- `src/crypto/ctam_client.h` / `.cpp`: a `Rewind` wrapper carrying the request
  tuple, alongside the existing `AdminReset` and `CommitBranch` wrappers:
  `RewindResult Rewind(uint64_t tip_height, std::string_view tip_block_hash, uint64_t back_to_height, std::string_view back_to_block_hash, std::optional<uint64_t> num_of_commits = std::nullopt) const;`
  (implemented).
- `src/node/ctam_miner.h` / `.cpp`: teach the reorg/resync path to prefer
  rewind-to-common-ancestor over full reset, falling back to `AdminReset` +
  replay-from-genesis when CTAM reports the target is unknown.
- `src/test/util/setup_common.cpp`: `SyncCtamToActiveChain()` now prefers
  rewind-to-target when CTAM's trunk is ahead of the requested index, falling
  back to reset-and-replay-from-height-1 when rewind is unavailable, without
  changing its external behavior or signature. `CreateBlock()` reconciles CTAM
  to the active tip via this path before solving, so a block committed to CTAM
  but then rejected by BNG consensus (the double-spend tests) no longer leaves
  CTAM's trunk stuck ahead of the active chain.

## Relationship to BNG-0003

This document only proposes how the node and CTAM coordinate a difficulty transition over REST, and how they coordinate moving CTAM's trunk backward when a committed block turns out not to be part of BNG's final chain.
The following items should be finalized in BNG-0003:

- the consensus schedule that decides when major CTAM difficulty grows or shrinks
- any header commitment, witness commitment, or block-level identifier for the transition
- how validators replay the same resize history during block verification and reorg handling
- whether a rewind event needs any consensus-visible commitment at all, or remains purely a CTAM-local operational aid
