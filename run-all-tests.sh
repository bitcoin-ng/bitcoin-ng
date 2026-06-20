#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BNG_REPO_ROOT="${BNG_REPO_ROOT:-${CTAM_REPO_ROOT:-$SCRIPT_DIR}}"
PYTHON_BIN="${PYTHON_BIN:-}"
TEST_BUILD_DIRS="${TEST_BUILD_DIRS:-build build_ctam_rest_test build_ctam_functional_test}"

usage() {
	cat <<EOF
Usage:
  $(basename -- "$0") [--repo-root PATH]

Environment:
  BNG_REPO_ROOT    Repo root to operate from (default: script directory)
  CTAM_REPO_ROOT   Legacy fallback for the repo root
  TEST_BUILD_DIRS  Space-separated build dirs to run ctest in
  PYTHON_BIN       Python interpreter to use for pytest (default: python3 or python)
EOF
}

find_python() {
	if [ -n "$PYTHON_BIN" ]; then
		command -v "$PYTHON_BIN" >/dev/null 2>&1 || return 1
		return 0
	fi

	if command -v python3 >/dev/null 2>&1; then
		PYTHON_BIN=python3
		return 0
	fi

	if command -v python >/dev/null 2>&1; then
		PYTHON_BIN=python
		return 0
	fi

	return 1
}

run_ctest_dir() {
	build_dir="$1"
	if [ ! -f "$BNG_REPO_ROOT/$build_dir/CTestTestfile.cmake" ]; then
		echo "Skipping $build_dir (no CTestTestfile.cmake found)"
		return 0
	fi

	echo "Running ctest in $build_dir..."
	ctest --test-dir "$BNG_REPO_ROOT/$build_dir" --output-on-failure
}

run_functional_tests() {
	runner="$BNG_REPO_ROOT/build/test/functional/test_runner.py"
	if [ ! -x "$runner" ]; then
		runner="$BNG_REPO_ROOT/test/functional/test_runner.py"
	fi
	if [ ! -f "$runner" ]; then
		echo "Functional test runner not found under $BNG_REPO_ROOT" >&2
		exit 1
	fi

	if ! find_python; then
		echo "Neither python3 nor python is available; cannot run the functional suite." >&2
		exit 127
	fi

	echo "Running functional tests with $PYTHON_BIN..."
	"$PYTHON_BIN" "$runner"
}

run_ctam_functional_tests() {
	script="$BNG_REPO_ROOT/run-ctam-functional-tests.sh"
	if [ ! -x "$script" ]; then
		echo "CTAM functional test wrapper not found or not executable: $script" >&2
		exit 1
	fi

	if ! find_python; then
		echo "Neither python3 nor python is available; cannot run the CTAM functional wrapper." >&2
		exit 127
	fi

	echo "Running CTAM functional flow tests via run-ctam-functional-tests.sh..."
	BNG_REPO_DIR="$BNG_REPO_ROOT" PYTHON="$PYTHON_BIN" "$script"
}

if [ ! -d "$BNG_REPO_ROOT" ]; then
	echo "BNG repo not found at: $BNG_REPO_ROOT" >&2
	exit 1
fi

case "${1:-}" in
	-h|--help|help)
		usage
		exit 0
		;;
	--repo-root)
		BNG_REPO_ROOT="${2:-}"
		shift 2
		;;
	"")
		;;
	*)
		echo "Unknown argument: $1" >&2
		usage >&2
		exit 2
		;;
esac

if [ ! -d "$BNG_REPO_ROOT" ]; then
	echo "BNG repo not found at: $BNG_REPO_ROOT" >&2
	exit 1
fi

cd "$BNG_REPO_ROOT"

echo "BNG repo root: $BNG_REPO_ROOT"

for build_dir in $TEST_BUILD_DIRS; do
	run_ctest_dir "$build_dir"
done

run_functional_tests
run_ctam_functional_tests
