
## How to fix broken tests

Periodically or after each test is fixed, update this list with the remaining broken tests. This helps track progress and identify which areas of the codebase are still affected by the changes.

TO check if the tests are passing, run the full test suite with:

```bash
./run-all-tests.sh
```

THis re/builds the test binaries and runs all tests, showing a summary of passed/failed tests at the end. You can also run specific test suites or cases to focus on fixing particular issues.

Apply fixes only one test at a time, and verify that the fix works by re-running the affected test(s) after each change. This helps isolate the impact of each fix and ensures that you are making progress towards a fully passing test suite.

## List of broken tests

### Current run (2026-07-13, CTAM daemon up)

3 tests failed out of 158.

The following tests did not run:
         41 - ctam_rest_client_tests (Skipped)

The following tests FAILED:
 - [ ] 135 - validation_block_tests (SEGFAULT)
 - [ ] 136 - validation_chainstate_tests (Failed)
 - [ ] 137 - validation_chainstatemanager_tests (Failed)

All three share the same root cause: **assumeutxo / UTXO-snapshot activation**.
The chain built by the test fixtures under CTAM mining produces a different tip
hash at the assumeutxo snapshot heights than the hashes hardcoded in the regtest
`m_assumeutxo_data` chainparams. The snapshot is written correctly, but
`ActivateSnapshot` rejects it because its base hash is not registered:

```
[error] ActivateSnapshot failed: assumeutxo block hash in snapshot metadata not
recognized (hash: ade1b170b3dbad502c7d566cd4776d59575ac7ba92ab210162556e43f7451244).
The following snapshot heights are available: 110, 200, 299
test/validation_chainstatemanager_tests.cpp:266: fatal error: ... critical check
CreateAndActivateUTXOSnapshot(this) has failed
```

`validation_block_tests` segfaults rather than asserting, but is in the same
snapshot/chainstate family and is expected to share the cause.

Open question to confirm during analysis: is this a pre-existing branch issue
(stale hardcoded assumeutxo hashes vs. CTAM-mined chain) or newly introduced?
The rewind/`setup_common` reconcile change does not alter block header contents
or the winning nonce in the happy path (reconcile is a no-op when CTAM is
already at the tip), so it is not the expected cause.

### Previous run (CTAM daemon down — environmental, all fixtures failed)

Earlier full-suite failures were `Could not connect to CTAM REST service at
127.0.0.1:8080` in the fixture constructor (`ResetCtamStateForTest()`), i.e. the
CTAM daemon was not running. Not code-related. Now resolved.

Fixed since the prior list:
 - [x]  75 - peerman_tests
 - [x] 129 - txvalidationcache_tests (rewind reconcile in `CreateBlock`)
