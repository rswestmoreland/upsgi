# P5 R5 Queue-file append saturation results

## append delay 0 ms
- requests/sec: 34.99
- mean request ms: 914.521
- request p50/p95/p99 ms: 888 / 971 / 990
- control posts ok/total: 10/10
- control latency p50/p95/max ms: 855.1618660001168 / 940.871994950021 / 961.5574459999152
- failed requests: 0
- queue lines written: 266
- max listen_queue: 32
- max listen_queue_errors: 0

## append delay 20 ms
- requests/sec: 20.24
- mean request ms: 790.4
- request p50/p95/p99 ms: 785 / 808 / 841
- control posts ok/total: 10/10
- control latency p50/p95/max ms: 758.5181740000735 / 803.2257827499961 / 820.1368660002117
- failed requests: 0
- queue lines written: 138
- max listen_queue: 16
- max listen_queue_errors: 0

## append delay 50 ms
- requests/sec: 11.73
- mean request ms: 682.148
- request p50/p95/p99 ms: 680 / 720 / 728
- control posts ok/total: 10/10
- control latency p50/p95/max ms: 599.4434080000701 / 710.4040686999497 / 714.2060529999981
- failed requests: 0
- queue lines written: 74
- max listen_queue: 8
- max listen_queue_errors: 0

## append delay 5 ms
- requests/sec: 29.82
- mean request ms: 1072.93
- request p50/p95/p99 ms: 1053 / 1115 / 1127
- control posts ok/total: 10/10
- control latency p50/p95/max ms: 1021.5003279997745 / 1091.6503305001015 / 1099.5718200001647
- failed requests: 0
- queue lines written: 266
- max listen_queue: 32
- max listen_queue_errors: 0

## Interpretation
- Throughput declined as append delay increased, which is the clearest sign that the sink path can dominate even when request bodies are moderate and valid.
- Control POSTs continued to succeed in every case, but their latency stayed high and roughly tracked the append-bound request cost, indicating worker time was being consumed by append completion rather than accept backlog.
- listen_queue rose under load but never produced listen_queue_errors, so this focused R5 run still looked more like sink/worker serialization than socket admission failure.
- This supports the Kafka/syslog-ng-inspired idea that bounded sink backpressure or sink isolation would be the next meaningful design direction on the receiver lane.
