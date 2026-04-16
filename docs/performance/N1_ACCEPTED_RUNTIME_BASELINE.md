# N1 accepted runtime baseline

This document locks the current accepted runtime baseline before broader roadmap
work resumes.

## Protected behavior

- W4 PSGI slow-reader redesign remains accepted and protected.
- W5 narrow static-serving improvements remain accepted and protected.
- B2c remains the only active request-body scheduler candidate.

## Baseline posture

- B2d, B2e, and B2f are measurement history only.
- The current N1 snapshot does not treat them as active runtime baselines.
- The B2c scheduler is now part of the maintained default runtime posture.
- Disable it only for controlled comparison through `--disable-body-scheduler`
  or `disable-body-scheduler: true`.

## Repo cleanup performed for N1

- Removed generated performance work directories.
- Removed generated regression artifact directories.
- Removed repo-local temporary result directories from prior body and static runs.
- Removed stale validation text files and build-lastprofile residue that did not
  belong to the stabilized source snapshot.
- Added a minimal operator-doc spine so quickstart and config references resolve
  inside the repo.

## Protected regression expectations

The stabilized baseline should continue to keep these areas green:
- request-body scheduler observability and gate coverage
- static-serving regression coverage
- upload regression coverage
- PSGI runtime and startup coverage already in `tests/fork/regression`

## Next roadmap posture

After N1, the broader roadmap should move on to:
- N2 config and operator-surface cleanup
- N3 CLI and documentation completion
- N4 namespace and repo cleanup completion
- N5 hardening
- N6 observability and profiling surface
- N7 packaging, migration, and release preparation
