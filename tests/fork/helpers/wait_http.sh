#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <host> <port> [path] [attempts] [sleep_seconds]" >&2
    exit 2
}

[[ $# -ge 2 && $# -le 5 ]] || usage
host="$1"
port="$2"
path="${3:-/}"
attempts="${4:-100}"
sleep_seconds="${5:-0.10}"

python3 - "$host" "$port" "$path" "$attempts" "$sleep_seconds" <<'PY'
import http.client
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
path = sys.argv[3]
attempts = int(sys.argv[4])
sleep_seconds = float(sys.argv[5])

for _ in range(attempts):
    try:
        conn = http.client.HTTPConnection(host, port, timeout=1.0)
        conn.request('GET', path)
        resp = conn.getresponse()
        resp.read()
        sys.exit(0)
    except Exception:
        time.sleep(sleep_seconds)

sys.exit(1)
PY
