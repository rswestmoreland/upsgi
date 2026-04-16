#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/../../../../.." && pwd)
ASSET_DIR=$(cd "$(dirname "$0")" && pwd)
PORT=${PORT:-18081}
LOG_FILE=${LOG_FILE:-$ASSET_DIR/a1_smoke.log}
CFG_FILE=${CFG_FILE:-$ASSET_DIR/a1_smoke.yaml}
SERVER_BIN=${SERVER_BIN:-$ROOT_DIR/upsgi}

python3 - <<PY
from pathlib import Path
src = Path(r"$ASSET_DIR/config.yaml.in").read_text(encoding='utf-8')
out = src % {
    'port': $PORT,
    'app': str(Path(r"$ASSET_DIR/app_browser_mix.psgi")),
    'static_root': str(Path(r"$ASSET_DIR/static")),
    'log_file': str(Path(r"$LOG_FILE")),
}
Path(r"$CFG_FILE").write_text(out, encoding='utf-8')
PY

"$SERVER_BIN" --yaml "$CFG_FILE" &
SERVER_PID=$!
trap 'kill "$SERVER_PID" >/dev/null 2>&1 || true; wait "$SERVER_PID" >/dev/null 2>&1 || true' EXIT
sleep 1
python3 "$ASSET_DIR/keepalive_mix_client.py" --port "$PORT" --sequence "$ASSET_DIR/request_mix.json"
