# P6 X1 Results

Status: X1c lane-stability matrix completed on the current codebase without server-core behavior changes.

## What was run
- `X1_MODE=s1` control lane + upload lane
- `X1_MODE=s2` control lane + receiver lane
- `X1_MODE=s3` control lane + upload lane + receiver lane

Profiles used in X1c:
- upload profile: `medium` (524288-byte uploads)
- receiver profile: `r2_10x_32768`
- append delay: `PERF_R_APPEND_DELAY_MS=0`

## Lane-stability results

### S1: control + uploads
Control lane:
- throughput requests/sec: 341.78
- request latency ms p50/p95/p99/max: 99.19 / 224.97 / 299.33 / 386.31
- request errors: 0

Upload lane:
- upload latency ms p50/p95/max: 2030.60 / 2935.94 / 3060.89
- control GET latency ms p50/p95/max inside upload lane: 2.21 / 2244.51 / 2865.09

Sampled listen queue:
- control instance max listen_queue: 9
- upload instance max listen_queue: 4
- listen_queue_errors max across sampled instances: 0

Interpretation:
- legitimate uploads still badly hurt latency inside the upload lane
- a separate control instance on the same host stays healthy enough to continue serving traffic
- this suggests body pressure is expensive but does not automatically collapse every lane on the host at this level

### S2: control + receiver
Control lane:
- throughput requests/sec: 361.41
- request latency ms p50/p95/p99/max: 102.44 / 194.63 / 206.65 / 284.03
- request errors: 0

Receiver lane:
- requests/sec: 3.93
- failed requests: 0
- mean request ms: 2545.63
- request ms p95/p99: 2551 / 2551

Sampled listen queue:
- control instance max listen_queue: 21
- receiver instance max listen_queue: 9
- listen_queue_errors max across sampled instances: 0

Interpretation:
- the control lane remains stable under receiver pressure
- receiver work is still slow and queue growth is visible
- this points to real host contention, but not an accept-backlog collapse

### S3: control + uploads + receiver
Control lane:
- throughput requests/sec: 261.28
- request latency ms p50/p95/p99/max: 114.86 / 297.16 / 382.03 / 497.58
- request errors: 0

Upload lane:
- upload latency ms p50/p95/max: 2628.48 / 3524.13 / 3695.47
- control GET latency ms p50/p95/max inside upload lane: 4.59 / 2830.97 / 3501.40

Receiver lane:
- requests/sec: 2.22
- failed requests: 0
- mean request ms: 4506.41
- request ms p95/p99: 5514 / 5514

Sampled listen queue:
- control instance max listen_queue: 8
- upload instance max listen_queue: 4
- receiver instance max listen_queue: 9
- listen_queue_errors max across sampled instances: 0

Interpretation:
- this is the strongest mixed-host signal so far
- all three lanes remain alive, but combined body-heavy and receiver-heavy work clearly reduces control-lane throughput and worsens control-lane tail latency
- the effect looks more like shared host-resource contention and long work residency than socket admission failure

## Most important conclusion from X1c
The host does not fail by dropping the control lane outright. Instead, coexistence degrades as more body-heavy and receiver-heavy work is active at the same time.

That supports the QoS direction:
- protect small/control traffic from long body work
- keep legitimate large bodies moving without data loss
- decide next whether ingress byte scheduling or sink isolation explains more of the shared-host pain

## What X1c does and does not prove
X1c proves:
- control traffic can survive alongside uploads and receiver traffic on the same host
- combined upload + receiver pressure is materially worse than either lane alone
- the degradation signature still does not look like pure listen-backlog collapse

X1c does not yet prove:
- whether read-side body service or sink-side append service is the dominant cause of the mixed-host harm
- whether a few very large bodies are worse than many medium bodies at similar aggregate bytes in flight

## Next steps
- X1d read-vs-sink discrimination matrix
- X1e few-large-vs-many-medium matrix
- X1f correlation pass
- X1g decision memo ranking ingress scheduling versus sink isolation

## X1d read-vs-sink discrimination matrix

Status: X1d completed on the current codebase without server-core behavior changes.

What was run:
- `X1_MODE=s3`
- upload profile: `medium` (524288-byte uploads)
- receiver profile: `r2_10x_32768`
- append delays:
  - D1: `PERF_R_APPEND_DELAY_MS=0`
  - D2: `PERF_R_APPEND_DELAY_MS=5`
  - D3: `PERF_R_APPEND_DELAY_MS=20`

### D1: mixed load, append delay 0 ms
Control lane:
- throughput requests/sec: 546.95
- request latency ms p50/p95/p99/max: 88.59 / 104.39 / 174.20 / 186.67

Upload lane:
- upload latency ms p50/p95: 1098.68 / 1168.15
- control GET latency ms p95 inside upload lane: 896.53

Receiver lane:
- requests/sec: 3.92
- mean request ms: 2553.87
- request ms p95: 2982

Sampled listen queue:
- control instance max listen_queue: 9
- receiver instance max listen_queue: 9

### D2: same mixed load, append delay 5 ms
Control lane:
- throughput requests/sec: 503.19
- request latency ms p50/p95/p99/max: 91.10 / 104.35 / 167.88 / 185.53

Upload lane:
- upload latency ms p50/p95: 1181.52 / 1306.49
- control GET latency ms p95 inside upload lane: 1052.93

Receiver lane:
- requests/sec: 3.58
- mean request ms: 2794.56
- request ms p95: 3206

Sampled listen queue:
- control instance max listen_queue: 4
- receiver instance max listen_queue: 9

### D3: same mixed load, append delay 20 ms
Control lane:
- throughput requests/sec: 520.83
- request latency ms p50/p95/p99/max: 90.76 / 103.75 / 172.97 / 187.86

Upload lane:
- upload latency ms p50/p95: 1161.39 / 1327.51
- control GET latency ms p95 inside upload lane: 1036.61

Receiver lane:
- requests/sec: 3.34
- mean request ms: 2992.14
- request ms p95: 3464

Sampled listen queue:
- control instance max listen_queue: 11
- receiver instance max listen_queue: 9

## Most important conclusion from X1d
Mixed-host control-lane harm did **not** track append delay strongly in this matrix.

Across 0 ms, 5 ms, and 20 ms append delay:
- control-lane p95 stayed roughly flat, around 104 ms
- control-lane p50 and p99 stayed in the same general range
- control-lane throughput moved, but not in a strong monotonic way

By contrast, receiver-lane service clearly worsened as append delay increased:
- requests/sec fell from 3.92 to 3.34
- receiver mean request time rose from 2553.87 ms to 2992.14 ms
- receiver p95 rose from 2982 ms to 3464 ms

Interpretation:
- sink pressure is real and should still be addressed
- but sink delay does **not** currently explain most of the control-lane degradation under mixed load
- the stronger first-design signal remains ingress/body scheduling and QoS-style protection of the control lane from long body work

## What X1d proves and does not prove
X1d proves:
- append delay worsens receiver-lane service time under mixed load
- append delay modestly worsens upload-lane p95 and upload-lane internal control GET p95
- control-lane tail latency does not blow up in proportion to append delay

X1d does not yet prove:
- whether a few large uploads are worse than many medium uploads at similar aggregate bytes in flight
- which predictor best explains tail latency once body-shape variation is introduced

## Next steps
- X1e few-large-vs-many-medium matrix
- X1f correlation pass
- X1g decision memo ranking ingress scheduling versus sink isolation
