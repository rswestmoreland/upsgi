# A4 asset bundle

This bundle makes the `A4` download-heavy path scenario executable from repo-local assets.

Contents:
- `app_download_mix.psgi` - serves small control endpoints plus app-generated downloads at 256 KiB, 1 MiB, and 8 MiB.
- `config.yaml.in` - server config template for the scenario.
- `download_matrix.json` - canonical download sizes and request mix.
- `download_mix_client.py` - standard-library client that alternates small requests with download requests.
- `run_a4_smoke.sh` - lightweight runner that starts the server and exercises one mixed cycle.

The goal is to make the app-served download path concrete before broader fairness benchmarking starts.
