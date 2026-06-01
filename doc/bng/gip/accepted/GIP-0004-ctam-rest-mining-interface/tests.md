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
- energy difficulty bits: `0x1d00ffff`
- major CTAM difficulty: the live CTAM `context_size` reported by `GET /health`, provided it is at least `27` (~16 MiB)
- minor CTAM difficulty: `16` rounds

Current implementation status:

- BNG sends `energy_difficulty_bits`, `major_ctam_difficulty_context_size`, and `minor_ctam_difficulty_rounds` in the JSON payload.
- BNG also maps `minor_ctam_difficulty_rounds` onto CTAM's existing `injections` field.
- The Rust CTAM request DTO now accepts the new energy/major/minor fields.
- On `POST /crypto/cta_sha256`, only the minor CTAM difficulty currently affects hashing behavior; it is resolved through the existing injections path.
- On `POST /crypto/mine_csha256`, the energy difficulty is also used to drive nonce enumeration against the compact target.
- The major CTAM difficulty is now checked against CTAM's live `context_size`, so the smoke test derives it from the health response before hashing.
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