# GIP-0004 Test Notes

This note captures the first C++ smoke test added for the CTAM REST mining interface work.

## Current C++ Smoke Test

Test file:
- `src/test/bng/ctam_rest_client_tests.cpp`

Boost suite and case:
- suite: `ctam_rest_client_tests`
- case: `csha256_smoke`

Run only this case with a locally running CTAM daemon (ctam is started with `cargo run -- --config config.toml` in the `ctam` directory):

```bash
./build_ctam_rest_test/bin/test_bitcoin --run_test=ctam_rest_client_tests/csha256_smoke --catch_system_error=no --log_level=test_suite -- -ctam=1
```

Optional explicit endpoint and timeout:

```bash
./build_ctam_rest_test/bin/test_bitcoin --run_test=ctam_rest_client_tests/csha256_smoke --catch_system_error=no --log_level=test_suite -- -ctam=1 -ctamresturl=http://127.0.0.1:8080/ -ctamtimeoutms=5000
```

## What The Smoke Test Verifies

The test assumes CTAM is already running locally. It verifies:

1. BNG can reach `GET /health` on the CTAM REST server.
2. The health response contains the expected `test = "ok"` marker.
3. BNG can issue a direct `CTA_SHA256` request through the new `CtamRestClient`.
4. The returned digest is not trivially all zero bytes.
5. The buffered `CCTAMSHA256` wrapper produces the same digest as the direct client call for the same input and difficulty triple.

## Current Test Inputs

These are the current BNG-side wrapper inputs for the smoke test, not fully enforced CTAM server-side inputs yet:

- message: `"ctam csha256 smoke"`
- energy difficulty: `0x1d00ffff`
- context-size difficulty: the live CTAM `context_size` reported by `GET /health`, provided it is at least `27` (~16 MiB)
- injection-round difficulty: `16` rounds

Current implementation status:

- BNG sends `energy_difficulty`, `context_size_difficulty`, and `injection_rounds_difficulty` in the JSON payload.
- The Rust CTAM request DTO now accepts the new energy/context-size/injection-round fields.
- On `POST /crypto/cta_sha256`, only the injection-round difficulty currently affects hashing behavior.
- On `POST /crypto/mine_csha256`, the energy difficulty is also used to drive nonce enumeration against the compact target.
- The context-size difficulty is now checked against CTAM's live `context_size`, so the smoke test derives it from the health response before hashing.
- BNG also enforces a local floor of `context_size = 27` (~16 MiB); if the local CTAM daemon reports a smaller context size, the smoke test skips with an explanatory message.

## Expected Runner Output

When the test is run with `--run_test=ctam_rest_client_tests/csha256_smoke`, Boost prints many lines such as `Test suite "..." is skipped because disabled` for unrelated suites.

That output is expected. It means Boost filtered execution down to the requested suite and disabled the rest.

The important lines are the ones entering and leaving:

- `ctam_rest_client_tests`
- `csha256_smoke`

## What This Test Does Not Cover Yet

- miner integration
- branch lifecycle control from BNG
- CTAM commit semantics for winning versus losing branches
- verifier-side CTAM recomputation
- failure-path assertions for malformed CTAM responses or CTAM downtime

Those belong in later mining and validation tests.

---


## Miner Test Adoption Notes

The upstream `src/test/miner_tests.cpp` test is mostly a block-template and
mempool-selection test, not a client-side mining test. Its useful coverage for
BNG is still the server-side block assembly behavior:

- `createNewBlock()` builds a structurally valid candidate block.
- transaction selection respects package feerates, prioritisation, sigops,
  block min fee, finality, sequence locks, and invalid mempool cases.
- `waitNext()` refreshes templates when the next block becomes meaningfully
  better.
- `checkBlock(..., check_pow=false)` verifies template structure before proof
  work is available.

The part that needs BNG-specific interpretation is the bootstrap loop that
creates 110 spendable blocks. In Bitcoin Core this alternates between direct
`ProcessNewBlock()` and `BlockTemplate::submitSolution()` to model an external
miner filling in a nonce and coinbase. In BNG, CTAM-backed mining is
server-side: BNG assembles the block, asks CTAM to solve the header, CTAM keeps
and commits the winning branch, and BNG accepts the solved block.

For CTAM-enabled runs, the miner test should therefore submit the solved block
through `ProcessNewBlock()` after `CtamMiningBackend::SolveHeader()`. The old
`submitSolution()` path can remain as compatibility/interface coverage for
non-CTAM runs, but it should not be the primary proof that BNG mining works.

Run the focused miner test with CTAM enabled and a local CTAM daemon:

```bash
./build_all_test/bin/test_bitcoin --run_test=miner_tests/CreateNewBlock_validity --catch_system_error=no --log_level=test_suite DEBUG_LOG_OUT -- -ctammining=1 -ctamresturl=http://127.0.0.1:8080/ -ctamtimeoutms=600000 -printtoconsole=1
```

The miner test uses a 10 minute CTAM REST timeout because it starts async CTAM
mining jobs and then polls their status. The timeout applies to each REST
request, not to the whole mining job, and C-SHA256 mining can make individual
poll/control requests slow under load.

Do not use `-ctammining=0` for this BNG miner test. That disables CTAM proof
validation and falls back to legacy hash proof-of-work; BNG headers, including
the genesis-height block connected by the test fixture, are not valid under that
legacy check and fail with `high-hash`.

## Peerman Test Adoption Notes

The upstream `src/test/peerman_tests.cpp` test is a peer-selection policy test,
not a mining test. It verifies when `PeerManager::GetDesirableServiceFlags()`
should prefer full `NODE_NETWORK` peers versus allowing
`NODE_NETWORK_LIMITED` peers based on how close the node is to the current tip.

The test still needs to produce a couple of blocks so validation-interface
callbacks update the peer manager's view of the tip. In upstream Bitcoin Core,
that small helper used legacy `CheckProofOfWork()` nonce grinding. In BNG, those
helper blocks must be mined through the same server-side CTAM path as normal
BNG blocks: BNG assembles the block, CTAM solves and commits the winning branch,
and BNG processes the solved block.

For that reason, `peerman_tests` now uses the shared `MineBlock()` test helper
instead of open-coding proof-of-work. This keeps the peer-manager assertions
unchanged while making the block-producing scaffold valid for CTAM-enabled BNG
runs.

Run the focused peerman test with CTAM enabled and a local CTAM daemon:

```bash
./build_all_test/bin/test_bitcoin --run_test=peerman_tests --catch_system_error=no --log_level=test_suite -- DEBUG_LOG_OUT -ctammining=1 -ctamresturl=http://127.0.0.1:8080/ -ctamtimeoutms=600000 -printtoconsole=1
```

### Verification

Verified 2026-07-03 against a live local CTAM daemon on `127.0.0.1:8080`:

- The focused invocation above passes (`*** No errors detected`), mining two
  blocks through real CTAM jobs and connecting them before the peer-manager
  assertions run.
- The plain CTest registration for this suite (`ctest --test-dir
  build_all_test -R '^peerman_tests$'`), which passes no explicit `-ctam*`
  flags and therefore relies on the `-ctammining=true` / `-ctamresturl=
  http://127.0.0.1:8080/` defaults, also passes (`Test #75: peerman_tests
  ... Passed`).
- Like `miner_tests`, this suite requires a live CTAM daemon reachable at the
  configured (or default) REST URL to pass; without one, `SolveHeader()` fails
  and `MineBlock()` returns a null `COutPoint`, aborting the test via
  `Assert(!MineBlock(node, block).IsNull())`. That is expected/by-design for
  this fork, not a defect in the adoption fix.
- `broken_tests.md` still lists `peerman_tests` under failing tests as of this
  writing; that entry is stale relative to the above and should be cleared the
  next time that tracking file is updated.

## CTAM Trunk-State Orchestration In Tests (Rewind / Fast-Forward / Reset+Replay)

CTAM proof-of-work is state-sensitive: solving and verifying a header depends on
CTAM's trunk being at the block's parent. Test suites, unlike production block
flow, freely build side branches, process blocks that consensus then rejects,
and invalidate/reconsider tips. That leaves CTAM's trunk out of sync with the
chain state the next block must be mined against, so the test scaffolding now
reconciles trunk state explicitly before every solve.

Client support (`src/crypto/ctam_client.{h,cpp}`):

- `CtamRestClient::Rewind(tip_height, tip_block_hash, back_to_height,
  back_to_block_hash, num_of_commits)` drives CTAM's rewind endpoint and
  returns a `RewindResult` (old/new tip, applied entries, remaining rewind
  depth). Rewind is bounded by CTAM's retained committed-branch history, so
  callers must treat it as best-effort and keep a fallback.

Linear reconciliation (`TestChain100Setup::SyncCtamToActiveChain()` in
`src/test/util/setup_common.cpp`), used by `CreateBlock()` before each
`SolveHeader()`:

1. No-op fast path when CTAM's trunk already matches the BNG active tip (the
   common case in the happy path).
2. Rewind fast path when the trunk is *ahead* of the active tip — e.g. a block
   was committed to CTAM but then rejected by BNG consensus, as in the
   double-spend cases of `txvalidationcache_tests`. The trunk is rewound by
   `trunk_height - tip_height` commits and verified against the tip hash.
3. Fallback: `AdminReset` followed by a full replay of the active chain prefix
   (re-posting each header's work bytes with `keep_branch=true` and committing
   the branch at its height). This always works but is O(height).

Branch-aware reconciliation (`MinerTestingSetup::SyncCtamToHeaderChain()` in
`src/test/validation_block_tests.cpp`), needed because that suite mines on
arbitrary branches of a randomly generated block tree:

- Finds the last common ancestor of CTAM's current trunk block and the target
  parent, rewinds back to it, then replays the target branch's headers forward
  (fast-forward). Headers are reconstructed losslessly from the block index.
- A rewind all the way past the first commit is a genesis-boundary 409 by
  contract, so genesis targets are matched against the freshly-reset trunk
  state (trunk_key 0, no checkpoint metadata) instead.
- Any rewind/replay failure falls back to reset + full replay from genesis.

Related validation-side change (`src/node/blockstorage.cpp`,
`LoadBlockIndexGuts()`): headers reloaded from the block-index DB skip the
stateless `CheckProofOfWork()` nBits check when CTAM mining is enabled, since
CTAM proof cannot be re-derived from the header hash alone and those headers
were fully validated before being written. This mirrors the equivalent skip in
`CheckBlockHeader()` and unblocked the snapshot/chainstate suites
(`validation_chainstate_tests`, `validation_chainstatemanager_tests`,
`validation_block_tests`).

## Test Logging And Difficulty Tuning (2026-07-14)

CTAM mining used to emit three unconditional `LogPrintf` lines per solved block
(job start, job id, job completion). Fixture-heavy suites mine 100+ blocks per
test case, so failure output (and `-printtoconsole=1` debugging sessions) was
dominated by this noise.

- A dedicated `ctam` debug-log category was added (`BCLog::CTAM` in
  `src/logging.h` / `src/logging.cpp`).
- The per-block mining lifecycle logs in `src/node/ctam_miner.cpp` were demoted
  to `LogDebug(BCLog::CTAM, ...)`. They are silent by default; re-enable them
  when debugging with `-debug=ctam` (add `-printtoconsole=1` for unit-test
  runs). Warnings and errors (poll timeouts, rewind fallbacks, PoW validation
  failures) are unchanged and still print unconditionally.

Miner-test energy difficulty was reduced again. `miner_tests` runs on main-net
params, whose inherited target made the 110-block bootstrap loop the slowest
part of the whole suite (~560 s). The `-ctamtestdiffbits` override constant
`CTAM_MINER_TEST_ENERGY_BITS` in `src/test/miner_tests.cpp` went from
`0x1f3fffc0` (1-in-1024 digests pass, "64x easier than genesis") to
`0x207fffff` (1-in-2 digests pass — the same target as the regtest genesis and
the practical floor of the compact encoding). The CTAM digest/target comparison
(`DigestMeetsTarget`) is not clamped by `powLimit`, and
`ContextualCheckBlockHeader()` accepts the override via `-ctamtestdiffbits`,
so no consensus-code change is involved. Regtest fixtures already mine at
`0x207fffff` via `GetNextWorkRequired`, so only `miner_tests` needed this.

## Full-Suite Status (2026-07-14, live CTAM daemon)

`./run-all-tests.sh` against `38720f51d6` plus the logging/difficulty changes
above: **158/158 passed, 0 failed** (`ctam_rest_client_tests` skipped as
usual — it only runs with `-ctam=1`). The three snapshot/chainstate suites
previously tracked as broken all pass; `broken_tests.md` was updated to match.
Suites that mine through CTAM remain dependent on a live daemon at the default
`127.0.0.1:8080` REST URL and fail in the fixture constructor without one.

## Persistent HTTP Transport And Adaptive Mining Poll (2026-07-14)

Two REST-interaction speedups landed for the CTAM-mining test suites; both
keep CTAM doing all server-side mining (no test shortcuts around the daemon).

### 1. Persistent HTTP/1.1 transport (implements `persistent-http-transport.md`)

`CtamRestClient` now owns a reusable transport session (libevent event base +
`evhttp_connection`) instead of creating a connection and sending
`Connection: close` for every request:

- The session is created lazily on the first request and reused across
  status, mining start, polls, replay, and commit for the lifetime of the
  client instance. The mining backend and the fixture's
  `SyncCtamToActiveChain()` already use one client per logical operation, so
  each block now costs 1-2 TCP connections instead of ~8.
- The transport is discarded and transparently recreated after connect
  failures, timeouts, EOF, framing errors, or malformed (non-JSON) response
  bodies — any state where connection reuse would be unsafe.
- If a request written to a reused connection fails (a server may drop an
  idle keep-alive connection at any time), the client reconnects and retries
  exactly once — but only for idempotent GETs and never after a timeout.
  State-changing POSTs keep their previous failure behaviour and are never
  automatically duplicated.
- Requests are serialized on a per-client mutex; one HTTP/1.1 connection must
  not carry interleaved requests from concurrent callers.
- Connection lifecycle events log under `-debug=ctam`.

Files: `src/crypto/ctam_client.{h,cpp}` (transport hidden behind a private
`Transport` impl struct so libevent types stay out of the public header).

### 2. Adaptive mining-job poll interval

`CtamMiningBackend::SolveHeader()` used to sleep a fixed 50 ms before the
*first* `GET /mining/{job_id}` poll. Loopback measurements against the live
daemon showed an easy-difficulty job (~2 attempts) reaches a terminal state in
~15 ms, `GET /status` costs ~2.5 ms, and a `POST /crypto/cta_sha256` replay
~13 ms — so the fixed sleep, not TCP setup, dominated per-block fixture cost.
Polling now starts at 1 ms and backs off exponentially to the unchanged 50 ms
cap (`CTAM_MINING_INITIAL_POLL_INTERVAL` in `src/node/ctam_miner.cpp`), so
fast test jobs are picked up almost immediately while long production jobs
poll at the same 50 ms rate as before.

### Measured suite timings (live CTAM daemon, same machine, same difficulty)

| Suite | Before | After | Notes |
|---|---|---|---|
| miner_tests | 561 s | 6.0 s | bulk of the drop from the `0x207fffff` energy-bits change recorded above; transport + poll changes remove most of the rest |
| miniminer_tests | 42 s | 20.3 s | transport alone: 40.4 s; adaptive poll took it to 20.3 s |
| validation_block_tests | 87 s | 42.6 s | |
| validation_chainstatemanager_tests | 102 s | 51.0 s | |
| interfaces_tests | 51 s | 24.6 s | |

Takeaway: persistent connections alone bought little on loopback (~1.5 s of
miniminer's 42 s) because local TCP setup is cheap; the first-poll sleep was
the dominant fixed overhead. The remaining ~40 ms per fixture block is genuine
CTAM server work (mining job ~15 ms + verification replay ~13 ms + status and
commit round-trips), so further test speedups would need fewer REST operations
per block, not a faster transport. Persistent transport still removes
per-request socket churn and matters more for remote/production deployments.

Verification: `ctam_rest_client_tests` passes with `-- -ctam=1` (5 cases,
covering the job lifecycle over one reused connection), and the suites above
all pass individually against the live daemon.

## Full-Suite History And Triage Notes (2026-07-14, absorbed from `broken_tests.md`)

The repo-root `broken_tests.md` tracker is being retired; the durable insights
from it live here now.

### Latest full-suite run

`./run-all-tests.sh` with the persistent HTTP transport and adaptive poll
changes (on top of `38720f51d6`): **158/158 passed, 0 failed**, total test
time 376 s. `ctam_rest_client_tests` is skipped in suite runs (it only
executes with `-- -ctam=1`) but passes when run manually against the live
daemon. Full-suite timings for the previously slow CTAM suites:
validation_block_tests 52.8 s, validation_chainstatemanager_tests 50.8 s,
interfaces_tests 24.5 s, miniminer_tests 20.0 s, miner_tests 6.0 s (slightly
above their standalone timings due to suite-level contention on the shared
CTAM daemon).

### Triage workflow that worked

- `./run-all-tests.sh` rebuilds the test binaries and runs everything with a
  pass/fail summary; use it to refresh the overall state.
- Fix one test at a time and re-run the affected suite(s) after each change
  to isolate the impact before moving on.

### Root causes fixed along the way (for future debugging reference)

- **assumeutxo/snapshot family** (validation_block_tests SEGFAULT,
  validation_chainstate_tests, validation_chainstatemanager_tests):
  `ActivateSnapshot` rejected the fixture-built chain's snapshot base hash
  ("assumeutxo block hash in snapshot metadata not recognized"). Resolved by
  the branch-aware CTAM trunk reconcile in `validation_block_tests.cpp` plus
  the CTAM-aware `LoadBlockIndexGuts()` PoW skip in `blockstorage.cpp`
  (commits `bb350ab2b8` + `38720f51d6`).
- **txvalidationcache_tests**: fixed by the rewind reconcile in the fixture's
  `CreateBlock` — a previously committed CTAM branch for a block that BNG
  consensus later rejected left CTAM's trunk ahead of the active tip;
  `SyncCtamToActiveChain()` rewinds (or reset-and-replays) before solving.
- **peerman_tests**: fixed in the same fixture-reconcile family.

### Known environmental failure signature

Every CTAM-mining suite needs a live CTAM daemon at the default
`127.0.0.1:8080`. Without one, fixtures fail in the constructor
(`ResetCtamStateForTest()`) with `Could not connect to CTAM REST service at
127.0.0.1:8080`. A whole-suite wipeout with that message is environmental,
not a code regression — start the daemon and re-run.
