# GIP-0004 Extended Functional Test Prompt

Write a new Bitcoin functional test for the CTAM REST mining integration.

The test must use the real BNG node, the real BNG regtest blockchain, and a real CTAM REST daemon. Do not use the toy-chain code from the CTAM repository for this test. The toy-chain tests may remain in the CTAM project, but this new test belongs in the BNG functional test suite.

Target file:

```text
test/functional/ctam_mining_flows.py
```

The filename should start with `ctam_`. If there is a better name, keep the `ctam_` prefix.

## High-Level Goal

Cover two separate lifecycle scenarios:

1. Genesis flow: BNG starts from a clean regtest chain and CTAM starts from an empty zero/null checkpoint state. BNG mines a few blocks using CTAM-backed mining, and CTAM advances by committing the winning branches.
2. Fast-forward flow: BNG already has a non-zero-height blockchain copy, while CTAM starts empty. The test replays the existing BNG block headers through CTAM using ordinary crypto/branch calls, commits periodically, and brings CTAM up to the copied chain tip without any `/admin/fast-forward` endpoint.

These are intentionally separate test cases, but they should share helper code for running CTAM, querying CTAM status, replaying headers, and committing checkpoints.

## Important Constraint: No Admin Fast-Forward

Do not add or call a CTAM `/admin/fast-forward` endpoint.

Fast-forward is a client-side replay process:

```text
for each existing BNG block header:
    POST /crypto/cta_sha256 with payload_hex = serialized 80-byte block header
    keep the resulting branch
    use clone_from to continue from the previous uncommitted replay branch
    every 10 blocks, commit the replay branch with bng_height and bng_block_hash
```

At the end of replay, CTAM `/status` must report the copied BNG chain tip height and block hash.

## Shared Test Infrastructure

Use `BitcoinTestFramework`.

Add a small CTAM REST client helper inside the functional test, or factor it locally if the test becomes large. The helper should support:

- `GET /health`
- `GET /status`
- `POST /crypto/cta_sha256`
- `POST /branches/{key}/commit`

The `POST /crypto/cta_sha256` payload should use the CTAM DTO field names:

```json
{
  "payload_hex": "<80-byte-header-hex>",
  "energy_difficulty_bits": <nBits>,
  "major_ctam_difficulty_context_size": <status.context_size>,
  "minor_ctam_difficulty_rounds": 16,
  "injections": 16,
  "keep_branch": true,
  "clone_from": <optional-branch-key>
}
```

For replay batches:

- The first header after a commit has no `clone_from`.
- Subsequent headers before the next commit use `clone_from = previous_branch_key`.
- Each request uses `keep_branch = true`.
- Commit every 10 blocks, and always commit the final partial batch.

Commit payload:

```json
{
  "bng_height": <height>,
  "bng_block_hash": "<block hash at height>"
}
```

The CTAM server helper should be modeled after the existing CTAM functional/e2e helpers:

- Locate CTAM repo with `CTAM_REPO_ROOT`, defaulting to the sibling `ctam` repo.
- Prefer `target/debug/ctam` if it exists and is executable.
- Otherwise run `cargo run --manifest-path <ctam>/Cargo.toml -- --serve rest --state-path <tempdir>`.
- Use a fresh CTAM state directory for each scenario.
- Wait for `/health`.
- If port `127.0.0.1:8080` is already in use, either skip with a clear reason or fail with a clear diagnostic. Do not reuse a dirty CTAM state silently.
- On startup failure, print the last CTAM log lines.

## Scenario 1: Genesis / Empty CTAM State

Purpose:

Verify the real BNG node can mine from a fresh chain while CTAM is in its zero/null checkpoint state.

Setup:

1. Start a fresh CTAM daemon with an empty state directory.
2. Assert CTAM `/status` reports:
   - `status == "running"`
   - `bng_height == null`
   - `bng_block_hash == null`
   - `trunk_key == 0`
3. Start one clean regtest BNG node with:
   - `-ctammining=1`
   - `-ctamresturl=http://127.0.0.1:8080/`
   - any wallet/fallback fee settings needed by the normal functional framework
4. Create/load a wallet and get a mining address.

Actions:

1. Mine a small number of blocks, for example 3, using RPC `generatetoaddress`.
2. After each block or at the end, query:
   - BNG `getblockcount`
   - BNG `getbestblockhash`
   - CTAM `/status`

Required assertions:

- BNG height reaches the expected value.
- CTAM `bng_height` equals BNG tip height.
- CTAM `bng_block_hash` equals BNG best block hash.
- CTAM `branch_count == 0` after committed winners.
- The wallet receives coinbase outputs as expected.
- A follow-up `getblock` or `getblockheader` on the mined blocks succeeds, proving the blocks are part of the real BNG chain.

This scenario proves the zero/null CTAM state is a valid starting point only when BNG is extending the genesis/height-zero prefix.

## Scenario 2: Client-Side Fast-Forward From A Non-Zero Chain Copy

Purpose:

Verify a fresh CTAM daemon can be synchronized to an already-existing BNG chain tip by replaying block headers through the normal CTAM crypto/branch API.

Setup:

1. Create a source BNG regtest chain with non-zero height, for example 23 blocks.
2. This source chain may be created by a preliminary BNG node run. Prefer using the real BNG mining path available in this branch. If needed, create the source chain with CTAM-backed genesis mining first, then stop the node.
3. Copy the source node datadir to a second datadir. This copied datadir is the fast-forward target chain.
4. Start a fresh CTAM daemon with empty state:
   - `bng_height == null`
   - `bng_block_hash == null`
5. Start a BNG node on the copied datadir in a mode that allows RPC access to the existing chain before mining the next block.

Header replay process:

1. Read the copied chain height with `getblockcount`.
2. For each height `1..tip_height`:
   - `block_hash = getblockhash(height)`
   - `block_hex = getblock(block_hash, 0)`
   - `header_hex = block_hex[:160]`
   - call CTAM `POST /crypto/cta_sha256` with `payload_hex = header_hex`
   - pass `clone_from` for in-batch continuation
   - set `keep_branch = true`
3. Every 10 headers:
   - commit the latest branch with `bng_height = height`
   - commit with `bng_block_hash = block_hash`
   - assert CTAM `/status` has that height/hash and `branch_count == 0`
   - clear the local `clone_from` branch key
4. Commit the final partial batch if `tip_height` is not divisible by 10.

Required assertions after replay:

- CTAM `bng_height == copied_node.getblockcount()`.
- CTAM `bng_block_hash == copied_node.getbestblockhash()`.
- CTAM `branch_count == 0`.
- CTAM `context_size` is still compatible with the requested major CTAM difficulty.

Post-fast-forward mining assertion:

After CTAM status matches the copied BNG tip:

1. Mine one additional block with the copied BNG node using CTAM-backed mining.
2. Assert BNG height increases by 1.
3. Assert CTAM `bng_height` and `bng_block_hash` advance to the new BNG tip.

This scenario proves fast-forward is achieved by replaying the actual BNG chain prefix into CTAM state, not by mutating CTAM checkpoint metadata directly.

## Suggested Test Shape

Use one test class:

```python
class CtamMiningFlowsTest(BitcoinTestFramework):
    ...
```

Suggested methods:

```python
def test_genesis_flow(self):
    ...

def test_fast_forward_from_chain_copy(self):
    ...
```

Suggested helpers:

```python
@contextmanager
def running_ctam_server(self):
    ...

def ctam_request(self, method, path, payload=None):
    ...

def ctam_status(self):
    ...

def replay_headers_to_ctam(self, node, *, commit_interval=10):
    ...

def assert_ctam_matches_tip(self, node):
    ...

def copy_node_datadir(self, source_index, target_path):
    ...
```

Be careful to stop nodes before copying datadirs. If copying a live datadir is unavoidable, use the framework’s existing safe-copy/sync patterns rather than raw partial copies.

## Notes And Edge Cases

- The fast-forward replay starts at height 1. CTAM’s null/null state represents the pre-mining zero checkpoint, not BNG’s genesis metadata.
- Do not replay height 0 unless the protocol later defines that explicitly.
- The replay digest itself does not need to equal the BNG block hash. The digest advances CTAM memory; the commit metadata records which BNG chain prefix that CTAM trunk now represents.
- The test should fail closed if CTAM `/status` is missing `bng_height` or `bng_block_hash` after a commit.
- The test should use exact block hashes from BNG RPC, not locally reconstructed strings.
- Keep the test deterministic and reasonably fast. Use small heights such as 3 for genesis and 23 for fast-forward unless runtime is too high.
- If the CTAM daemon cannot be found, skip with a clear message.
- If `127.0.0.1:8080` is occupied, skip or fail clearly; do not silently reuse the process because stale CTAM state invalidates both scenarios.

## Success Criteria

The final command should be able to run the new functional test directly, for example:

```bash
python3 test/functional/ctam_mining_flows.py
```

or through the standard functional runner once it is added to the suite list.

The test passes only if:

- real BNG mines from CTAM zero/null state,
- real CTAM records the mined BNG checkpoint metadata,
- a non-zero copied BNG chain can be replayed into fresh CTAM state,
- CTAM then permits mining the next block from that copied chain tip.
