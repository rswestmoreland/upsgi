#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then
    echo "usage: $0 <binary> <artifact_dir> <work_dir>" >&2
    exit 2
fi
binary="$1"
artifact_dir="$2"
work_dir="$3"
root_dir="$(cd "$(dirname "$0")/../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
config_yaml="$work_dir/a1_http11_smoke.yaml"
log_file="$artifact_dir/a1_http11.log"
port="${PORT:-18082}"
mkdir -p "$artifact_dir" "$work_dir"
python3 - <<PY
from pathlib import Path
asset_dir = Path(r"$asset_dir")
text = Path(asset_dir / 'config_http11.yaml.in').read_text(encoding='utf-8')
out = text % {
    'port': int($port),
    'app': str(asset_dir / 'app_browser_mix.psgi'),
    'static_root': str(asset_dir / 'static'),
    'log_file': str(Path(r"$log_file")),
}
Path(r"$config_yaml").write_text(out, encoding='utf-8')
PY
"$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap '"$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
"$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /dashboard
python3 "$asset_dir/keepalive_mix_client.py" --host 127.0.0.1 --port "$port" --sequence "$asset_dir/request_mix.json"
