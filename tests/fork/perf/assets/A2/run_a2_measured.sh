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
a1_dir="$(cd "$asset_dir/../A1" && pwd)"
config_yaml="$work_dir/a2_measured.yaml"
log_file="$artifact_dir/a2.log"
port="${PORT:-18082}"
stats_port="${STATS_PORT:-19082}"
mkdir -p "$artifact_dir" "$work_dir"
python3 - <<PY
from pathlib import Path
asset_dir = Path(r"$asset_dir")
a1_dir = Path(r"$a1_dir")
text = Path(asset_dir / 'config.yaml.in').read_text(encoding='utf-8')
text += '  stats: 127.0.0.1:%(stats_port)s\n'
text += '  stats-http: true\n'
out = text % {
    'port': int(r"$port"),
    'app': str(a1_dir / 'app_browser_mix.psgi'),
    'static_root': str(a1_dir / 'static'),
    'log_file': str(Path(r"$log_file")),
    'stats_port': int(r"$stats_port"),
}
Path(r"$config_yaml").write_text(out, encoding='utf-8')
PY
"$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap '"$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
"$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /dashboard
source "$artifact_dir/meta.env"
worker_pid="$UPSIG_PID"
stats_url="http://127.0.0.1:${stats_port}/"
bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "$stats_url" "$artifact_dir/p1_stats" 1 20 & p1_pid=$!
bash "$root_dir/tests/fork/perf/collectors/p4_smaps_rollup.sh" "$worker_pid" "$artifact_dir/p4_smaps_rollup.log" 1 20 & p4_pid=$!
bash "$root_dir/tests/fork/perf/collectors/p5_disk_profile.sh" "$artifact_dir/p5_disk_profile.log" 20 & p5_pid=$!
if command -v strace >/dev/null 2>&1; then bash "$root_dir/tests/fork/perf/collectors/p2_strace_fc.sh" "$worker_pid" "$artifact_dir/p2_strace_fc.txt" 20 & p2_pid=$!; else echo "strace unavailable on this host" > "$artifact_dir/p2_strace_fc.txt"; p2_pid=''; fi
if command -v perf >/dev/null 2>&1; then bash "$root_dir/tests/fork/perf/collectors/p3_perf_record.sh" "$worker_pid" "$artifact_dir/p3_perf.data" 20 & p3_pid=$!; else echo "perf unavailable on this host" > "$artifact_dir/p3_perf.data.txt"; p3_pid=''; fi
python3 "$asset_dir/cold_connection_bench.py" \
  --host 127.0.0.1 \
  --port "$port" \
  --sequence "$a1_dir/request_mix.json" \
  --rates 200 1000 3000 \
  --durations 5 5 3 \
  --max-workers 256 \
  --out "$artifact_dir/a2_measured_client.json" \
  > "$artifact_dir/a2_measured_client.stdout" || true
wait "$p1_pid" || true
wait "$p4_pid" || true
wait "$p5_pid" || true
if [[ -n "$p2_pid" ]]; then wait "$p2_pid" || true; fi
if [[ -n "$p3_pid" ]]; then wait "$p3_pid" || true; fi
