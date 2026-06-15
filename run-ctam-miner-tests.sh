#!/bin/bash
set -euo pipefail

# Build binaries in build_ctam_rest_test and run targeted miner tests:
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BNG_REPO_DIR="${BNG_REPO_DIR:-$SCRIPT_DIR}"
BUILD_DIR_NAME="${BUILD_DIR_NAME:-build_ctam_rest_test}"
BUILD_DIR="${BUILD_DIR:-$BNG_REPO_DIR/$BUILD_DIR_NAME}"
BUILD_TARGET="${BUILD_TARGET:-test_bitcoin}"
BUILD_JOBS="${BUILD_JOBS:-4}"
TEST_BINARY="${TEST_BINARY:-$BUILD_DIR/bin/$BUILD_TARGET}"

cmake --build "$BUILD_DIR" --target "$BUILD_TARGET" -j"$BUILD_JOBS"


# Run targeted miner tests:

"$TEST_BINARY" --run_test=miner_tests/CreateNewBlock_validity --catch_system_error=no --log_level=test_suite
echo "Running miner test: CreateNewBlock_validity"
"$TEST_BINARY" --run_test=miner_tests/CreateNewBlock_validity --catch_system_error=no --log_level=test_suite

echo "Running backend test suite: ctam_miner_tests"
"$TEST_BINARY" --run_test=ctam_miner_tests --catch_system_error=no --log_level=test_suite -- -ctam=1

echo "Starting bitcoind for regtest RPC smoke test with CTAM"
"$BUILD_DIR/bin/bitcoind" -regtest -datadir=/tmp/bng-ctam-rpc -fallbackfee=0.0001 -ctammining=1 -ctamresturl=http://127.0.0.1:8080/ -daemon

echo "Creating wallet ctamtest"
"$BUILD_DIR/bin/bitcoin-cli" -regtest -datadir=/tmp/bng-ctam-rpc createwallet ctamtest

ADDRESS=$("$BUILD_DIR/bin/bitcoin-cli" -regtest -datadir=/tmp/bng-ctam-rpc getnewaddress)

echo "Generating 1 block to address $ADDRESS"
"$BUILD_DIR/bin/bitcoin-cli" -regtest -datadir=/tmp/bng-ctam-rpc generatetoaddress 1 "$ADDRESS" 4096

echo "Stopping bitcoind"
"$BUILD_DIR/bin/bitcoin-cli" -regtest -datadir=/tmp/bng-ctam-rpc stop

echo "Finished all tests"
