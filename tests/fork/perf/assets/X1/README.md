# X1 asset bundle

Mixed-host QoS decision runner.

This bundle composes existing A-lane and R-lane traffic generators without changing
server-core behavior.

## Topology
- one upsgi instance serves the A1 control lane
- one upsgi instance serves the A5 upload lane
- one upsgi instance serves the receiver lane used for R2/R5-style pressure

All three run on the same host at the same time so the measurement focuses on host-level
resource contention and coexistence.

## Purpose
Use the same host to answer the X1 questions:
- can the control lane remain stable while uploads and ingest run at the same time?
- does mixed-host pain track body/read pressure, sink/append pressure, or both?
- are a few large bodies worse than many medium bodies with similar aggregate bytes?

## Reused assets
- `../A1/keepalive_mix_bench.py`
- `../A5/upload_mix_bench.py`
- `../R/common/receiver_bench_ab.py`
- `../R/common/generate_ratio_payload.py`

## Primary runner
- `run_x1_measured.sh`
- `mixed_qos_bench.py`

## Mode switches
The measured runner is controlled with environment variables:
- `X1_MODE=s1|s2|s3`
  - `s1`: control + uploads
  - `s2`: control + receiver
  - `s3`: control + uploads + receiver
- `X1_UPLOAD_PROFILE=medium|large4m|large16m|manymedium`
- `X1_RECEIVER_PROFILE=r2_4x_32768|r2_10x_32768|r2_10x_131072`
- `PERF_R_APPEND_DELAY_MS=<ms>` for sink-pressure discrimination

## Notes
This bundle prepares X1b only. It does not declare X1c-X1g complete until the measured
cases are executed and summarized.
