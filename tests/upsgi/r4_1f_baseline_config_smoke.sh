#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
UPSGI_BIN="${UPSGI_BIN:-${REPO_DIR}/upsgi}"
HTTP_PROBE="$REPO_DIR/tests/upsgi/helpers/http_probe.py"
CONFIG_DIR="$REPO_DIR/tests/fork/configs"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/upsgi-config-smoke.XXXXXX")"
PORT_BASE="${UPSGI_TEST_PORT_BASE:-19300}"

cleanup() {
    set +e
    for pid_file in "$WORK_DIR"/*.pid; do
        [[ -f "$pid_file" ]] || continue
        kill -INT "$(cat "$pid_file")" >/dev/null 2>&1 || true
        sleep 1
    done
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

if [[ ! -x "$UPSGI_BIN" ]]; then
    echo "missing executable upsgi binary: $UPSGI_BIN" >&2
    echo "build the fork first, then rerun this smoke harness" >&2
    exit 2
fi

mkdir -p "$WORK_DIR/static-root"
printf 'hello from static-map\n' > "$WORK_DIR/static-root/hello.txt"

render_config() {
    local template="$1"
    local target="$2"
    local port="$3"
    local app="$4"
    local log_file="$5"
    python3 - "$template" "$target" "$port" "$app" "$WORK_DIR/static-root" "$log_file" <<'PY'
import pathlib
import sys
template = pathlib.Path(sys.argv[1]).read_text()
target = pathlib.Path(sys.argv[2])
port = sys.argv[3]
app = sys.argv[4]
static_root = sys.argv[5]
log_file = sys.argv[6]
target.write_text(template % {
    "port": port,
    "app": app,
    "static_root": static_root,
    "log_file": log_file,
})
PY
}

start_profile() {
    local cfg="$1"
    local pid_file="$2"
    local log_file="$3"
    "$UPSGI_BIN" --ini "$cfg" --pidfile "$pid_file" >/dev/null 2>&1 &
    for _ in $(seq 1 40); do
        if [[ -f "$pid_file" ]]; then
            break
        fi
        sleep 0.25
    done
    if [[ ! -f "$pid_file" ]]; then
        echo "upsgi failed to start config=$cfg" >&2
        [[ -f "$log_file" ]] && cat "$log_file" >&2
        exit 1
    fi
}

stop_profile() {
    local pid_file="$1"
    if [[ -f "$pid_file" ]]; then
        kill -INT "$(cat "$pid_file")" >/dev/null 2>&1 || true
        rm -f "$pid_file"
        sleep 1
    fi
}

assert_static_and_fallback() {
    local port="$1"
    python3 "$HTTP_PROBE" --port "$port" --method GET --path /assets/hello.txt --expect-status 200 --expect-body $'hello from static-map\n'
    python3 "$HTTP_PROBE" --port "$port" --method GET --path /assets/missing.txt --expect-status 200 --expect-body-contains 'psgi-fallback method=GET path=/assets/missing.txt'
}

assert_debug_exception_log() {
    local port="$1"
    local log_file="$2"
    python3 - "$port" <<'PY'
import http.client
import sys
port = int(sys.argv[1])
conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
conn.request("GET", "/error")
res = conn.getresponse()
res.read()
conn.close()
PY
    sleep 1
    grep -Eq 'Devel::StackTrace|logging smoke requested failure' "$log_file"
}

run_profile() {
    local name="$1"
    local template="$2"
    local app="$3"
    local port="$4"
    local cfg="$WORK_DIR/$name.ini"
    local pid_file="$WORK_DIR/$name.pid"
    local log_file="$WORK_DIR/$name.log"

    render_config "$template" "$cfg" "$port" "$app" "$log_file"
    start_profile "$cfg" "$pid_file" "$log_file"
    assert_static_and_fallback "$port"

    if [[ "$name" == "legacy_compatible" ]]; then
        grep -q '^http-modifier1 = 5$' "$cfg"
        grep -q '^perl-no-die-catch = true$' "$cfg"
    fi

    if [[ "$name" == "debug_exceptions" ]]; then
        assert_debug_exception_log "$port" "$log_file"
    fi

    stop_profile "$pid_file"
    echo "profile $name passed"
}

run_profile legacy_compatible "$CONFIG_DIR/legacy_compatible.ini.in" "$REPO_DIR/tests/upsgi/apps/static_fallback.psgi" "$((PORT_BASE + 1))"
run_profile baseline "$CONFIG_DIR/baseline.ini.in" "$REPO_DIR/tests/upsgi/apps/static_fallback.psgi" "$((PORT_BASE + 2))"
run_profile baseline_no_affinity "$CONFIG_DIR/baseline_no_affinity.ini.in" "$REPO_DIR/tests/upsgi/apps/static_fallback.psgi" "$((PORT_BASE + 3))"
run_profile debug_exceptions "$CONFIG_DIR/debug_exceptions.ini.in" "$REPO_DIR/tests/upsgi/apps/logging_smoke.psgi" "$((PORT_BASE + 4))"

echo "R4.1f baseline config validation passed"
