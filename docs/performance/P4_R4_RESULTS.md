# P4-R4 Slow uploader flood summary

## 8192 bytes/sec
- uploaders: 8
- payload_bytes: 65536
- slow_successful: 8
- slow_failed: 0
- control_successful: 5
- control_failed: 0
- control latency p50/p95/max ms: 118.99 / 6053.46 / 7535.43
- slow elapsed p50/p95/max ms: 8061.19 / 8388.99 / 8427.78
- slow errors: []

## 65536 bytes/sec
- uploaders: 8
- payload_bytes: 65536
- slow_successful: 8
- slow_failed: 0
- control_successful: 5
- control_failed: 0
- control latency p50/p95/max ms: 67.85 / 1393.47 / 1713.84
- slow elapsed p50/p95/max ms: 1458.05 / 1782.21 / 1804.87
- slow errors: []

## 262144 bytes/sec
- uploaders: 8
- payload_bytes: 65536
- slow_successful: 8
- slow_failed: 0
- control_successful: 5
- control_failed: 0
- control latency p50/p95/max ms: 120.45 / 822.77 / 997.30
- slow elapsed p50/p95/max ms: 693.60 / 1047.08 / 1078.51
- slow errors: []

- max listen_queue: 0
- max listen_queue_errors: 0

## Interpretation
- Slow uploaders did not trigger listen-queue saturation in this focused run.
- Good control POSTs still succeeded during all three slow-upload rates.
- At 8 KiB/s, control latency degraded severely into multi-second territory even though requests still eventually succeeded.
- At 64 KiB/s and 256 KiB/s, control latency remained elevated but much lower than the 8 KiB/s case.
- This points to worker/socket occupancy and slow-body handling pressure rather than pure accept backlog.
