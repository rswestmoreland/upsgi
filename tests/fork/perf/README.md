# upsgi performance harness seed

This tree packages the scenario manifests, collector templates, and seed validation assets for the performance/scalability program.

## Current scope
- machine-readable scenario manifests for A1-R6-X4
- manifest index and schema
- dry-run manifest runner
- collector wrapper templates for P1-P5
- carried-forward W4 validation seed assets
- execution checklist and descriptive plan docs

## What is ready now
- P0-H1 through P0-H4 are defined in-repo
- P0-H7 dry-run manifest emission is runnable
- W4 validation-runner, hash-check, and slow-reader seed assets are preserved in `seeds/w4_runtime_validation/`

## Current execution status
- A-lane repo-local scenario asset packs are packaged under `assets/`.
- R-lane receiver asset packs are packaged under `assets/R/`.
- Measured baseline runners exist for A1 and R1.
- Focused measured early-chokepoint runners now exist for R2 and R6.
- collector wrappers are still host-dependent and may degrade gracefully when tools like `strace`, `perf`, or working disk samplers are unavailable

## Typical dry-run usage
- `python3 tests/fork/perf/run_manifest.py --scenario A3 --output-root tests/fork/artifacts/perf_dryrun --dry-run`
- `python3 tests/fork/perf/run_manifest.py --all --output-root tests/fork/artifacts/perf_dryrun --dry-run`

A dry run creates a scenario directory with:
- `manifest.lock.json`
- `run_plan.md`
- `collector_contract.json`
- `commands.todo.txt`

These outputs are intended to make the next execution step explicit before host-specific traffic tooling is attached.


A2 status update: repo-local A2 asset pack is packaged and measured runner is available.

A3 status update: focused measured cold-vs-warm fanout run completed. The measured runner captures before/after stats snapshots so static cache hit/miss, realpath, stat, and index-check deltas can be interpreted directly.


R3 measured runner added with a conservative size-scaled body matrix.


R4 measured runner available at tests/fork/perf/assets/R/R4/run_r4_measured.sh.


A6 status update: focused measured slow-reader run completed with the W4 async getline app; control GETs stayed fast while slow readers drained.


R5 status update: measured append-pressure runner added with bounded delay cases.


R5 status update: measured append-pressure run completed and packaged.

X1 status update: mixed-host QoS runner is now composed from A1, A5, and R2/R5 assets; measured execution is the next step.
