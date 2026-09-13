#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

# Set CS_SMOKE_TMPDIR to a case-sensitive volume to run the distinct-case
# scenario; the default /tmp on macOS runs the case-alias scenario instead.
WORK_DIR="$(mktemp -d "${CS_SMOKE_TMPDIR:-/tmp}/cs-art-replace-smoke-XXXXXX")"
SDCARD_ROOT="$WORK_DIR/sdcard"
COOKIE_JAR="$WORK_DIR/cookies.txt"
ROM_NAME="Artwork Test.gba"
ROM_PATH="$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/$ROM_NAME"
PNG_SOURCE="$WORK_DIR/replacement.png"
JPG_SOURCE="$WORK_DIR/replacement.jpg"
JPEG_SOURCE="$WORK_DIR/replacement.jpeg"
JPG_ALIAS_SOURCE="$WORK_DIR/alias.jpg"
GIF_SOURCE="$WORK_DIR/replacement.gif"
ART_DIR="$SDCARD_ROOT/Images/GBA"
PNG_TARGET="$ART_DIR/Artwork Test.png"
JPG_TARGET="$ART_DIR/Artwork Test.jpg"
JPEG_TARGET="$ART_DIR/Artwork Test.jpeg"
WEBP_TARGET="$ART_DIR/Artwork Test.webp"
CSRF_TOKEN=""
IMMUTABLE_FILE=""

prepare_mock_sdcard "$SDCARD_ROOT"
mkdir -p "$ART_DIR"
printf 'rom' > "$ROM_PATH"
printf 'old-jpg' > "$JPG_TARGET"
printf 'old-jpeg' > "$JPEG_TARGET"
printf 'old-webp' > "$WEBP_TARGET"
printf 'png' > "$PNG_SOURCE"
printf 'jpg' > "$JPG_SOURCE"
printf 'jpeg' > "$JPEG_SOURCE"
printf 'jpg-alias' > "$JPG_ALIAS_SOURCE"
printf 'GIF89a' > "$GIF_SOURCE"

printf 'probe' > "$ART_DIR/CaseProbe"
if [ -e "$ART_DIR/caseprobe" ]; then
    CASE_SENSITIVE=0
else
    CASE_SENSITIVE=1
fi
rm -f "$ART_DIR/CaseProbe"

if [ "$CASE_SENSITIVE" -eq 1 ]; then
    # Distinct files on a case-sensitive card: same stem, other extension
    # spellings (cleared), and a differently cased stem (kept).
    printf 'old-upper-png' > "$ART_DIR/Artwork Test.PNG"
    printf 'old-mixed-jpg' > "$ART_DIR/Artwork Test.JpG"
    printf 'old-upper-webp' > "$ART_DIR/Artwork Test.WEBP"
    printf 'other-stem-png' > "$ART_DIR/artwork test.png"
    printf 'other-stem-jpg' > "$ART_DIR/artwork test.jpg"
fi

CS_PAIRING_CODE=7391 ./build/mac/central-scrutinizer --headless --port 8877 --web-root web/out --sdcard "$SDCARD_ROOT" &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    if [ -n "$IMMUTABLE_FILE" ]; then
        chflags nouchg "$IMMUTABLE_FILE" 2>/dev/null || true
    fi
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

fail() {
    echo "$1" >&2
    exit 1
}

# replace_art <source file> <upload filename> -> body, newline, HTTP status
replace_art() {
    curl -sS -X POST \
        -b "$COOKIE_JAR" \
        -H "X-CS-CSRF: $CSRF_TOKEN" \
        -F "tag=GBA" \
        -F "path=$ROM_NAME" \
        -F "file=@$1;filename=$2" \
        -w '\n%{http_code}' \
        http://127.0.0.1:8877/api/art/replace
}

expect_replaced() {
    local response="$1"
    echo "$response" | head -n 1 | grep -Fq '{"ok":true,"action":"replace-art"}' ||
        fail "replace-art did not succeed: $response"
    echo "$response" | tail -n 1 | grep -q '^200$' || fail "replace-art status: $response"
}

# The thumbnail is the spelling on disk. A case-insensitive volume can keep an
# old entry's spelling when the upload is renamed onto it, so compare without
# case there.
expect_thumbnail() {
    local response grep_flags=-Fq
    [ "$CASE_SENSITIVE" -eq 1 ] || grep_flags=-Fqi
    response="$(curl -sS -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA')"
    printf '%s' "$response" | grep -Fq "\"name\":\"$ROM_NAME\"" || fail "ROM missing from browser: $response"
    printf '%s' "$response" | grep $grep_flags "\"thumbnailPath\":\"$1\"" || fail "thumbnail is not $1: $response"
}

# This game's directory entries themselves (any stem case), so case aliases
# don't mask a leftover. Other games' art in the fixture is ignored.
art_entries() {
    (cd "$ART_DIR" && ls -1 | grep -i '^artwork test\.' | LC_ALL=C sort) || true
}

expect_entries() {
    local actual expected
    actual="$(art_entries)"
    expected="$(printf '%s\n' "$@" | LC_ALL=C sort)"
    [ "$actual" = "$expected" ] || fail "art folder has:
$actual
expected:
$expected"
}

UNAUTH_RESPONSE="$(curl -sS -X POST \
    -F "tag=GBA" \
    -F "path=$ROM_NAME" \
    -F "file=@$PNG_SOURCE;filename=replacement.png" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/art/replace)"
echo "$UNAUTH_RESPONSE" | head -n 1 | grep -Fq '{"ok":false}'
echo "$UNAUTH_RESPONSE" | tail -n 1 | grep -q '^403$'

PAIR_RESPONSE="$(curl -sS -c "$COOKIE_JAR" -X POST --data "browser_id=art-browser&code=7391" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$PAIR_RESPONSE" | head -n 1 | grep -Fq '{"ok":true'
echo "$PAIR_RESPONSE" | tail -n 1 | grep -q '^200$'

SESSION_RESPONSE="$(curl -sS -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session)"
CSRF_TOKEN="$(printf '%s' "$SESSION_RESPONSE" | sed -n 's/.*"csrf":"\([^"]*\)".*/\1/p')"
[ -n "$CSRF_TOKEN" ]

# Unsupported formats are rejected before anything changes.
GIF_RESPONSE="$(replace_art "$GIF_SOURCE" replacement.gif)"
echo "$GIF_RESPONSE" | head -n 1 | grep -Fq '"error":"unsupported_art_type"'
echo "$GIF_RESPONSE" | tail -n 1 | grep -q '^400$'
test "$(cat "$JPG_TARGET")" = "old-jpg"

# PNG replaces every same-stem sibling.
expect_replaced "$(replace_art "$PNG_SOURCE" replacement.png)"
cmp -s "$PNG_SOURCE" "$PNG_TARGET"
if [ "$CASE_SENSITIVE" -eq 1 ]; then
    expect_entries "Artwork Test.png" "artwork test.jpg" "artwork test.png"
    test "$(cat "$ART_DIR/artwork test.png")" = "other-stem-png"
    test "$(cat "$ART_DIR/artwork test.jpg")" = "other-stem-jpg"
else
    expect_entries "Artwork Test.png"
fi
expect_thumbnail "Images/GBA/Artwork Test.png"

# PNG -> JPEG, keeping the upload's format and lower-casing its extension.
expect_replaced "$(replace_art "$JPEG_SOURCE" replacement.JPEG)"
cmp -s "$JPEG_SOURCE" "$JPEG_TARGET"
test ! -e "$PNG_TARGET"
expect_thumbnail "Images/GBA/Artwork Test.jpeg"

# JPEG -> JPG (mixed-case upload name).
expect_replaced "$(replace_art "$JPG_SOURCE" replacement.JpG)"
cmp -s "$JPG_SOURCE" "$JPG_TARGET"
test ! -e "$JPEG_TARGET"
expect_thumbnail "Images/GBA/Artwork Test.jpg"

if [ "$CASE_SENSITIVE" -eq 0 ]; then
    # Case aliases: an old file stored as "Artwork Test.JPG" is the same entry
    # the upload promotes onto, and an old "Artwork Test.PNG" answers to the
    # "Artwork Test.png" probe. Cleanup must remove the PNG and never the
    # replacement that the JPG spellings resolve to.
    rm -f "$JPG_TARGET"
    printf 'old-upper-jpg' > "$ART_DIR/Artwork Test.JPG"
    printf 'old-upper-png' > "$ART_DIR/Artwork Test.PNG"
    expect_replaced "$(replace_art "$JPG_ALIAS_SOURCE" cover.jpg)"
    cmp -s "$JPG_ALIAS_SOURCE" "$JPG_TARGET"
    [ "$(art_entries | tr '[:upper:]' '[:lower:]')" = "artwork test.jpg" ] ||
        fail "case alias cleanup left: $(art_entries)"
    expect_thumbnail "Images/GBA/Artwork Test.jpg"
else
    # Distinct-case stem art survived every replacement byte for byte.
    test "$(cat "$ART_DIR/artwork test.png")" = "other-stem-png"
    test "$(cat "$ART_DIR/artwork test.jpg")" = "other-stem-jpg"
fi

# JPG -> PNG selects the PNG again.
expect_replaced "$(replace_art "$PNG_SOURCE" replacement.png)"
cmp -s "$PNG_SOURCE" "$PNG_TARGET"
test ! -e "$JPG_TARGET"
expect_thumbnail "Images/GBA/Artwork Test.png"

# A failed promotion leaves existing art alone: the JPEG destination is a
# directory, so the rename cannot land.
mkdir "$JPEG_TARGET"
PROMOTE_FAIL_RESPONSE="$(replace_art "$JPEG_SOURCE" replacement.jpeg)"
if echo "$PROMOTE_FAIL_RESPONSE" | tail -n 1 | grep -q '^200$'; then
    fail "promotion onto a directory reported success: $PROMOTE_FAIL_RESPONSE"
fi
cmp -s "$PNG_SOURCE" "$PNG_TARGET"
test -d "$JPEG_TARGET"
rmdir "$JPEG_TARGET"
expect_thumbnail "Images/GBA/Artwork Test.png"

# A sibling that cannot be removed: the new file stays, but the response says
# the replacement is incomplete instead of claiming success.
if command -v chflags >/dev/null 2>&1; then
    printf 'stuck-jpg' > "$JPG_TARGET"
    chflags uchg "$JPG_TARGET"
    IMMUTABLE_FILE="$JPG_TARGET"
    CLEANUP_FAIL_RESPONSE="$(replace_art "$JPEG_SOURCE" replacement.jpeg)"
    echo "$CLEANUP_FAIL_RESPONSE" | head -n 1 | grep -Fq '"error":"art_cleanup_incomplete"' ||
        fail "cleanup failure not reported: $CLEANUP_FAIL_RESPONSE"
    echo "$CLEANUP_FAIL_RESPONSE" | tail -n 1 | grep -q '^500$'
    cmp -s "$JPEG_SOURCE" "$JPEG_TARGET"
    test ! -e "$PNG_TARGET"
    test "$(cat "$JPG_TARGET")" = "stuck-jpg"
    chflags nouchg "$JPG_TARGET"
    IMMUTABLE_FILE=""
else
    echo "SKIP cleanup failure scenario (no chflags)"
fi

if [ "$CASE_SENSITIVE" -eq 1 ]; then
    echo "PASS art replace smoke (case-sensitive)"
else
    echo "PASS art replace smoke (case-insensitive)"
fi
