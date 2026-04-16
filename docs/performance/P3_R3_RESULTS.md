# P3-R3 large body matrix summary

- Runner return code: 0
- Max listen_queue: 0
- Max listen_queue_errors: 0

## Primary matrix cases
- r3_compressed_262144: 3.59 req/s, mean 1113.763 ms, p95 1106 ms, failed 0, concurrency 4, requests 8
- r3_uncompressed_262144: 3.43 req/s, mean 1165.72 ms, p95 1231 ms, failed 0, concurrency 4, requests 8
- r3_uncompressed_1048576: 0.86 req/s, mean 2325.095 ms, p95 2312 ms, failed 0, concurrency 2, requests 4

## Supplemental one-off probes
- manual_compressed_1048576: 0.9 req/s, mean 1114.247 ms, p95 1164 ms, failed 0, concurrency 1, requests 2
- manual_compressed_4194304: 0.23 req/s, mean 4302.446 ms, p95 None ms, failed 0, concurrency 1, requests 1
- manual_uncompressed_4194304: 0.22 req/s, mean 4552.442 ms, p95 None ms, failed 0, concurrency 1, requests 1
- manual_compressed_16777216: 0.06 req/s, mean 17852.071 ms, p95 None ms, failed 0, concurrency 1, requests 1
- manual_uncompressed_16777216: 0.06 req/s, mean 17816.982 ms, p95 None ms, failed 0, concurrency 1, requests 1

## Interpretation
- The dominant cost in this focused R3 work is body handling and application-side full-body buffering, not accept backlog.
- Around 1 MiB, per-request time is already above one second even at very low concurrency.
- Around 4 MiB, single requests are in the 4.3 to 4.6 second range.
- At 16 MiB, even a single request takes about 17.8 seconds in both compressed and uncompressed form.
- The compressed-vs-uncompressed difference is small at these larger target sizes because this receiver path still reads the full request body and, for application/octet-stream, inflates it fully in memory before JSON decode.
- This strengthens the earlier R2 finding: the next receiver-side scalability work should focus on body-path limits, buffering policy, and overload handling rather than accept-path tuning.

## Notes
- The primary matrix was scaled down to fit the sandbox and completed 256 KiB and one 1 MiB case directly.
- Missing 1 MiB compressed, 4 MiB, and 16 MiB points were filled with isolated one-off probes at lower request counts.
- This is enough to identify the body-size breakpoint trend, but not enough to compare all sizes under one identical load pattern.
