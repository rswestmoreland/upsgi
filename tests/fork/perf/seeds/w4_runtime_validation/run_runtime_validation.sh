#!/usr/bin/env bash
set -euo pipefail

APP="${1:-async_getline_validation_app.psgi}"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-9090}"

echo "[1] Fast correctness check"
python3 check_async_getline_hash.py --host "$HOST" --port "$PORT" --chunks 32 --chunk-size 65536 --label alpha

echo "[2] Slow-reader correctness check"
python3 check_async_getline_hash.py --host "$HOST" --port "$PORT" --chunks 16 --chunk-size 65536 --label slow --read-size 4096 --delay-ms 25

echo "[3] Small response sanity"
curl -fsS "http://${HOST}:${PORT}/small" >/dev/null

echo "[4] Mixed pressure example"
python3 slow_reader_client.py --host "$HOST" --port "$PORT" --chunks 64 --chunk-size 65536 --label slow --read-size 4096 --delay-ms 50 &
SLOW_PID=$!
for _ in $(seq 1 50); do
    curl -fsS "http://${HOST}:${PORT}/small" >/dev/null
done
wait "$SLOW_PID"

echo "validation-pack run complete"
