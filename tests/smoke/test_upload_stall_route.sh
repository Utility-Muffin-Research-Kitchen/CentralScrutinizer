#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-upload-stall-smoke-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
SERVER_LOG="$WORK_DIR/server.log"
PORT="$(python3 - <<'PY'
import socket

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.bind(("127.0.0.1", 0))
    print(sock.getsockname()[1])
PY
)"

prepare_mock_sdcard "$SDCARD_ROOT"
rm -rf "$SDCARD_ROOT/.userdata/mlp1/CentralScrutinizer"

CS_PAIRING_CODE=7391 ./build/mac/central-scrutinizer --headless --port "$PORT" --web-root web/out --sdcard "$SDCARD_ROOT" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$WORK_DIR"
}

trap cleanup EXIT INT TERM

READY=0
for _ in $(seq 1 50); do
    if curl -sf "http://127.0.0.1:$PORT/api/status" >/dev/null; then
        READY=1
        break
    fi
    sleep 0.1
done

if [ "$READY" -ne 1 ]; then
    echo "server did not become ready" >&2
    cat "$SERVER_LOG" >&2
    exit 1
fi

PORT="$PORT" SDCARD_ROOT="$SDCARD_ROOT" python3 - <<'PY'
import http.client
import json
import os
import socket
import sys
import time
from pathlib import Path
from urllib.parse import urlencode

port = int(os.environ["PORT"])
sdcard_root = Path(os.environ["SDCARD_ROOT"])

conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
conn.request(
    "POST",
    "/api/pair",
    body=urlencode({"browser_id": "stall-browser", "code": "7391"}),
    headers={"Content-Type": "application/x-www-form-urlencoded"},
)
resp = conn.getresponse()
pair_body = resp.read().decode("utf-8", "replace")
cookie = (resp.getheader("Set-Cookie") or "").split(";", 1)[0]
if resp.status != 200 or not cookie:
    raise SystemExit(f"pair failed {resp.status}: {pair_body}")

conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
conn.request("GET", "/api/session", headers={"Cookie": cookie})
resp = conn.getresponse()
session_body = resp.read().decode("utf-8", "replace")
if resp.status != 200:
    raise SystemExit(f"session failed {resp.status}: {session_body}")
csrf = json.loads(session_body)["csrf"]

boundary = "----cs-upload-stall-boundary"
filename = "stall-upload.bin"
payload = b"A" * (1024 * 1024)
body = b"".join(
    [
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"scope\"\r\n\r\nfiles\r\n".encode(),
        (
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"path\"\r\n\r\n"
            ".userdata/mlp1/CentralScrutinizer/imports\r\n"
        ).encode(),
        (
            f"--{boundary}\r\n"
            f"Content-Disposition: form-data; name=\"file\"; filename=\"{filename}\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n"
        ).encode(),
        payload,
        b"\r\n",
        f"--{boundary}--\r\n".encode(),
    ]
)
split_at = 12 * 1024
headers = (
    "POST /api/upload HTTP/1.1\r\n"
    f"Host: 127.0.0.1:{port}\r\n"
    f"Cookie: {cookie}\r\n"
    f"X-CS-CSRF: {csrf}\r\n"
    f"Content-Type: multipart/form-data; boundary={boundary}\r\n"
    f"Content-Length: {len(body)}\r\n"
    "Connection: close\r\n"
    "\r\n"
).encode()

response = b""
with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.sendall(headers + body[:split_at])
    time.sleep(5.0)
    sock.sendall(body[split_at:])
    sock.settimeout(10)
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        response += chunk

status_line = response.split(b"\r\n", 1)[0].decode("ascii", "replace") if response else "<no response>"
if not status_line.startswith("HTTP/1.1 200 "):
    sys.stderr.write(response.decode("utf-8", "replace"))
    raise SystemExit(f"expected upload success after stalled body, got {status_line}")

uploaded = sdcard_root / ".userdata/mlp1/CentralScrutinizer/imports" / filename
if uploaded.stat().st_size != len(payload):
    raise SystemExit("uploaded file size mismatch")
PY

echo "PASS upload stall route smoke"
