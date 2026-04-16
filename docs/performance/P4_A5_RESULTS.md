# P4-A5 Upload-mixed web workload results

## 65536 bytes
- uploaders: 4
- uploads_per_uploader: 3
- control GETs ok/failed: 10/0
- control latency p50/p95/max: 1.99 / 35.43 / 60.37 ms
- upload POSTs ok/failed: 12/0
- upload latency p50/p95/max: 81.58 / 87.70 / 91.95 ms

## 524288 bytes
- uploaders: 4
- uploads_per_uploader: 2
- control GETs ok/failed: 10/0
- control latency p50/p95/max: 2.18 / 635.37 / 675.51 ms
- upload POSTs ok/failed: 8/0
- upload latency p50/p95/max: 799.78 / 858.90 / 878.16 ms

## 4194304 bytes
- uploaders: 2
- uploads_per_uploader: 1
- control GETs ok/failed: 10/0
- control latency p50/p95/max: 2.06 / 1838.17 / 2080.37 ms
- upload POSTs ok/failed: 2/0
- upload latency p50/p95/max: 2945.01 / 3728.59 / 3815.66 ms

## 16777216 bytes
- uploaders: 1
- uploads_per_uploader: 1
- control GETs ok/failed: 10/0
- control latency p50/p95/max: 2.31 / 4332.49 / 7854.47 ms
- upload POSTs ok/failed: 1/0
- upload latency p50/p95/max: 8050.03 / 8050.03 / 8050.03 ms

- max listen_queue: 4
- max listen_queue_errors: 0

## Interpretation
- Uploads did not cause accept backlog saturation in this focused run.
- Small control GETs remained fast at 64 KiB uploads, degraded noticeably by 512 KiB, and showed multi-second tail latency by 4 MiB and especially 16 MiB uploads.
- This points to app-lane request-body handling pressure rather than accept-path collapse.
