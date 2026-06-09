#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make package-local >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-package-smoke-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
PAK_DIR="build/mac/package/CentralScrutinizer.pak"
PORT=8891

prepare_mock_sdcard "$SDCARD_ROOT"

SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK_DIR"
}

trap cleanup EXIT INT TERM

test -f "$PAK_DIR/bin/central-scrutinizer"
test -f "$PAK_DIR/launch.sh"
test -f "$PAK_DIR/pak.json"
test -f "$PAK_DIR/resources/web/index.html"
test -x "$PAK_DIR/bin/central-scrutinizer"
test -x "$PAK_DIR/launch.sh"
diff -qr web/out "$PAK_DIR/resources/web" >/dev/null
grep -Fq '"platform": "mlp1"' "$PAK_DIR/pak.json"
grep -Fq '"pak_version": "0.1.6"' "$PAK_DIR/pak.json"
grep -Fq '"min_jawaka_version": "0.0.1"' "$PAK_DIR/pak.json"

(
    cd "$PAK_DIR"
    ./launch.sh --headless --port "$PORT" --sdcard "$SDCARD_ROOT"
) &
SERVER_PID=$!

READY=0
for _ in $(seq 1 50); do
    if curl -sf "http://127.0.0.1:$PORT/api/status" >/dev/null; then
        READY=1
        break
    fi
    sleep 0.1
done

if [ "$READY" -ne 1 ]; then
    echo "packaged server did not become ready" >&2
    exit 1
fi

ROOT_HTML="$(curl -sf "http://127.0.0.1:$PORT/")"
echo "$ROOT_HTML" | grep -qi '<!doctype html'
echo "$ROOT_HTML" | grep -Fq '__next'

echo "PASS package layout smoke"
