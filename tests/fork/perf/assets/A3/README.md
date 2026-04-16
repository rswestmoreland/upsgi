# A3 asset bundle

This bundle makes the `A3` static-fanout scenario executable from repo-local assets.

Contents:
- `app_static_fanout.psgi` - serves the HTML document and a tiny JSON endpoint.
- `config.yaml.in` - server config template for the scenario.
- `fanout_sequence.json` - canonical request order for one page-load sequence.
- `static/` - CSS, JS, and image-like assets faned out under `/assets`.
- `run_a3_smoke.sh` - lightweight runner that starts the server and performs a small warm/cold sequence.

The intent is not to be a full load generator. It is the scenario-specific asset pack that the generic perf harness can point at.
