#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BNG_REPO_DIR="${BNG_REPO_DIR:-$SCRIPT_DIR}"
BUILD_DIR_NAME="${BUILD_DIR_NAME:-build_ctam_rest_test}"
BUILD_DIR="${BUILD_DIR:-$BNG_REPO_DIR/$BUILD_DIR_NAME}"
BUILD_TARGET="${BUILD_TARGET:-test_bitcoin}"
BUILD_JOBS="${BUILD_JOBS:-4}"
TEST_BINARY="${TEST_BINARY:-$BUILD_DIR/bin/$BUILD_TARGET}"

MOCK_TEST_SUITES=(
	ctam_miner_tests
)

LIVE_CTAM_TEST_SUITES=(
	ctam_rest_client_tests
)

cmake --build "$BUILD_DIR" --target "$BUILD_TARGET" -j"$BUILD_JOBS"

for test_suite in "${MOCK_TEST_SUITES[@]}"; do
	"$TEST_BINARY" --run_test="${test_suite}" --catch_system_error=no --log_level=test_suite
done

for test_suite in "${LIVE_CTAM_TEST_SUITES[@]}"; do
	# ctest --test-dir "$BUILD_DIR" -R "^${test_suite}$" -V
	"$TEST_BINARY" --run_test="${test_suite}" --catch_system_error=no --log_level=test_suite -- -ctam=1
done
