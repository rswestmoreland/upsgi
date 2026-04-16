# R4 asset bundle

Slow-uploader harness built from a shared raw-socket sender that trickles valid bodies to /push.

Contents:
- shared receiver app in `../common/receiver_app.psgi`
- shared corpus and client helpers in `../common/`
- `config.yaml.in` for the scenario
- `r4_matrix.json` for the scenario matrix
- `run_r4_smoke.sh` lightweight smoke runner
