
## How to fix broken tests

Periodically or after each test is fixed, update this list with the remaining broken tests. This helps track progress and identify which areas of the codebase are still affected by the changes.

TO check if the tests are passing, run the full test suite with:

```bash
./run-all-tests.sh
```

THis re/builds the test binaries and runs all tests, showing a summary of passed/failed tests at the end. You can also run specific test suites or cases to focus on fixing particular issues.

Apply fixes only one test at a time, and verify that the fix works by re-running the affected test(s) after each change. This helps isolate the impact of each fix and ensures that you are making progress towards a fully passing test suite.

## List of broken tests
96% tests passed, 6 tests failed out of 158

The following tests did not run:
         41 - ctam_rest_client_tests (Skipped)

The following tests FAILED:
 - [ ]  62 - miner_tests (Failed)
 - [ ]  75 - peerman_tests (Subprocess aborted)
 - [ ] 129 - txvalidationcache_tests (Failed)
 - [ ] 135 - validation_block_tests (SEGFAULT)
 - [ ] 136 - validation_chainstate_tests (Failed)
 - [ ] 137 - validation_chainstatemanager_tests (Failed)
