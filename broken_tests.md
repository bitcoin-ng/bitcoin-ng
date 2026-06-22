
## How to fix broken tests

Periodically or after each test is fixed, update this list with the remaining broken tests. This helps track progress and identify which areas of the codebase are still affected by the changes.

TO check if the tests are passing, run the full test suite with:

```bash
./run-all-tests.sh
```

THis re/builds the test binaries and runs all tests, showing a summary of passed/failed tests at the end. You can also run specific test suites or cases to focus on fixing particular issues.

Apply fixes only one test at a time, and verify that the fix works by re-running the affected test(s) after each change. This helps isolate the impact of each fix and ensures that you are making progress towards a fully passing test suite.

## List of broken tests
78% tests passed, 34 tests failed out of 158

Total Test time (real) =  50.22 sec

The following tests did not run:
         41 - ctam_rest_client_tests (Skipped)

The following tests FAILED:
 - [ ]  55 - interfaces_tests (Failed)
 - [ ]  59 - mempool_tests (Failed)
 - [ ]  60 - merkle_tests (Failed)
 - [ ]  62 - miner_tests (Failed)
 - [ ]  63 - miniminer_tests (Failed)
 - [ ]  67 - net_peer_connection_tests (Failed)
 - [ ]  69 - net_tests (Failed)
 - [ ]  71 - node_init_tests (Failed)
 - [ ]  73 - orphanage_tests (Failed)
 - [ ]  75 - peerman_tests (Failed)
 - [ ]  81 - prevector_tests (Failed)
 - [ ]  84 - rbf_tests (Failed)
 - [ ]  88 - bng_regtest_init_tests (Failed)
 - [ ]  95 - rpc_tests (Failed)
 - [ ] 117 - testnet4_miner_tests (Failed)
 - [ ] 122 - txdownload_tests (Failed)
 - [ ] 124 - txindex_tests (Failed)
 - [ ] 125 - txpackage_tests (Failed)
 - [ ] 128 - txvalidation_tests (Failed)
 - [ ] 129 - txvalidationcache_tests (Failed)
 - [ ] 135 - validation_block_tests (Failed)
 - [ ] 136 - validation_chainstate_tests (Failed)
 - [ ] 137 - validation_chainstatemanager_tests (Failed)
 - [ ] 138 - validation_flush_tests (Failed)
 - [ ] 139 - validation_tests (Failed)
 - [ ] 143 - coinselector_tests (Failed)
 - [ ] 144 - coinselection_tests (Failed)
 - [ ] 145 - feebumper_tests (Failed)
 - [ ] 146 - group_outputs_tests (Failed)
 - [ ] 149 - psbt_wallet_tests (Failed)
 - [ ] 151 - spend_tests (Failed)
 - [ ] 154 - wallet_tests (Failed)
 - [ ] 155 - wallet_transaction_tests (Failed)
 - [ ] 157 - walletload_tests (Failed)
