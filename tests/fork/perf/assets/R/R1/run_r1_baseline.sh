#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then echo "usage: $0 <binary> <artifact_dir> <work_dir>" >&2; exit 2; fi
binary="$1"
artifact_dir="$2"
work_dir="$3"
root_dir="$(cd "$(dirname "$0")/../../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
asset_common="$(cd "$asset_dir/../common" && pwd)"
config_tmpl="$asset_dir/config.yaml.in"
config_yaml="$work_dir/r1_baseline.yaml"
log_file="$artifact_dir/r1.log"
port="${PORT:-18091}"
stats_port="${STATS_PORT:-19091}"
queue_file="$artifact_dir/r1.queue"
mkdir -p "$artifact_dir" "$work_dir"
rm -f "$queue_file"
export PERF_R_QUEUE_FILE="$queue_file"
export PERF_R_APPEND_DELAY_MS="${PERF_R_APPEND_DELAY_MS:-0}"
sed -e "s#%(port)s#$port#g" -e "s#%(app)s#$asset_common/receiver_app.psgi#g" -e "s#%(log_file)s#$log_file#g" "$config_tmpl" > "$config_yaml"
printf '  stats: 127.0.0.1:%s\n  stats-http: true\n' "$stats_port" >> "$config_yaml"
"$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap '"$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
"$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /healthz
source "$artifact_dir/meta.env"
worker_pid="$UPSIG_PID"
stats_url="http://127.0.0.1:${stats_port}/"
bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "$stats_url" "$artifact_dir/p1_stats" 1 30 & p1_pid=$!
bash "$root_dir/tests/fork/perf/collectors/p4_smaps_rollup.sh" "$worker_pid" "$artifact_dir/p4_smaps_rollup.log" 1 30 & p4_pid=$!
bash "$root_dir/tests/fork/perf/collectors/p5_disk_profile.sh" "$artifact_dir/p5_disk_profile.log" 30 & p5_pid=$!
if command -v strace >/dev/null 2>&1; then bash "$root_dir/tests/fork/perf/collectors/p2_strace_fc.sh" "$worker_pid" "$artifact_dir/p2_strace_fc.txt" 30 & p2_pid=$!; else echo "strace unavailable on this host" > "$artifact_dir/p2_strace_fc.txt"; p2_pid=''; fi
if command -v perf >/dev/null 2>&1; then bash "$root_dir/tests/fork/perf/collectors/p3_perf_record.sh" "$worker_pid" "$artifact_dir/p3_perf.data" 30 & p3_pid=$!; else echo "perf unavailable on this host" > "$artifact_dir/p3_perf.data.txt"; p3_pid=''; fi
for size in 8192 32768 65536; do
    python3 "$asset_common/generate_payloads.py" --out "$work_dir/payload_${size}.json" --size "$size"
    python3 "$asset_common/receiver_bench_ab.py" --host 127.0.0.1 --port "$port" --path /push --payload "$work_dir/payload_${size}.json" --content-type application/json --concurrency 50 --requests 100 --out "$artifact_dir/r1_${size}.json" --raw-out "$artifact_dir/r1_${size}.ab.txt" > "$artifact_dir/r1_${size}.stdout"
done
wait "$p1_pid" || true
wait "$p4_pid" || true
wait "$p5_pid" || true
if [[ -n "$p2_pid" ]]; then wait "$p2_pid" || true; fi
if [[ -n "$p3_pid" ]]; then wait "$p3_pid" || true; fi
