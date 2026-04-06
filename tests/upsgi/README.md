# upsgi legacy smoke assets

This directory is retained for the older R4 checkpoint smoke assets.
It is not the authoritative fork regression harness.

## Current asset sets
### R4.1d logging verification
- `apps/logging_smoke.psgi`
- `helpers/udp_log_collector.py`
- `r4_1d_logging_smoke.sh`

Purpose:
- bundled logger availability
- forwarded-for request logging
- localized PSGI exception stack logging

### R4.1e static-map verification
- `apps/static_fallback.psgi`
- `helpers/http_probe.py`
- `r4_1e_static_map_smoke.sh`

Purpose:
- prove `--static-map` remains a core-path baseline feature
- verify GET and HEAD behavior for mapped files
- verify missing file fallback reaches the PSGI app
- verify path traversal does not leak files outside the mapped root

### R4.1f baseline config validation
- `r4_1f_baseline_config_smoke.sh`

Purpose:
- verify the retained baseline profile shapes
- verify compatibility-only flags remain inert
- verify debug exception visibility is opt-in
- verify static-map and basic request flow stay stable across baseline configs

Note:
- the authoritative fork-owned profile templates now live under `tests/fork/configs/`
- `tests/upsgi/` remains only for these retained legacy smoke assets

## Authoritative fork test path
- `tests/fork/` contains the maintained fork regression, fault, and soak harness
- `tests/fork/configs/` is the profile source of truth for maintained fork tests
