# upsgi packaging and release layout

## Release objective
Ship a PSGI-first fork that is small enough to understand, predictable to
migrate to, and explicit about what is baseline versus what is merely retained
for compatibility.

## Release artifacts
Required for a public release refresh:
- `upsgi` binary built from the default PSGI-first profile
- source archive/tag for the exact release commit (for example `upsgi-0.1.2-source.tar.gz` when cutting the current public release)
- release checksum file generated for that source archive
- `README` and `README.md`
- `LICENSE`
- `CHANGELOG.md`
- `docs/upsgi/INDEX.md`
- `docs/upsgi/QUICKSTART.md`
- `docs/upsgi/OPTION_SURFACE.md`
- `docs/upsgi/LOGGING.md`
- `docs/upsgi/COMPATIBILITY.md`
- `docs/upsgi/MIGRATION.md`
- `docs/upsgi/REPO_LAYOUT.md`
- `docs/upsgi/DEFERRED_SCOPE.md`
- release notes and decision record under `docs/upsgi/releases/`
- `examples/upsgi/`
- `tests/fork/`
- `buildconf/default.toml`
- `buildconf/psgi.toml`

## Repo paths that define the release story
- build defaults: `buildconf/default.toml`, `buildconf/psgi.toml`
- baseline launch docs: `docs/upsgi/QUICKSTART.md`
- supported/deferred surface: `docs/upsgi/OPTION_SURFACE.md`,
  `docs/upsgi/DEFERRED_SCOPE.md`
- migration: `docs/upsgi/MIGRATION.md`
- release notes: `docs/upsgi/releases/`
- release helper scripts: `scripts/release/`
- shipped examples: `examples/upsgi/`
- shipped regression harness: `tests/fork/`

## Packaging layout
At tag time, package review should confirm this minimum story:

```text
upsgi/
  README
  README.md
  LICENSE
  CHANGELOG.md
  buildconf/
    default.toml
    minimal.toml
    psgi.toml
  dist/
    .gitignore
    # generated at release time, not stored as working-tree source
    upsgi-0.1.2-source.tar.gz
    upsgi-0.1.2-SHA256SUMS.txt
  scripts/
    release/
      make_source_release.sh
  docs/
    upsgi/
      INDEX.md
      QUICKSTART.md
      OPTION_SURFACE.md
      LOGGING.md
      COMPATIBILITY.md
      MIGRATION.md
      REPO_LAYOUT.md
      DEFERRED_SCOPE.md
      PSGI_BOUNDARY.md
      LOGGING_BOUNDARY.md
      REQUEST_STATIC_BOUNDARY.md
      releases/
        0.1.2.md
        0.1.2-decision.md
  examples/
    upsgi/
      app.psgi
      baseline.ini
      debug_exceptions.ini
      migration_legacy.ini
      public/
        hello.txt
  tests/
    fork/
      regression/
      fault/
      soak/
      fixtures/
      helpers/
      lib/
```

## Packaging exclusions
Do not include transient local artifacts in a release archive:
- `tests/fork/artifacts/`
- local build logs such as `uwsgibuild.log`
- local workspace-only linker workarounds
- temporary pidfiles, sockets, and log captures generated during testing

## Build note
The current release story assumes the default `make` path resolves to the default
PSGI-first build profile. Release notes should not claim support parity with the
full upstream multi-runtime build matrix.

## RC soak policy
- RC1 keeps soak scripts advisory so release blocking remains centered on deterministic regression and fault coverage.
- RC2 promotes only `tests/fork/soak/request_burst_smoke.sh` and `tests/fork/soak/reload_cycle_smoke.sh` to required gates.
- broader long-run or mixed-traffic soaks remain advisory until later release cycles.
