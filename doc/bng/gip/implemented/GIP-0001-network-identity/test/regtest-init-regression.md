# Regression: TestChain100Setup regtest bootstrap

This note documents why `TestChain100Setup` is the fixture that tends to “break first” in BNG forks, and adds a dedicated regression test to make failures actionable.

## What is specific about `TestChain100Setup`?

Unlike `BasicTestingSetup` (and many lightweight fixtures), `TestChain100Setup` performs **full chainstate initialization** for regtest, including:

- Selecting `regtest` chain params.
- Loading/creating the chainstate databases.
- Loading/creating the genesis block entry and validating it as part of initialization.
- Producing a 100-block chain (or at least setting up the machinery to do so).

This means it hits consensus/header policy and init-time assertions that other tests never touch.

## Typical failure mode in BNG: genesis header time too new

If BNG’s regtest genesis header has `genesis.nTime` **in the future relative to wall-clock time** (beyond the allowed future drift, commonly ~2 hours), chainstate init can fail with a message equivalent to:

- “block timestamp too far in the future”

This is easy to miss because:
- It depends on *when* you run the tests (works after the genesis date; fails before it).
- Many unit tests never initialize chainstate, so they remain green.

## Added regression test

A dedicated smoke test was added:

- `src/test/bng/regtest_init_tests.cpp`
- - Boost suite: `bng_regtest_init_tests/testchain100setup_regtest_bootstrap_smoke`
- - CTest name: `bng_regtest_init_tests` (runs `test_bitcoin --run_test=bng_regtest_init_tests/*`)

Design notes:
- The test treats `src/kernel/chainparams.cpp` as a **black box** (via `SelectParams(...)` + `Params()`).
- It asserts the expected BNG regtest identity (magic bytes, port, bech32 HRP).
- It checks that `genesis.nTime` is not too far in the future.
- It then constructs `TestChain100Setup(ChainType::REGTEST)` inside `try/catch` so failures point at the real init surface.

## If this trips

Recommended fixes (choose one; document the decision in GIP-0001):

- Set BNG genesis header `nTime` to a timestamp safely in the past (recommended for reproducible CI).
- Avoid future-dated genesis times for any network that unit tests initialize (especially regtest).
- Do **not** “fix” this by weakening header time checks in validation code (would change consensus/policy assumptions).
