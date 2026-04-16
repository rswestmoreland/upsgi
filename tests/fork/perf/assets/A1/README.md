# A1 asset bundle

This bundle makes the `A1` warm keepalive UI mix scenario executable from repo-local assets.

Contents:
- `app_browser_mix.psgi` - serves a small dashboard page, JSON APIs, and a tiny POST endpoint.
- `config.yaml.in` - server config template for the scenario.
- `request_mix.json` - canonical warm browser-like request sequence.
- `keepalive_mix_client.py` - standard-library client that reuses one HTTP connection for a request sequence.
- `run_a1_smoke.sh` - lightweight runner that starts the server and exercises the keepalive mix.
- `static/` - CSS, JS, and image-like assets mapped under `/assets`.

The goal is not to be a full browser benchmark. It is the scenario-specific asset pack that the generic perf harness can point at.
