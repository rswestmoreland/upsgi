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
a5_dir="$(cd "$asset_dir/../A5" && pwd)"
r2_dir="$(cd "$asset_dir/../R/R2" && pwd)"
r_common="$(cd "$asset_dir/../R/common" && pwd)"
mode="${X1_MODE:-s3}"
upload_profile="${X1_UPLOAD_PROFILE:-medium}"
receiver_profile="${X1_RECEIVER_PROFILE:-r2_10x_32768}"
append_delay_ms="${PERF_R_APPEND_DELAY_MS:-0}"

control_port="${X1_CONTROL_PORT:-18101}"
control_stats_port="${X1_CONTROL_STATS_PORT:-19101}"
upload_port="${X1_UPLOAD_PORT:-18105}"
upload_stats_port="${X1_UPLOAD_STATS_PORT:-19105}"
receiver_port="${X1_RECEIVER_PORT:-18109}"
receiver_stats_port="${X1_RECEIVER_STATS_PORT:-19109}"

mkdir -p "$artifact_dir" "$work_dir"
control_art="$artifact_dir/control_instance"
upload_art="$artifact_dir/upload_instance"
receiver_art="$artifact_dir/receiver_instance"
control_work="$work_dir/control_instance"
upload_work="$work_dir/upload_instance"
receiver_work="$work_dir/receiver_instance"
mkdir -p "$control_art" "$upload_art" "$receiver_art" "$control_work" "$upload_work" "$receiver_work"

make_control_config() {
python3 - <<PY
from pathlib import Path
asset_dir = Path(r"$a1_dir")
text = Path(asset_dir / 'config.yaml.in').read_text(encoding='utf-8')
text += '  stats: 127.0.0.1:%(stats_port)s\n'
text += '  stats-http: true\n'
out = text % {
    'port': $control_port,
    'app': str(asset_dir / 'app_browser_mix.psgi'),
    'static_root': str(asset_dir / 'static'),
    'log_file': str(Path(r"$control_art/control.log")),
    'stats_port': $control_stats_port,
}
Path(r"$control_work/control.yaml").write_text(out, encoding='utf-8')
PY
}

make_upload_config() {
  sed -e "s#%(port)s#$upload_port#g" \
      -e "s#%(app)s#$a5_dir/app_upload_mix.psgi#g" \
      -e "s#%(log_file)s#$upload_art/upload.log#g" \
      "$a5_dir/config.yaml.in" > "$upload_work/upload.yaml"
  printf '  stats: 127.0.0.1:%s\n  stats-http: true\n  limit-post: 33554432\n' "$upload_stats_port" >> "$upload_work/upload.yaml"
}

make_receiver_config() {
  sed -e "s#%(port)s#$receiver_port#g" \
      -e "s#%(app)s#$r_common/receiver_app.psgi#g" \
      -e "s#%(log_file)s#$receiver_art/receiver.log#g" \
      "$r2_dir/config.yaml.in" > "$receiver_work/receiver.yaml"
  printf '  stats: 127.0.0.1:%s\n  stats-http: true\n  limit-post: 1048576\n' "$receiver_stats_port" >> "$receiver_work/receiver.yaml"
}

start_instance() {
  local config="$1"; local art="$2"
  bash "$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config" "$art"
}

stop_instance() {
  local art="$1"
  bash "$root_dir/tests/fork/helpers/stop_server.sh" "$art" >/dev/null 2>&1 || true
}

make_control_config
make_upload_config
make_receiver_config

cleanup() {
  stop_instance "$control_art"
  stop_instance "$upload_art"
  stop_instance "$receiver_art"
}
trap cleanup EXIT

start_instance "$control_work/control.yaml" "$control_art"
start_instance "$upload_work/upload.yaml" "$upload_art"
export PERF_R_QUEUE_FILE="$receiver_art/x1.queue"
export PERF_R_APPEND_DELAY_MS="$append_delay_ms"
start_instance "$receiver_work/receiver.yaml" "$receiver_art"

bash "$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$control_port" /dashboard
bash "$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$upload_port" /small
bash "$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$receiver_port" /healthz

source "$control_art/meta.env"; control_pid="$UPSIG_PID"
source "$upload_art/meta.env"; upload_pid="$UPSIG_PID"
source "$receiver_art/meta.env"; receiver_pid="$UPSIG_PID"
control_stats_url="http://127.0.0.1:${control_stats_port}/"
upload_stats_url="http://127.0.0.1:${upload_stats_port}/"
receiver_stats_url="http://127.0.0.1:${receiver_stats_port}/"

bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "$control_stats_url" "$control_art/p1_stats" 1 45 & P1C=$!
bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "$upload_stats_url" "$upload_art/p1_stats" 1 45 & P1U=$!
bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "$receiver_stats_url" "$receiver_art/p1_stats" 1 45 & P1R=$!
bash "$root_dir/tests/fork/perf/collectors/p4_smaps_rollup.sh" "$control_pid" "$control_art/p4_smaps_rollup.log" 1 45 & P4C=$!
bash "$root_dir/tests/fork/perf/collectors/p4_smaps_rollup.sh" "$upload_pid" "$upload_art/p4_smaps_rollup.log" 1 45 & P4U=$!
bash "$root_dir/tests/fork/perf/collectors/p4_smaps_rollup.sh" "$receiver_pid" "$receiver_art/p4_smaps_rollup.log" 1 45 & P4R=$!
bash "$root_dir/tests/fork/perf/collectors/p5_disk_profile.sh" "$artifact_dir/p5_disk_profile.log" 45 & P5=$!

curl -s "$control_stats_url" > "$control_art/stats_before.json"
curl -s "$upload_stats_url" > "$upload_art/stats_before.json"
curl -s "$receiver_stats_url" > "$receiver_art/stats_before.json"

case "$upload_profile" in
  medium)
    upload_sizes=(524288); uploaders=(4); uploads_per_uploader=(2)
    ;;
  large4m)
    upload_sizes=(4194304); uploaders=(2); uploads_per_uploader=(1)
    ;;
  large16m)
    upload_sizes=(16777216); uploaders=(1); uploads_per_uploader=(1)
    ;;
  manymedium)
    upload_sizes=(524288 524288); uploaders=(4 4); uploads_per_uploader=(2 2)
    ;;
  *)
    echo "unknown X1_UPLOAD_PROFILE: $upload_profile" >&2; exit 2
    ;;
esac

case "$receiver_profile" in
  r2_4x_32768)
    python3 "$r_common/generate_ratio_payload.py" --out "$receiver_work/r2_payload.gz" --meta-out "$receiver_art/r2_payload_meta.json" --compressed-target 32768 --ratio-profile 4x > "$receiver_art/r2_payload.stdout"
    receiver_concurrency=20; receiver_requests=60
    ;;
  r2_10x_32768)
    python3 "$r_common/generate_ratio_payload.py" --out "$receiver_work/r2_payload.gz" --meta-out "$receiver_art/r2_payload_meta.json" --compressed-target 32768 --ratio-profile 10x > "$receiver_art/r2_payload.stdout"
    receiver_concurrency=10; receiver_requests=20
    ;;
  r2_10x_131072)
    python3 "$r_common/generate_ratio_payload.py" --out "$receiver_work/r2_payload.gz" --meta-out "$receiver_art/r2_payload_meta.json" --compressed-target 131072 --ratio-profile 10x > "$receiver_art/r2_payload.stdout"
    receiver_concurrency=4; receiver_requests=8
    ;;
  *)
    echo "unknown X1_RECEIVER_PROFILE: $receiver_profile" >&2; exit 2
    ;;
esac

python3 "$asset_dir/mixed_qos_bench.py" \
  --out "$artifact_dir/x1_measured.json" \
  --work-dir "$work_dir/mixed_qos" \
  --mode "$mode" \
  --control-script "$a1_dir/keepalive_mix_bench.py" \
  --control-host 127.0.0.1 --control-port "$control_port" \
  --control-sequence "$a1_dir/request_mix.json" \
  --control-clients 50 --control-iterations 4 --control-timeout 10 \
  --upload-script "$a5_dir/upload_mix_bench.py" \
  --upload-host 127.0.0.1 --upload-port "$upload_port" \
  --upload-sizes "${upload_sizes[@]}" \
  --uploaders "${uploaders[@]}" \
  --uploads-per-uploader "${uploads_per_uploader[@]}" \
  --control-gets 8 --control-interval-ms 200 --upload-timeout 120 \
  --receiver-script "$r_common/receiver_bench_ab.py" \
  --receiver-host 127.0.0.1 --receiver-port "$receiver_port" \
  --receiver-path /push --receiver-payload "$receiver_work/r2_payload.gz" \
  --receiver-content-type application/octet-stream \
  --receiver-concurrency "$receiver_concurrency" --receiver-requests "$receiver_requests" \
  --receiver-timeout 30 \
  > "$artifact_dir/x1_measured.stdout"

curl -s "$control_stats_url" > "$control_art/stats_after.json"
curl -s "$upload_stats_url" > "$upload_art/stats_after.json"
curl -s "$receiver_stats_url" > "$receiver_art/stats_after.json"

wait "$P1C" || true
wait "$P1U" || true
wait "$P1R" || true
wait "$P4C" || true
wait "$P4U" || true
wait "$P4R" || true
wait "$P5" || true
