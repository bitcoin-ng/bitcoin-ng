# pow_tests fix (GIP-0001)

This note records the changes made to get `pow_tests` passing under BNG after GIP-0001’s chain parameter updates.

**Date**: 2026-02-18

## What broke

`pow_tests` contained assumptions that are valid for Bitcoin mainnet/testnet, but not necessarily for BNG:

- The `get_next_work_pow_limit` case hardcoded Bitcoin’s historical pow-limit `nBits` (`0x1d00ffff`).
- The `ChainParams_*_sanity` checks asserted an implicit precondition from upstream: PoW retarget arithmetic must not overflow 256-bit intermediate values.

BNG’s genesis uses an easier `nBits` (`0x1f00ffff`), which requires a much larger `consensus.powLimit`. With such a large `powLimit`, the legacy retarget step:

- `bnNew *= nActualTimespan; bnNew /= nPowTargetTimespan;`

can overflow when computed in a 256-bit integer, even though the final result is clamped back to `powLimit`.

## What changed

- `src/pow.cpp`
  - Retarget arithmetic is made overflow-safe by using wider integer math (via `boost::multiprecision::cpp_int`) when the target is large.
  - The old 256-bit arithmetic path is preserved for smaller targets.

- `src/test/pow_tests.cpp`
  - `get_next_work_pow_limit` now derives the expected pow-limit compact value from `chainParams->GetConsensus().powLimit` instead of hardcoding a Bitcoin-specific constant.
  - The “powLimit * 4*nPowTargetTimespan must not overflow” sanity check is removed, because overflow is now prevented by the production code.

## Why this is correct

- Consensus behavior remains: the next difficulty target is computed per the same rules and then clamped to `powLimit`.
- The fix only changes how intermediate values are computed, preventing wraparound for large `powLimit` values while keeping results identical for typical Bitcoin-like limits.
