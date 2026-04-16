# A5 asset bundle

This bundle makes the `A5` upload-mixed workload scenario executable from repo-local assets.

Contents:
- `app_upload_mix.psgi` - serves a small GET endpoint plus an upload endpoint that reads the full request body.
- `config.yaml.in` - server config template for the scenario.
- `upload_matrix.json` - canonical upload sizes for smoke and later benchmark wiring.
- `upload_mix_client.py` - standard-library client that alternates small GETs with upload POSTs.
- `run_a5_smoke.sh` - lightweight runner that starts the server and exercises one mixed upload cycle.

The goal is to make the upload path concrete without waiting for the external application bundle.
