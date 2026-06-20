#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
CTAM_REPO_ROOT="${CTAM_REPO_ROOT:-$SCRIPT_DIR/../../ctam}"
CTAM_STATE_PATH="${CTAM_STATE_PATH:-${TMPDIR:-/tmp}/ctam-bng-state}"
CTAM_PORT="${CTAM_PORT:-8080}"
RUST_LOG="${RUST_LOG:-debug}"

if [ ! -d "$CTAM_REPO_ROOT" ]; then
	echo "CTAM repo not found at: $CTAM_REPO_ROOT" >&2
	exit 1
fi

mkdir -p "$CTAM_STATE_PATH"

echo "Starting CTAM from: $CTAM_REPO_ROOT"
echo "CTAM state path: $CTAM_STATE_PATH"
echo "CTAM URL: http://127.0.0.1:$CTAM_PORT/"
echo "RUST_LOG: $RUST_LOG"

cd "$CTAM_REPO_ROOT"

if [ -x "$CTAM_REPO_ROOT/target/debug/ctam" ]; then
	exec env RUST_LOG="$RUST_LOG" \
		"$CTAM_REPO_ROOT/target/debug/ctam" \
		--serve rest \
		--state-path "$CTAM_STATE_PATH" \
		--port "$CTAM_PORT"
fi

exec env RUST_LOG="$RUST_LOG" \
	cargo run \
	--manifest-path "$CTAM_REPO_ROOT/Cargo.toml" \
	-- \
	--serve rest \
	--state-path "$CTAM_STATE_PATH" \
	--port "$CTAM_PORT"
