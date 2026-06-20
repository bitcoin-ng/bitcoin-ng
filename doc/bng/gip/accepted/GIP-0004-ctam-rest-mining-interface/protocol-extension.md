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

## Relationship to BNG-0003

This document only proposes how the node and CTAM coordinate a difficulty transition over REST.
The following items should be finalized in BNG-0003:

- the consensus schedule that decides when major CTAM difficulty grows or shrinks
- any header commitment, witness commitment, or block-level identifier for the transition
- how validators replay the same resize history during block verification and reorg handling
