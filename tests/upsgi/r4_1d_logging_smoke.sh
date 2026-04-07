#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
UPSGI_BIN="${1:-${REPO_DIR}/upsgi}"
APP="$REPO_DIR/tests/upsgi/apps/logging_smoke.psgi"
HELPER="$REPO_DIR/tests/upsgi/helpers/udp_log_collector.py"
TMPDIR="${TMPDIR:-/tmp}/upsgi_r4_1d_$$"
PORT="${UPSGI_TEST_PORT:-19191}"
LOGSOCKET_PORT="${UPSGI_LOGSOCKET_PORT:-17170}"
RSYSLOG_PORT="${UPSGI_RSYSLOG_PORT:-17171}"

mkdir -p "$TMPDIR"

cleanup() {
    set +e
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill -INT "$SERVER_PID" >/dev/null 2>&1 || true
        wait "$SERVER_PID" >/dev/null 2>&1 || true
    fi
    if [[ -n "${UDP1_PID:-}" ]]; then
        kill "$UDP1_PID" >/dev/null 2>&1 || true
    fi
    if [[ -n "${UDP2_PID:-}" ]]; then
        kill "$UDP2_PID" >/dev/null 2>&1 || true
    fi
    rm -rf "$TMPDIR"
}
trap cleanup EXIT INT TERM

request() {
    python3 - "$@" <<'PY'
import http.client
import sys
host = "127.0.0.1"
port = int(sys.argv[1])
path = sys.argv[2]
header_name = sys.argv[3] if len(sys.argv) > 3 else None
header_value = sys.argv[4] if len(sys.argv) > 4 else None
conn = http.client.HTTPConnection(host, port, timeout=5)
headers = {}
if header_name:
    headers[header_name] = header_value
conn.request("GET", path, headers=headers)
resp = conn.getresponse()
body = resp.read().decode("utf-8", errors="replace")
print(resp.status)
print(body)
conn.close()
PY
}

wait_http() {
    local path="$1"
    for _ in $(seq 1 60); do
        if request "$PORT" "$path" >/dev/null 2>&1; then
            return 0
        fi
        if [[ -n "${SERVER_PID:-}" ]] && ! kill -0 "$SERVER_PID" >/dev/null 2>&1; then
            return 1
        fi
        sleep 0.25
    done
    return 1
}

if [[ ! -x "$UPSGI_BIN" ]]; then
    echo "missing executable upsgi binary: $UPSGI_BIN" >&2
    echo "build the fork first, then rerun this smoke harness" >&2
    exit 2
fi

python3 "$HELPER" --port "$LOGSOCKET_PORT" --output "$TMPDIR/logsocket.out" --timeout 8 &
UDP1_PID=$!
python3 "$HELPER" --port "$RSYSLOG_PORT" --output "$TMPDIR/rsyslog.out" --timeout 8 &
UDP2_PID=$!

"$UPSGI_BIN" \
    --master \
    --workers 1 \
    --need-app \
    --strict \
    --vacuum \
    --die-on-term \
    --http-socket ":$PORT" \
    --psgi "$APP" \
    --logger "file:$TMPDIR/server.log" \
    --logger "socket:127.0.0.1:$LOGSOCKET_PORT" \
    --logger "rsyslog:127.0.0.1:$RSYSLOG_PORT,upsgi-test,30" \
    --log-x-forwarded-for \
    >/dev/null 2>&1 &
SERVER_PID=$!

if ! wait_http /; then
    echo "upsgi failed to start in default logging mode" >&2
    exit 1
fi

request "$PORT" / >/dev/null
request "$PORT" /whoami X-Forwarded-For 203.0.113.10 > "$TMPDIR/whoami.out"
request "$PORT" /psgix-log >/dev/null || true
request "$PORT" /error >/dev/null || true

sleep 2

grep -q "203.0.113.10" "$TMPDIR/whoami.out"
grep -q "GET / " "$TMPDIR/server.log"
grep -q "psgix logger hit" "$TMPDIR/server.log"
grep -q "GET / " "$TMPDIR/logsocket.out"
grep -q "GET / " "$TMPDIR/rsyslog.out"

if grep -q "Devel::StackTrace" "$TMPDIR/server.log"; then
    echo "unexpected stack trace logging without --log-exceptions" >&2
    exit 1
fi

kill -INT "$SERVER_PID" >/dev/null 2>&1 || true
wait "$SERVER_PID" >/dev/null 2>&1 || true
unset SERVER_PID
sleep 2

"$UPSGI_BIN" \
    --master \
    --workers 1 \
    --need-app \
    --strict \
    --vacuum \
    --die-on-term \
    --http-socket ":$PORT" \
    --psgi "$APP" \
    --logger "file:$TMPDIR/server_debug.log" \
    --log-exceptions \
    >/dev/null 2>&1 &
SERVER_PID=$!

if ! wait_http /; then
    echo "upsgi failed to start in debug exception mode" >&2
    exit 1
fi

request "$PORT" /error >/dev/null || true
sleep 2

grep -Eq "Devel::StackTrace|logging smoke requested failure|\[upsgi-perl error\] upsgi regression die marker" "$TMPDIR/server_debug.log"

echo "R4.1d logging smoke passed"
