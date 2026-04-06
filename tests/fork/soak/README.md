# upsgi fork soak scripts

This directory contains longer-running shell smoke loops for:
- repeated request handling
- repeated reload acceptance

Current scripts:
- `request_burst_smoke.sh`
- `reload_cycle_smoke.sh`


Environment knobs:
- `UPSGI_SOAK_BURST_COUNT` adjusts request count for `request_burst_smoke.sh`
- `UPSGI_SOAK_RELOAD_CYCLES` adjusts reload count for `reload_cycle_smoke.sh`
- `UPSGI_SOAK_PORT` and `UPSGI_SOAK_RELOAD_PORT` override the default ports
- `UPSGI_BIN` overrides the binary path

Note: the soak scripts now resolve the repo root from `tests/fork/soak/` to the repository top-level, so the default binary path points at the built `upsgi` executable in the repo root.


Default gate sizes:
- `request_burst_smoke.sh` defaults to 25 requests
- `reload_cycle_smoke.sh` defaults to 3 reload cycles
- reload wait polling is intentionally longer because master reload and worker respawn can take several seconds even on a healthy build

Process handling notes:
- both soak scripts now use a pidfile so signal delivery and cleanup target the real master process
- the reload soak uses longer wait polling because healthy master reload can take several seconds before the worker is serving again

- Cleanup now escalates INT -> TERM -> KILL so advisory and RC soak runs do not hang on a stubborn master process.
