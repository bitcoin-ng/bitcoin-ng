Step 6 in `impl-branch-managed-mining.md` is a good start, but it needs to be expanded into a full lifecycle rule that treats CTAM state as a proof of the BNG chain prefix. This is really a protocol/control-flow step, not just a local “check context size” patch.

The core invariant should be:

**BNG must only mine or verify against a CTAM trunk state that exactly corresponds to the BNG chain prefix being extended.**

That means CTAM state must match:

- expected BNG height
- expected previous block hash
- expected CTAM context size
- expected CTAM trunk identity/key, if exposed
- expected mining difficulty tuple
- no stale candidate branch from an old prefix may be committed

A deeper plan:

**1. Define The State Contract**

BNG should treat CTAM `/status` as a state proof before every mining job and verification call.

Required CTAM status fields:

```json
{
  "status": "running",
  "context_size": 27,
  "trunk_key": 0,
  "bng_height": 123,
  "bng_block_hash": "...prev block hash...",
  "branch_count": 0
}
```

BNG should require:

```cpp
status.status == "running"
status.context_size == requested_major_context_size
status.bng_height == pindexPrev->nHeight
status.bng_block_hash == pindexPrev->GetBlockHash()
```

Current code already has the beginning of this with `RequireCtamTrunkState(...)`, which is good. Step 6 should harden and expand that into a full lifecycle rule.

**2. Separate Three States**

The backend should explicitly model these as different situations:

`ready`
: CTAM trunk matches the BNG prefix and context size. Mining/verification may proceed.

`stale`
: CTAM is running, but height/hash/context size do not match the requested BNG prefix. Mining must not start, and existing work must not commit.

`unavailable`
: CTAM is down, malformed, missing metadata, resizing, or otherwise unable to prove state. Mining/verification fails closed.

This avoids accidentally treating “CTAM responded” as “CTAM is safe to use.”

**3. Preflight Before Mining**

Before `POST /mining`, `CtamMiningBackend::SolveHeader()` should:

1. read BNG tip under `cs_main`
2. capture `expected_height`
3. capture `expected_prev_hash`
4. call CTAM `/status`
5. require CTAM running
6. require CTAM height/hash match
7. require CTAM context size matches the major difficulty
8. only then submit `/mining`

If any check fails, do not submit a mining job.

**4. Recheck Before Accepting A Winner**

This is the big stale-work guard.

After `/mining/{job_id}` returns a found nonce, but before replaying/committing the winning branch, BNG should recheck:

- chain tip is still the same expected height/hash
- CTAM `/status` still matches the same expected height/hash/context size
- mining job metadata still matches the submitted request
- winning nonce is inside the submitted range
- digest matches solved-header replay
- digest satisfies target

Only then commit the winning branch.

This prevents a job that started on prefix A from being committed after the node has moved to prefix B.

**5. Stop Jobs On Local Staleness**

While polling, the backend should periodically check whether local BNG work is stale.

For example, while waiting for CTAM:

```cpp
if (chainman.ActiveChain().Tip()->GetBlockHash() != expected_prev_hash) {
    client.StopMiningJob(job.job_id);
    return false;
}
```

This gives BNG control over the flow even though CTAM owns the nonce search.

**6. Context Size Rules**

I would make the rule strict:

- BNG does not resize CTAM during mining.
- BNG does not silently accept a larger/smaller CTAM context.
- If `status.context_size != requested_major_context_size`, return stale/unavailable.
- Resizing CTAM is an explicit operator or higher-level synchronization action, outside `SolveHeader()`.

That keeps mining deterministic and avoids “the backend fixed it for me” behavior in consensus-facing code.

**7. Verification Path Is Even Stricter**

For `VerifyHeader()`:

- check CTAM height/hash equals `pindexPrev`
- check context size equals expected major difficulty
- call only `/crypto/cta_sha256`
- use `keep_branch=false`
- reject if CTAM retained a branch
- do not call `/mining`
- do not commit

Verification should be a pure replay against the matching CTAM trunk.

**8. Add Tests By Scenario**

Step 6 tests should cover flow control, not just individual fields:

- CTAM height mismatch before mining: no `/mining` request sent
- CTAM prev hash mismatch before mining: no `/mining` request sent
- CTAM context size mismatch before mining: no `/mining` request sent
- CTAM status missing `bng_height`: fail closed
- CTAM status missing `bng_block_hash`: fail closed
- local tip changes while polling: `POST /mining/{job_id}/stop`, no commit
- CTAM status changes after winning nonce but before commit: no commit
- verification height/hash mismatch: no `/crypto/cta_sha256`
- verification uses `keep_branch=false`
- winning branch commit only happens after final state recheck

**9. Likely Backend Shape**

I’d add one internal helper around the existing checks:

```cpp
struct CtamExpectedTrunk {
    int height;
    uint256 block_hash;
    uint32_t context_size;
};

void RequireCtamReadyForPrefix(
    const CtamRestClient::StatusResult& status,
    const CtamExpectedTrunk& expected);
```

And one local chain check:

```cpp
bool ChainTipStillMatches(
    ChainstateManager& chainman,
    int expected_height,
    const uint256& expected_hash);
```

Then `SolveHeader()` becomes:

```text
capture expected prefix
status preflight
start mining
poll:
  if interrupt: stop
  if local tip stale: stop
  poll job
on found:
  recheck local tip
  recheck CTAM status
  replay solved header
  commit branch
```

That is the flow-control heart of Step 6. The important design choice is: **BNG remains the authority on which chain prefix is being extended, and CTAM must prove it is aligned before BNG lets any CTAM branch affect mining outcome.**