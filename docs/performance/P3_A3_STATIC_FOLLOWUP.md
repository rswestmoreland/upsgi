# P3 A3 Static Follow-up

This follow-up turns the A3 warm-cache result into a narrow optimization step.

Implemented changes:
- add `static_open_calls` and `static_open_failures` counters to core, stats, and metrics
- add a direct-file GET fast path that prefers `open()+fstat()` over `stat()+open()`
- keep HEAD and directory/index handling on the conservative stat path

Intended effect:
- direct warm-cache static GET hits can reuse the final opened fd
- path-cache benefits from W5 remain in place
- `stat()` stays for directory/index resolution and freshness-safe non-direct paths

Validation note:
- repo-local regression expectations were updated and a new `static_open_fastpath.t` regression was added
- full live rebuild/runtime validation was attempted, but the local validation environment was unstable during clean rebuilds in that pass

Crash follow-up note:
- the live rerun crash did not reproduce with the packaged pre-followup binary; it only appeared on the locally rebuilt binary used during the rerun attempt
- the backtrace landed in PSGI request parsing before any static fast-path work was exercised
- the most plausible cause is rebuild/object-layout drift after inserting new counters into the middle of `struct upsgi_core`
- as a defensive fix, the new `static_open_*` counters are now append-only fields at the end of `struct upsgi_core` so existing core field offsets remain stable across incremental rebuilds
