#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
PORT="${UPSGI_TEST_PORT:-9192}"
UPSGI_BIN="${UPSGI_BIN:-${REPO_DIR}/upsgi}"
APP_FILE="$REPO_DIR/tests/upsgi/apps/static_fallback.psgi"
HTTP_PROBE="$REPO_DIR/tests/upsgi/helpers/http_probe.py"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/upsgi-static-smoke.XXXXXX")"
LOG_FILE="$WORK_DIR/upsgi.log"
STATIC_ROOT="$WORK_DIR/static-root"
SECRET_FILE="$WORK_DIR/secret.txt"

cleanup() {
    set +e
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill -INT "$SERVER_PID" >/dev/null 2>&1 || true
        wait "$SERVER_PID" >/dev/null 2>&1 || true
    fi
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

mkdir -p "$STATIC_ROOT"
printf 'hello from static-map\n' > "$STATIC_ROOT/hello.txt"
printf 'TOP-SECRET outside static root\n' > "$SECRET_FILE"

if [[ ! -x "$UPSGI_BIN" ]]; then
    echo "missing executable upsgi binary: $UPSGI_BIN" >&2
    echo "build the fork first, then rerun this smoke harness" >&2
    exit 2
fi

"$UPSGI_BIN" \
    --master \
    --workers 1 \
    --need-app \
    --strict \
    --die-on-term \
    --vacuum \
    --http-socket ":$PORT" \
    --psgi "$APP_FILE" \
    --static-map "/assets=$STATIC_ROOT" \
    --logto "$LOG_FILE" \
    >/dev/null 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 60); do
    if python3 "$HTTP_PROBE" --port "$PORT" --method GET --path /assets/hello.txt --expect-status 200 --expect-body $'hello from static-map\n' >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "$SERVER_PID" >/dev/null 2>&1; then
        echo "upsgi failed to start" >&2
        [[ -f "$LOG_FILE" ]] && cat "$LOG_FILE" >&2
        exit 1
    fi
    sleep 0.25
done

python3 "$HTTP_PROBE" --port "$PORT" --method GET --path /assets/hello.txt --expect-status 200 --expect-body $'hello from static-map\n'
python3 "$HTTP_PROBE" --port "$PORT" --method HEAD --path /assets/hello.txt --expect-status 200 --expect-empty-body
python3 "$HTTP_PROBE" --port "$PORT" --method GET --path /assets/missing.txt --expect-status 200 --expect-body-contains 'psgi-fallback method=GET path=/assets/missing.txt' --expect-header 'x-upsgi-fallback=1'
python3 "$HTTP_PROBE" --port "$PORT" --method GET --path '/assets/../secret.txt' --expect-status 200 --expect-body-contains 'psgi-fallback method=GET path=/assets/../secret.txt' --reject-body-contains 'TOP-SECRET outside static root' --expect-header 'x-upsgi-fallback=1'

echo 'R4.1e static-map smoke passed'
