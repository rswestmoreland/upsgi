# A6 asset bundle

This bundle makes the `A6` slow-reader scenario executable from repo-local assets.

Contents:
- `app_async_getline.psgi` - copied from the accepted W4 validation app so the scenario reuses the same PSGI response shape.
- `config.yaml.in` - server config template.
- `run_a6_smoke.sh` - starts the server, launches one slow reader, and drives normal `/small` traffic in parallel.
- `normal_mix_requests.txt` - small-request mix used by the smoke runner.

The goal is to turn the W4 validation seed into a reusable A6 scenario asset pack.
