#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then echo "usage: $0 <binary> <artifact_dir> <work_dir>" >&2; exit 2; fi
binary="$1"; artifact_dir="$2"; work_dir="$3"
root_dir="$(cd "$(dirname "$0")/../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
config_yaml="$work_dir/a5_measured.yaml"
log_file="$artifact_dir/a5.log"
port="${PORT:-18085}"; stats_port="${STATS_PORT:-19085}"
mkdir -p "$artifact_dir" "$work_dir"
sed -e "s#%(port)s#$port#g" -e "s#%(app)s#$asset_dir/app_upload_mix.psgi#g" -e "s#%(log_file)s#$log_file#g" "$asset_dir/config.yaml.in" > "$config_yaml"
printf '  stats: 127.0.0.1:%s\n  stats-http: true\n  limit-post: 33554432\n' "$stats_port" >> "$config_yaml"
bash "$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap 'bash "$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
bash "$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /small
source "$artifact_dir/meta.env"
worker_pid="$UPSIG_PID"
stats_url="http://127.0.0.1:${stats_port}/"
bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "$stats_url" "$artifact_dir/p1_stats" 1 60 & p1_pid=$!
bash "$root_dir/tests/fork/perf/collectors/p4_smaps_rollup.sh" "$worker_pid" "$artifact_dir/p4_smaps_rollup.log" 1 60 & p4_pid=$!
bash "$root_dir/tests/fork/perf/collectors/p5_disk_profile.sh" "$artifact_dir/p5_disk_profile.log" 60 & p5_pid=$!
if command -v strace >/dev/null 2>&1; then bash "$root_dir/tests/fork/perf/collectors/p2_strace_fc.sh" "$worker_pid" "$artifact_dir/p2_strace_fc.txt" 60 & p2_pid=$!; else echo 'strace unavailable on this host' > "$artifact_dir/p2_strace_fc.txt"; p2_pid=''; fi
if command -v perf >/dev/null 2>&1; then bash "$root_dir/tests/fork/perf/collectors/p3_perf_record.sh" "$worker_pid" "$artifact_dir/p3_perf.data" 60 & p3_pid=$!; else echo 'perf unavailable on this host' > "$artifact_dir/p3_perf.data.txt"; p3_pid=''; fi
curl -s "$stats_url" > "$artifact_dir/stats_before.json"
python3 "$asset_dir/upload_mix_bench.py" --host 127.0.0.1 --port "$port" --sizes 65536 524288 4194304 16777216 --uploaders 4 4 2 1 --uploads-per-uploader 3 2 1 1 --control-gets 10 --control-interval-ms 200 --out "$artifact_dir/a5_measured.json" > "$artifact_dir/a5_measured.stdout"
curl -s "$stats_url" > "$artifact_dir/stats_after.json"
wait "$p1_pid" || true
wait "$p4_pid" || true
wait "$p5_pid" || true
if [[ -n "$p2_pid" ]]; then wait "$p2_pid" || true; fi
if [[ -n "$p3_pid" ]]; then wait "$p3_pid" || true; fi
