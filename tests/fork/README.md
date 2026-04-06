# upsgi fork regression harness

This tree contains fork-specific regression, fault, and soak assets for upsgi.

## Current scope
The current checkpoint implements the maintained fork harness for regression, fault, soak, and Y1 measurement baseline capture:
- fixture apps
- fixture static assets
- profile rendering helper
- lifecycle/process helpers
- log assertion helpers
- first-pass regression tests
- second-pass regression tests
- retained hardening fault tests
- advisory soak scripts
- Y1 benchmark capture helpers

## Directory layout
- `fixtures/apps/` PSGI apps used by the harness
- `fixtures/static/` static files used for `--static-map` checks
- `helpers/` shell helpers for profile rendering and process control
- `lib/` shared Perl helper module for tests
- `regression/` first-pass and second-pass regression tests
- `fault/` startup and retained hardening tests
- `soak/` advisory burst and reload smoke checks
- `bench/` Y1 measurement baseline helpers

## Current regression tests
- `startup.t`
- `compatibility.t`
- `exceptions.t`
- `static.t`
- `logging.t`
- `upload.t`
- `reload.t`

## Current fault tests
- `bad_app_path.t`
- `unknown_option.t`
- `request_hardening.t`
- `psgi_hardening.t`

## Artifact contract
Each test creates its own artifact directory and preserves it on failure. The expected artifact set is:
- `server.log`
- `server.pid`
- `launch.cmd`
- `meta.env`
- optional request and response captures

## Profile source of truth
The regression harness reuses the fork-owned profile templates in `tests/fork/configs/`:
- `baseline.ini.in`
- `baseline_no_affinity.ini.in`
- `debug_exceptions.ini.in`
- `legacy_compatible.ini.in`

## Current soak scripts
- `soak/request_burst_smoke.sh`
- `soak/reload_cycle_smoke.sh`


## Harness normalization notes
- retained regression and fault tests now choose an available local port through `UpSGITest::pick_port()` instead of depending on a fixed shared port map
- retained lifecycle helpers now launch with an explicit `--pidfile` so reload and stop helpers target the real master pid

## Current benchmark helpers
- `bench/capture_baseline.sh` captures the maintained Y1 baseline matrix
- `bench/http_bench.py` performs sequential request timing and writes per-case JSON
- `bench/render_summary.py` renders a Markdown and JSON summary from captured case results
