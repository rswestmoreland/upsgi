#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
UPSGI_BIN="${UPSGI_BIN:-${REPO_DIR}/upsgi}"
PORT="${UPSGI_SOAK_PORT:-19195}"
BURST_COUNT="${UPSGI_SOAK_BURST_COUNT:-25}"
APP="$REPO_DIR/tests/fork/fixtures/apps/app_simple.psgi"
ARTIFACT_BASE="$REPO_DIR/tests/fork/artifacts"
mkdir -p "$ARTIFACT_BASE"
ARTIFACT_DIR="$(mktemp -d "$ARTIFACT_BASE/soak_burst.XXXXXX")"
LOG_FILE="$ARTIFACT_DIR/server.log"
PID_FILE="$ARTIFACT_DIR/server.pid"

cleanup() {
    local status=$?
    set +e
    local pid=""
    if [[ -f "$PID_FILE" ]]; then
        pid="$(cat "$PID_FILE")"
    elif [[ -n "${SERVER_PID:-}" ]]; then
        pid="$SERVER_PID"
    fi
    if [[ -n "$pid" ]] && kill -0 "$pid" >/dev/null 2>&1; then
        kill -INT "$pid" >/dev/null 2>&1 || true
        for _ in $(seq 1 30); do
            if ! kill -0 "$pid" >/dev/null 2>&1; then
                break
            fi
            sleep 0.10
        done
        if kill -0 "$pid" >/dev/null 2>&1; then
            kill -TERM "$pid" >/dev/null 2>&1 || true
            for _ in $(seq 1 20); do
                if ! kill -0 "$pid" >/dev/null 2>&1; then
                    break
                fi
                sleep 0.10
            done
        fi
        if kill -0 "$pid" >/dev/null 2>&1; then
            kill -9 "$pid" >/dev/null 2>&1 || true
        fi
    fi
    if [[ $status -eq 0 ]]; then
        rm -rf "$ARTIFACT_DIR"
    else
        echo "request burst soak preserved artifacts in $ARTIFACT_DIR" >&2
    fi
    exit $status
}
trap cleanup EXIT

if [[ ! -x "$UPSGI_BIN" ]]; then
    echo "missing executable upsgi binary: $UPSGI_BIN" >&2
    exit 2
fi

"$UPSGI_BIN" \
    --master \
    --workers 1 \
    --need-app \
    --strict \
    --vacuum \
    --die-on-term \
    --http-socket ":$PORT" \
    --psgi "$APP" \
    --logger "file:$LOG_FILE" \
    --pidfile "$PID_FILE" \
    >/dev/null 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 80); do
    if [[ -f "$PID_FILE" ]]; then
        break
    fi
    sleep 0.25
done

if [[ ! -f "$PID_FILE" ]]; then
    echo "pid file not created: $PID_FILE" >&2
    exit 1
fi

python3 - "$PORT" <<'PY2'
import http.client, sys, time
port = int(sys.argv[1])
for _ in range(60):
    try:
        conn = http.client.HTTPConnection('127.0.0.1', port, timeout=1)
        conn.request('GET', '/')
        resp = conn.getresponse()
        resp.read()
        conn.close()
        sys.exit(0)
    except Exception:
        time.sleep(0.25)
sys.exit(1)
PY2

for i in $(seq 1 "$BURST_COUNT"); do
    python3 - "$PORT" "$i" <<'PY2'
import http.client, sys
port = int(sys.argv[1])
i = sys.argv[2]
path = f'/burst/{i}'
conn = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
conn.request('GET', path)
resp = conn.getresponse()
body = resp.read().decode('utf-8', 'replace')
conn.close()
if resp.status != 200 or path not in body:
    raise SystemExit(1)
PY2
done

echo "request burst soak passed ($BURST_COUNT requests)"
