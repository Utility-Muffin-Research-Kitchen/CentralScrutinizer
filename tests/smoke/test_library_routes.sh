#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-library-routes-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
COOKIE_JAR="$WORK_DIR/cookies.txt"
ROM_NAME=""
ROM_BASE=""
CSRF_TOKEN=""

prepare_mock_sdcard "$SDCARD_ROOT"

ROM_NAME="$(find "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)" -maxdepth 1 -type f -name '*.gba' -print | sed 's#.*/##' | head -n 1)"
ROM_BASE="${ROM_NAME%.*}"
[ -n "$ROM_NAME" ]
mkdir -p "$SDCARD_ROOT/Images/GBA"
mkdir -p "$SDCARD_ROOT/States"
mkdir -p "$SDCARD_ROOT/.system/leaf/platforms/mlp1/cores"
mkdir -p "$SDCARD_ROOT/Roms/ARCADE"
mkdir -p "$SDCARD_ROOT/Roms/ATARI"
mkdir -p "$SDCARD_ROOT/Roms/MS"
mkdir -p "$SDCARD_ROOT/Roms/Dreamcast (FLYCAST)"
mkdir -p "$SDCARD_ROOT/Roms/Awesome System (FOO)"
printf 'png' > "$SDCARD_ROOT/Images/GBA/$ROM_BASE.png"
printf 'state' > "$SDCARD_ROOT/States/$ROM_BASE.state"
printf 'arcade' > "$SDCARD_ROOT/Roms/ARCADE/1942.zip"
printf 'atari' > "$SDCARD_ROOT/Roms/ATARI/Pitfall.a26"
printf 'sms' > "$SDCARD_ROOT/Roms/MS/Sonic.sms"
printf 'rom' > "$SDCARD_ROOT/Roms/Awesome System (FOO)/sample.rom"
printf 'core' > "$SDCARD_ROOT/.system/leaf/platforms/mlp1/cores/fbneo_libretro.so"
printf 'core' > "$SDCARD_ROOT/.system/leaf/platforms/mlp1/cores/stella2014_libretro.so"
printf 'core' > "$SDCARD_ROOT/.system/leaf/platforms/mlp1/cores/genesis_plus_gx_libretro.so"
printf 'core' > "$SDCARD_ROOT/.system/leaf/platforms/mlp1/cores/FOO_libretro.so"
export SDCARD_ROOT ROM_NAME ROM_BASE
python3 - <<'PY'
import os
import sqlite3

root = os.environ["SDCARD_ROOT"]
rom_name = os.environ["ROM_NAME"]
rom_base = os.environ["ROM_BASE"]
db_path = os.path.join(root, ".umrk", "mlp1", "library.db")
os.makedirs(os.path.dirname(db_path), exist_ok=True)
db = sqlite3.connect(db_path)
db.executescript(
    """
    CREATE TABLE games (
        id INTEGER PRIMARY KEY,
        system TEXT NOT NULL,
        name TEXT NOT NULL,
        rom_path TEXT NOT NULL UNIQUE,
        image_path TEXT,
        last_played INTEGER,
        playtime_s INTEGER NOT NULL DEFAULT 0
    );
    CREATE TABLE favorites (
        kind TEXT NOT NULL CHECK (kind IN ('game','app')),
        target_id INTEGER NOT NULL,
        added_at INTEGER NOT NULL,
        PRIMARY KEY (kind, target_id)
    );
    """
)
db.executemany(
    "INSERT INTO games (system, name, rom_path, image_path) VALUES (?, ?, ?, ?);",
    [
        ("GBA", "Database GBA", f"Roms/Game Boy Advance (GBA)/{rom_name}", f"Images/GBA/{rom_base}.png"),
        ("ARCADE", "Database Arcade", "Roms/ARCADE/1942.zip", None),
        ("ATARI2600", "Database Atari", "Roms/ATARI/Pitfall.a26", None),
        ("MS", "Database Master System", "Roms/MS/Sonic.sms", None),
    ],
)
db.commit()
db.close()
PY

CS_PAIRING_CODE=7391 ./build/mac/central-scrutinizer --headless --port 8877 --web-root web/out --sdcard "$SDCARD_ROOT" &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$WORK_DIR"
}

trap cleanup EXIT INT TERM

READY=0
for _ in $(seq 1 50); do
    if curl -sf http://127.0.0.1:8877/api/status >/dev/null; then
        READY=1
        break
    fi
    sleep 0.1
done

if [ "$READY" -ne 1 ]; then
    echo "server did not become ready" >&2
    exit 1
fi

UNAUTH_PLATFORMS="$(curl -s 'http://127.0.0.1:8877/api/platforms' -w '\n%{http_code}')"
echo "$UNAUTH_PLATFORMS" | head -n 1 | grep -Fq '{"ok":false}'
echo "$UNAUTH_PLATFORMS" | tail -n 1 | grep -q '^403$'

UNAUTH_BROWSER="$(curl -s 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA' -w '\n%{http_code}')"
echo "$UNAUTH_BROWSER" | head -n 1 | grep -Fq '{"ok":false}'
echo "$UNAUTH_BROWSER" | tail -n 1 | grep -q '^403$'

curl -sf -c "$COOKIE_JAR" -X POST --data "browser_id=library-browser&code=7391" http://127.0.0.1:8877/api/pair | grep -q '"ok":true'

SESSION_RESPONSE="$(curl -sS -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session)"
CSRF_TOKEN="$(printf '%s' "$SESSION_RESPONSE" | sed -n 's/.*"csrf":"\([^"]*\)".*/\1/p')"
[ -n "$CSRF_TOKEN" ]

# NDJSON streams chunk-by-chunk, so capture the full response once and grep against the variable
# instead of piping curl into grep — grep would short-circuit on first match and break the pipe.
PLATFORMS_RESPONSE="$(curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" http://127.0.0.1:8877/api/platforms)"
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"tag":"GBA"'
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"name":"Game Boy Advance"'
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"group":"Nintendo"'
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"counts":{"roms":1,"saves":1,"states":1,"bios":1,"overlays":0,"cheats":0}'
printf '%s' "$PLATFORMS_RESPONSE" | grep -F '"tag":"FBN"' | grep -Fq '"counts":{"roms":1'
printf '%s' "$PLATFORMS_RESPONSE" | grep -F '"tag":"A2600"' | grep -Fq '"counts":{"roms":1'
printf '%s' "$PLATFORMS_RESPONSE" | grep -F '"tag":"SMS"' | grep -Fq '"counts":{"roms":1'
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"type":"platform"'
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '{"type":"done"}'
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"tag":"PORTS"'
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"tag":"PORTS","name":"Ports","group":"PortMaster","icon":"PORTMASTER","isCustom":false'
if printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"requiresEmulator"'; then
    echo "platform response unexpectedly includes requiresEmulator" >&2
    exit 1
fi
if printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"emulatorInstalled"'; then
    echo "platform response unexpectedly includes emulatorInstalled" >&2
    exit 1
fi
if printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"catalog_error"'; then
    echo "platform response unexpectedly includes catalog_error" >&2
    exit 1
fi
# User-added core: FOO_libretro.so is installed, so Roms/Awesome System (FOO) should surface as a custom platform.
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"type":"platform","group":"Custom","platform":{"tag":"FOO"'
printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"tag":"FOO","name":"Awesome System","group":"Custom","icon":"FOO","isCustom":true'
# FLYCAST has no matching Leaf core/info file, so it must stay hidden.
if printf '%s' "$PLATFORMS_RESPONSE" | grep -Fq '"tag":"FLYCAST"'; then
    echo "custom platform unexpectedly exposed in library response" >&2
    exit 1
fi

SYSTEMS_CATALOG="$SDCARD_ROOT/.system/leaf/platforms/mlp1/defaults/systems.json"
mv "$SYSTEMS_CATALOG" "$SYSTEMS_CATALOG.missing"
CATALOG_ERROR_RESPONSE="$(curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" http://127.0.0.1:8877/api/platforms)"
mv "$SYSTEMS_CATALOG.missing" "$SYSTEMS_CATALOG"
printf '%s' "$CATALOG_ERROR_RESPONSE" | grep -Fq '"type":"catalog_error"'
printf '%s' "$CATALOG_ERROR_RESPONSE" | grep -Fq '"kind":"missing"'
printf '%s' "$CATALOG_ERROR_RESPONSE" | grep -Fq '{"type":"done"}'

curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=FOO' | grep -Fq '"name":"sample.rom"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA' | grep -Fq "\"name\":\"$ROM_NAME\""
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA' | grep -Fq "\"thumbnailPath\":\"Images/GBA/$ROM_BASE.png\""
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=FBN' | grep -Fq '"name":"1942.zip"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=A2600' | grep -Fq '"name":"Pitfall.a26"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=SMS' | grep -Fq '"name":"Sonic.sms"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=saves&tag=GBA' | grep -Fq '"name":"Pokemon Emerald.sav"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=bios&tag=PS' | grep -Fq '"name":"scph1001.bin"'
UNSUPPORTED_OVERLAYS="$(curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=overlays&tag=GBA' -w '\n%{http_code}')"
echo "$UNSUPPORTED_OVERLAYS" | head -n 1 | grep -Fq '"error":"scope_not_supported"'
echo "$UNSUPPORTED_OVERLAYS" | tail -n 1 | grep -q '^404$'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=files' | grep -Fq '"name":"Images"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=files' | grep -Fq '"name":"Roms"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA&offset=0' | grep -Fq '"offset":0'

INVALID_OFFSET="$(curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA&offset=-1' -w '\n%{http_code}')"
echo "$INVALID_OFFSET" | head -n 1 | grep -Fq '"error":"invalid_offset"'
echo "$INVALID_OFFSET" | tail -n 1 | grep -q '^400$'

INVALID_SCOPE="$(curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=bogus' -w '\n%{http_code}')"
echo "$INVALID_SCOPE" | head -n 1 | grep -Fq '"error":"invalid_scope"'
echo "$INVALID_SCOPE" | tail -n 1 | grep -q '^400$'

MISSING_PLATFORM="$(curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=NOPE' -w '\n%{http_code}')"
echo "$MISSING_PLATFORM" | head -n 1 | grep -Fq '"error":"platform_not_found"'
echo "$MISSING_PLATFORM" | tail -n 1 | grep -q '^404$'

TRAVERSAL="$(curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=files&path=..%2Foutside' -w '\n%{http_code}')"
echo "$TRAVERSAL" | head -n 1 | grep -Fq '"error":"path_not_found"'
echo "$TRAVERSAL" | tail -n 1 | grep -q '^404$'

echo "PASS library smoke"
