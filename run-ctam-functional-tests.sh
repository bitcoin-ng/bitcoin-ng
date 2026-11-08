#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BNG_REPO_DIR="${BNG_REPO_DIR:-$SCRIPT_DIR}"
BUILD_DIR_NAME="${BUILD_DIR_NAME:-build_ctam_functional_test}"
BUILD_DIR="${BUILD_DIR:-$BNG_REPO_DIR/$BUILD_DIR_NAME}"
BUILD_TARGETS="${BUILD_TARGETS:-bitcoind bitcoin-cli}"
BUILD_JOBS="${BUILD_JOBS:-4}"
PYTHON="${PYTHON:-python3}"
FUNCTIONAL_TEST="${FUNCTIONAL_TEST:-ctam_mining_flows.py}"
TEST_CONFIG="${TEST_CONFIG:-$BUILD_DIR/test/config.ini}"
PYCACHE_DIR="${PYCACHE_DIR:-${TMPDIR:-/tmp}/ctam-functional-pycache}"

if [ ! -d "$BUILD_DIR" ]; then
	echo "Build directory '$BUILD_DIR' does not exist. Creating it and running cmake to configure the build."
	mkdir -p "$BUILD_DIR"
	cmake -S "$BNG_REPO_DIR" -B "$BUILD_DIR"
fi

read -r -a BUILD_TARGET_ARRAY <<< "$BUILD_TARGETS"
cmake --build "$BUILD_DIR" --target "${BUILD_TARGET_ARRAY[@]}" -j"$BUILD_JOBS"

PYTHONPYCACHEPREFIX="$PYCACHE_DIR" \
	"$PYTHON" "$BNG_REPO_DIR/test/functional/$FUNCTIONAL_TEST" \
	--configfile "$TEST_CONFIG" \
	"$@"
