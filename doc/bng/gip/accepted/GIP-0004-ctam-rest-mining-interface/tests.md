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
