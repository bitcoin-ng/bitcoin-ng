# Test Fix Summary (make check)

This document tracks components invoked by `make check` and their status. As of 2026-01-13, all checks pass with no errors.

## How to run

- Unit tests: `make -C src check`
- Individual unit test binary: `src/test/test_bitcoin [--run_test=...]`
- Util CLI golden tests: `python3 test/util/test_runner.py` (runs `bitcoin-tx` / `bitcoin-util` golden files)

## Root-cause buckets (fork identity)

- **Bech32/Bech32m HRP rebrand**: `bc1.../tb1.../bcrt1...` → `bng1.../tbng1.../bngr1...`
- **Base58 version rebrand**: mainnet P2PKH no longer `1...` (version `0x00`); fork uses version `0x1a` (addresses like `B...`).
- **Default port rebrand**: don’t assume `8333`; use `Params().GetDefaultPort()` in tests.
- **Descriptor checksum changes**: `addr(...)#...` checksum changes when address string changes.

## Status Summary

- All C++ and wallet unit tests pass (126/126).
    - Report: `doc/bng/tests/logs/test_bitcoin.suites.json`
    - Logs: `doc/bng/tests/logs/test_bitcoin.<suite>.log`
- Util golden tests (`test/util/test_runner.py`) pass after regenerating expected outputs.

## Test components invoked by `make check`

### C++ unit tests (src/test/test_bitcoin)

These are compiled from `src/test/*.cpp`.

- [x] addrman_tests (`src/test/addrman_tests.cpp`)
- [x] allocator_tests (`src/test/allocator_tests.cpp`)
- [x] amount_tests (`src/test/amount_tests.cpp`)
- [x] argsman_tests (`src/test/argsman_tests.cpp`)
- [x] arith_uint256_tests (`src/test/arith_uint256_tests.cpp`)
- [x] banman_tests (`src/test/banman_tests.cpp`)
- [x] base32_tests (`src/test/base32_tests.cpp`)
- [x] base58_tests (`src/test/base58_tests.cpp`)
- [x] base64_tests (`src/test/base64_tests.cpp`)
- [x] bech32_tests (`src/test/bech32_tests.cpp`)
- [x] bip32_tests (`src/test/bip32_tests.cpp`)
- [x] bip324_tests (`src/test/bip324_tests.cpp`)
- [x] blockchain_tests (`src/test/blockchain_tests.cpp`)
- [x] blockencodings_tests (`src/test/blockencodings_tests.cpp`)
- [x] blockfilter_index_tests (`src/test/blockfilter_index_tests.cpp`)
- [x] blockfilter_tests (`src/test/blockfilter_tests.cpp`)
- [x] blockmanager_tests (`src/test/blockmanager_tests.cpp`)
- [x] bloom_tests (`src/test/bloom_tests.cpp`)
- [x] bswap_tests (`src/test/bswap_tests.cpp`)
- [x] checkqueue_tests (`src/test/checkqueue_tests.cpp`)
- [x] cluster_linearize_tests (`src/test/cluster_linearize_tests.cpp`)
- [x] coins_tests (`src/test/coins_tests.cpp`)
- [x] coinscachepair_tests (`src/test/coinscachepair_tests.cpp`)
- [x] coinstatsindex_tests (`src/test/coinstatsindex_tests.cpp`)
- [x] common_url_tests (`src/test/common_url_tests.cpp`)
- [x] compilerbug_tests (`src/test/compilerbug_tests.cpp`)
- [x] compress_tests (`src/test/compress_tests.cpp`)
- [x] crypto_tests (`src/test/crypto_tests.cpp`)
- [x] cuckoocache_tests (`src/test/cuckoocache_tests.cpp`)
- [x] dbwrapper_tests (`src/test/dbwrapper_tests.cpp`)
- [x] denialofservice_tests (`src/test/denialofservice_tests.cpp`)
- [x] descriptor_tests (`src/test/descriptor_tests.cpp`)
- [x] disconnected_transactions (`src/test/disconnected_transactions.cpp`)
- [x] feefrac_tests (`src/test/feefrac_tests.cpp`)
- [x] flatfile_tests (`src/test/flatfile_tests.cpp`)
- [x] fs_tests (`src/test/fs_tests.cpp`)
- [x] getarg_tests (`src/test/getarg_tests.cpp`)
- [x] hash_tests (`src/test/hash_tests.cpp`)
- [x] headers_sync_chainwork_tests (`src/test/headers_sync_chainwork_tests.cpp`)
- [x] httpserver_tests (`src/test/httpserver_tests.cpp`)
- [x] i2p_tests (`src/test/i2p_tests.cpp`)
- [x] interfaces_tests (`src/test/interfaces_tests.cpp`)
- [x] ipc_tests (`src/test/ipc_tests.cpp`)
- [x] key_io_tests (`src/test/key_io_tests.cpp`) — FIXED via updated vectors in `src/test/data/key_io_valid.json`.
- [x] key_tests (`src/test/key_tests.cpp`) — FIXED by updating hardcoded P2PKH addresses.
- [x] logging_tests (`src/test/logging_tests.cpp`)
- [x] mempool_tests (`src/test/mempool_tests.cpp`)
- [x] merkle_tests (`src/test/merkle_tests.cpp`)
- [x] merkleblock_tests (`src/test/merkleblock_tests.cpp`)
- [x] miner_tests (`src/test/miner_tests.cpp`)
- [x] miniminer_tests (`src/test/miniminer_tests.cpp`)
- [x] miniscript_tests (`src/test/miniscript_tests.cpp`)
- [x] minisketch_tests (`src/test/minisketch_tests.cpp`)
- [x] multisig_tests (`src/test/multisig_tests.cpp`)
- [x] net_peer_connection_tests (`src/test/net_peer_connection_tests.cpp`) — FIXED by removing hardcoded `8333` expectations.
- [x] net_peer_eviction_tests (`src/test/net_peer_eviction_tests.cpp`)
- [x] net_tests (`src/test/net_tests.cpp`)
- [x] netbase_tests (`src/test/netbase_tests.cpp`)
- [x] node_warnings_tests (`src/test/node_warnings_tests.cpp`)
- [x] orphanage_tests (`src/test/orphanage_tests.cpp`)
- [x] peerman_tests (`src/test/peerman_tests.cpp`)
- [x] pmt_tests (`src/test/pmt_tests.cpp`)
- [x] policy_fee_tests (`src/test/policy_fee_tests.cpp`)
- [x] policyestimator_tests (`src/test/policyestimator_tests.cpp`)
- [x] pool_tests (`src/test/pool_tests.cpp`)
- [x] pow_tests (`src/test/pow_tests.cpp`)
- [x] prevector_tests (`src/test/prevector_tests.cpp`)
- [x] raii_event_tests (`src/test/raii_event_tests.cpp`)
- [x] random_tests (`src/test/random_tests.cpp`)
- [x] rbf_tests (`src/test/rbf_tests.cpp`)
- [x] rest_tests (`src/test/rest_tests.cpp`)
- [x] result_tests (`src/test/result_tests.cpp`)
- [x] reverselock_tests (`src/test/reverselock_tests.cpp`)
- [x] rpc_tests (`src/test/rpc_tests.cpp`)
- [x] sanity_tests (`src/test/sanity_tests.cpp`)
- [x] scheduler_tests (`src/test/scheduler_tests.cpp`)
- [x] script_p2sh_tests (`src/test/script_p2sh_tests.cpp`)
- [x] script_parse_tests (`src/test/script_parse_tests.cpp`)
- [x] script_segwit_tests (`src/test/script_segwit_tests.cpp`)
- [x] script_standard_tests (`src/test/script_standard_tests.cpp`) — FIXED taproot address + updated `src/test/data/bip341_wallet_vectors.json`.
- [x] script_tests (`src/test/script_tests.cpp`)
- [x] scriptnum_tests (`src/test/scriptnum_tests.cpp`)
- [x] serfloat_tests (`src/test/serfloat_tests.cpp`)
- [x] serialize_tests (`src/test/serialize_tests.cpp`)
- [x] settings_tests (`src/test/settings_tests.cpp`)
- [x] sighash_tests (`src/test/sighash_tests.cpp`)
- [x] sigopcount_tests (`src/test/sigopcount_tests.cpp`)
- [x] skiplist_tests (`src/test/skiplist_tests.cpp`)
- [x] sock_tests (`src/test/sock_tests.cpp`)
- [x] span_tests (`src/test/span_tests.cpp`)
- [x] streams_tests (`src/test/streams_tests.cpp`)
- [x] sync_tests (`src/test/sync_tests.cpp`)
- [x] system_tests (`src/test/system_tests.cpp`)
- [x] timeoffsets_tests (`src/test/timeoffsets_tests.cpp`)
- [x] torcontrol_tests (`src/test/torcontrol_tests.cpp`)
- [x] transaction_tests (`src/test/transaction_tests.cpp`)
- [x] translation_tests (`src/test/translation_tests.cpp`)
- [x] txindex_tests (`src/test/txindex_tests.cpp`)
- [x] txpackage_tests (`src/test/txpackage_tests.cpp`)
- [x] txreconciliation_tests (`src/test/txreconciliation_tests.cpp`)
- [x] txrequest_tests (`src/test/txrequest_tests.cpp`)
- [x] txvalidation_tests (`src/test/txvalidation_tests.cpp`)
- [x] txvalidationcache_tests (`src/test/txvalidationcache_tests.cpp`)
- [x] uint256_tests (`src/test/uint256_tests.cpp`)
- [x] util_tests (`src/test/util_tests.cpp`) — FIXED message_verify address constants.
- [x] util_threadnames_tests (`src/test/util_threadnames_tests.cpp`)
- [x] validation_block_tests (`src/test/validation_block_tests.cpp`)
- [x] validation_chainstate_tests (`src/test/validation_chainstate_tests.cpp`)
- [x] validation_chainstatemanager_tests (`src/test/validation_chainstatemanager_tests.cpp`)
- [x] validation_flush_tests (`src/test/validation_flush_tests.cpp`)
- [x] validation_tests (`src/test/validation_tests.cpp`)
- [x] validationinterface_tests (`src/test/validationinterface_tests.cpp`)
- [x] versionbits_tests (`src/test/versionbits_tests.cpp`)

### Wallet unit tests (wallet/test/*)

- [x] coinselector_tests (`src/wallet/test/coinselector_tests.cpp`)
- [x] feebumper_tests (`src/wallet/test/feebumper_tests.cpp`)
- [x] group_outputs_tests (`src/wallet/test/group_outputs_tests.cpp`)
- [x] init_tests (`src/wallet/test/init_tests.cpp`)
- [x] ismine_tests (`src/wallet/test/ismine_tests.cpp`)
- [x] psbt_wallet_tests (`src/wallet/test/psbt_wallet_tests.cpp`)
- [x] rpc_util_tests (`src/wallet/test/rpc_util_tests.cpp`)
- [x] scriptpubkeyman_tests (`src/wallet/test/scriptpubkeyman_tests.cpp`)
- [x] spend_tests (`src/wallet/test/spend_tests.cpp`)
- [x] wallet_crypto_tests (`src/wallet/test/wallet_crypto_tests.cpp`)
- [x] wallet_tests (`src/wallet/test/wallet_tests.cpp`)
- [x] wallet_transaction_tests (`src/wallet/test/wallet_transaction_tests.cpp`)
- [x] walletdb_tests (`src/wallet/test/walletdb_tests.cpp`)
- [x] walletload_tests (`src/wallet/test/walletload_tests.cpp`)

### Util golden tests (test/util/test_runner.py)

- [x] `python3 test/util/test_runner.py` (drives `bitcoin-tx` and `bitcoin-util`, compares against files in `test/util/data/`)
    - Goldens regenerated with fork address/HRP updates via `python3 test/util/regenerate_golden_outputs.py`.

### Univalue tests

- [x] univalue/test/object (`src/univalue/test/object`)
- [x] univalue/test/unitester (`src/univalue/test/unitester`) — FIXED by setting `JSON_TEST_SRC=./src/univalue/test` in the test environment.

### Minisketch tests

- [x] minisketch/test (`src/minisketch/test`)

## Current Breakages

None. All `make check` components pass.


---
## Notes

✔ Ran the main unit-test binary suite-by-suite (126/126) using `run_test_bitcoin_suites.py`; report/logs are under logs. @done(26-01-13 15:21)
✔ Regenerated util goldens and updated fork address/HRP expectations. @done(26-01-13 16:05)
✔ Fixed Univalue `unitester` by pointing fixture path via `JSON_TEST_SRC`. @done(26-01-13 16:12)

---

`make check` is green (2026-01-13)