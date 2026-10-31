#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

PORT=8891
WORK_DIR="$(mktemp -d /tmp/cs-metadata-routes-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
COOKIE_JAR="$WORK_DIR/cookies.txt"
SOCKET="$WORK_DIR/jawakad.sock"
DB="$SDCARD_ROOT/.umrk/mlp1/library.db"
FAKE_STATE="$WORK_DIR/daemon.json"
FAKE_LOG="$WORK_DIR/daemon.log"
SERVER_PID=""
DAEMON_PID=""

cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true
    [ -n "$DAEMON_PID" ] && kill "$DAEMON_PID" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT INT TERM

prepare_mock_sdcard "$SDCARD_ROOT"
mkdir -p "$(dirname "$DB")"
export DB
python3 -I - <<'PY'
import os
import sqlite3

db = sqlite3.connect(os.environ["DB"])
db.executescript(
    """
    CREATE TABLE games (id INTEGER PRIMARY KEY, system TEXT NOT NULL, name TEXT NOT NULL,
        source_id TEXT NOT NULL, rom_relpath TEXT NOT NULL, rom_path TEXT NOT NULL);
    CREATE TABLE game_settings (game_id INTEGER NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL,
        updated_at INTEGER NOT NULL, PRIMARY KEY (game_id, key));
    CREATE TABLE favorites (kind TEXT NOT NULL, target_id INTEGER NOT NULL, added_at INTEGER NOT NULL,
        PRIMARY KEY (kind, target_id));
    INSERT INTO games (id, system, name, source_id, rom_relpath, rom_path) VALUES
        (1, 'GBA', 'Zelda', 'primary', 'GBA/Zelda.gba', 'Roms/GBA/Zelda.gba');
    INSERT INTO game_settings VALUES (1, 'ss.synopsis', 'Scraped text', 1);
    """
)
db.commit()
db.close()
PY

set_daemon() {
    printf '%s' "$1" > "$FAKE_STATE"
}

# What jawakad answers on its socket, driven by FAKE_STATE, with each request logged.
fake_daemon() {
    # exec, so DAEMON_PID is the process that holds the socket and cleanup can stop it.
    exec python3 -I - "$SOCKET" "$FAKE_STATE" "$FAKE_LOG" <<'PY'
import json
import os
import socket
import struct
import sys

socket_path, state_path, log_path = sys.argv[1:]
server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(socket_path)
server.listen(16)
while True:
    conn, _ = server.accept()
    with conn:
        header = conn.recv(4)
        if len(header) < 4:
            continue
        size = struct.unpack("!I", header)[0]
        body = b""
        while len(body) < size:
            body += conn.recv(size - len(body))
        kind = json.loads(body)["type"]
        with open(log_path, "a") as log:
            log.write(kind + "\n")
        with open(state_path) as handle:
            state = json.load(handle)
        if kind == "library-status":
            reply = {"type": "library-status", "generation": 1, "scan_running": state.get("scan", False),
                     "pending_rescan": False, "scan_error": ""}
            if "caps" in state:
                reply["metadata_caps"] = state["caps"]
        elif state.get("reject"):
            reply = {"type": "error", "message": "no"}
        else:
            reply = {"type": "ok", "action": kind}
        data = json.dumps(reply).encode()
        conn.sendall(struct.pack("!I", len(data)) + data)
PY
}

set_daemon '{"caps":["user_synopsis","metadata_changed"]}'
: > "$FAKE_LOG"
fake_daemon &
DAEMON_PID=$!
for _ in $(seq 1 50); do
    [ -S "$SOCKET" ] && break
    sleep 0.1
done
[ -S "$SOCKET" ]

JAWAKA_SOCKET_PATH="$SOCKET" UMRK_INTERNAL_DATA_PATH="$(dirname "$DB")" CS_PAIRING_CODE=7391 \
    ./build/mac/central-scrutinizer --headless --port "$PORT" \
    --web-root web/out --sdcard "$SDCARD_ROOT" >"$WORK_DIR/server.log" 2>&1 &
SERVER_PID=$!

BASE="http://127.0.0.1:$PORT"
READY=0
for _ in $(seq 1 50); do
    if curl -sf "$BASE/api/status" >/dev/null; then
        READY=1
        break
    fi
    sleep 0.1
done
[ "$READY" -eq 1 ]

# Every route needs a paired browser, and writes also need the CSRF token.
curl -s "$BASE/api/library/games?tag=GBA" -w '\n%{http_code}' | tail -n 1 | grep -q '^403$'
curl -s -X POST "$BASE/api/library/metadata" -d '{}' -w '\n%{http_code}' | tail -n 1 | grep -q '^403$'

curl -sf -c "$COOKIE_JAR" -X POST --data "browser_id=metadata-browser&code=7391" "$BASE/api/pair" >/dev/null
CSRF="$(curl -sS -b "$COOKIE_JAR" "$BASE/api/session" | sed -n 's/.*"csrf":"\([^"]*\)".*/\1/p')"
[ -n "$CSRF" ]

post() {
    curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF" -H 'Content-Type: application/json' \
        -X POST "$BASE/api/library/metadata" -d "$1" -w '\n%{http_code}'
}

title_change() {
    printf '{"tag":"GBA","changes":[{"source":"primary","path":"GBA/Zelda.gba","cardKey":null,"field":"title","value":%s,"expected":%s}]}' "$1" "$2"
}

db_value() {
    python3 -I -c 'import os, sqlite3, sys
row = sqlite3.connect(os.environ["DB"]).execute("SELECT value FROM game_settings WHERE game_id=1 AND key=?", (sys.argv[1],)).fetchone()
print(row[0] if row else "<none>")' "$1"
}

expect_status() {
    local response="$1" status="$2" text="$3"
    if ! printf '%s' "$response" | tail -n 1 | grep -q "^$status\$" \
        || ! printf '%s' "$response" | grep -Fq "$text"; then
        echo "wanted $status and $text, got: $response" >&2
        exit 1
    fi
}

GAMES="$(curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF" "$BASE/api/library/games?tag=GBA")"
printf '%s' "$GAMES" | grep -Fq '"title":"Zelda"'
printf '%s' "$GAMES" | grep -Fq '"titleOverride":null'
printf '%s' "$GAMES" | grep -Fq '"descriptionSupported":true'

GAME="$(curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF" "$BASE/api/library/game?tag=GBA&source=primary&path=GBA%2FZelda.gba")"
printf '%s' "$GAME" | grep -Fq '"defaultDescription":"Scraped text"'
curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF" "$BASE/api/library/game?tag=GBA&source=primary&path=GBA%2FNope.gba" \
    -w '\n%{http_code}' | tail -n 1 | grep -q '^404$'

# Saved, and the launcher told to reload without a scan.
: > "$FAKE_LOG"
expect_status "$(post "$(title_change '"My Zelda"' null)")" 200 '"refreshed":true'
[ "$(db_value display_name)" = "My Zelda" ]
grep -Fxq 'library-metadata-changed' "$FAKE_LOG"
if grep -Fxq 'scan-library' "$FAKE_LOG"; then
    echo "a save on a current Leaf started a scan" >&2
    exit 1
fi

# Someone else changed it first: nothing written, and the response says what's there.
RESPONSE="$(post "$(title_change '"Stale"' null)")"
expect_status "$RESPONSE" 409 '"reason":"stale"'
printf '%s' "$RESPONSE" | grep -Fq '"current":"My Zelda"'
[ "$(db_value display_name)" = "My Zelda" ]

# Nothing is written during a scan, since it can swap the second card's rows.
set_daemon '{"caps":["user_synopsis","metadata_changed"],"scan":true}'
expect_status "$(post "$(title_change null '"My Zelda"')")" 409 '"error":"scan_running"'
[ "$(db_value display_name)" = "My Zelda" ]

# The commit stands when the refresh fails, and the response tells them apart.
set_daemon '{"caps":["user_synopsis","metadata_changed"],"reject":true}'
expect_status "$(post "$(title_change '"Saved anyway"' '"My Zelda"')")" 200 '"refreshed":false'
[ "$(db_value display_name)" = "Saved anyway" ]

# Reset removes the row. A Leaf without the message refreshes through a scan.
set_daemon '{}'
: > "$FAKE_LOG"
expect_status "$(post "$(title_change null '"Saved anyway"')")" 200 '"refreshed":true'
[ "$(db_value display_name)" = "<none>" ]
grep -Fxq 'scan-library' "$FAKE_LOG"

# That Leaf wouldn't show a description override, so it isn't offered or accepted.
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF" "$BASE/api/library/games?tag=GBA" | grep -Fq '"descriptionSupported":false'
DESCRIPTION='{"tag":"GBA","changes":[{"source":"primary","path":"GBA/Zelda.gba","cardKey":null,"field":"description","value":"Mine","expected":null}]}'
expect_status "$(post "$DESCRIPTION")" 400 '"error":"description_unsupported"'
[ "$(db_value user_synopsis)" = "<none>" ]

set_daemon '{"caps":["user_synopsis"]}'
expect_status "$(post "$DESCRIPTION")" 200 '"saved":1'
[ "$(db_value user_synopsis)" = "Mine" ]
[ "$(db_value ss.synopsis)" = "Scraped text" ]

# With no daemon at all the save stands, and isn't reported as a refresh.
kill "$DAEMON_PID" 2>/dev/null || true
wait "$DAEMON_PID" 2>/dev/null || true
DAEMON_PID=""
rm -f "$SOCKET"
expect_status "$(post "$(title_change '"Offline"' null)")" 200 '"refreshed":false'
[ "$(db_value display_name)" = "Offline" ]

# Malformed and oversized requests.
expect_status "$(post 'not json')" 400 '"error":"invalid_request"'
expect_status "$(post '{"tag":"GBA"}')" 400 '"error":"invalid_request"'
expect_status "$(post '{"tag":"NOPE","changes":[]}')" 404 '"error":"platform_not_found"'
python3 -I -c 'print("x" * (4 * 1024 * 1024 + 1), end="")' > "$WORK_DIR/too-big"
curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF" -X POST "$BASE/api/library/metadata" \
    --data-binary "@$WORK_DIR/too-big" -w '\n%{http_code}' | tail -n 1 | grep -q '^400$'

echo "metadata route smoke passed"
