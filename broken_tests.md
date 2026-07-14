
## How to fix broken tests

Periodically or after each test is fixed, update this list with the remaining broken tests. This helps track progress and identify which areas of the codebase are still affected by the changes.

TO check if the tests are passing, run the full test suite with:

```bash
./run-all-tests.sh
```

THis re/builds the test binaries and runs all tests, showing a summary of passed/failed tests at the end. You can also run specific test suites or cases to focus on fixing particular issues.

Apply fixes only one test at a time, and verify that the fix works by re-running the affected test(s) after each change. This helps isolate the impact of each fix and ensures that you are making progress towards a fully passing test suite.

## List of broken tests

### Current run (2026-07-14, CTAM daemon up, at 38720f51d6)

**0 tests failed out of 158 — suite is clean.**

The following tests did not run:
         41 - ctam_rest_client_tests (Skipped — only runs with `-ctam=1`)

Fixed since the prior list (all three shared the assumeutxo/snapshot family
root cause; resolved by the branch-aware CTAM trunk reconcile in
`validation_block_tests.cpp` and the CTAM-aware `LoadBlockIndexGuts()` PoW
skip in `blockstorage.cpp`, commits bb350ab2b8 + 38720f51d6):
 - [x] 135 - validation_block_tests (was SEGFAULT)
 - [x] 136 - validation_chainstate_tests
 - [x] 137 - validation_chainstatemanager_tests

Notable timings from this run (candidates for speedups, not failures):
 - miner_tests: 561 s at the old `CTAM_MINER_TEST_ENERGY_BITS{0x1f3fffc0}`;
   now reduced to `0x207fffff` (~2 CTAM attempts per block) — expect a large
   drop on the next run.
 - validation_chainstatemanager_tests: 102 s, validation_block_tests: 87 s,
   interfaces_tests: 51 s, miniminer_tests: 42 s (fixture mining REST
   round-trips dominate).

All CTAM-mining suites require a live CTAM daemon at 127.0.0.1:8080; without
one the fixture constructor fails (see previous-run section below).

### Prior run (2026-07-13, CTAM daemon up)

3 tests failed out of 158: validation_block_tests (SEGFAULT),
validation_chainstate_tests, validation_chainstatemanager_tests. Root cause
was the assumeutxo/UTXO-snapshot activation family: `ActivateSnapshot`
rejected the fixture-built chain's snapshot base hash
(`assumeutxo block hash in snapshot metadata not recognized`), and
validation_block_tests segfaulted in the same snapshot/chainstate family.
Resolved as noted above.

### Previous run (CTAM daemon down — environmental, all fixtures failed)

Earlier full-suite failures were `Could not connect to CTAM REST service at
127.0.0.1:8080` in the fixture constructor (`ResetCtamStateForTest()`), i.e. the
CTAM daemon was not running. Not code-related. Now resolved.

Fixed since the prior list:
 - [x]  75 - peerman_tests
 - [x] 129 - txvalidationcache_tests (rewind reconcile in `CreateBlock`)
