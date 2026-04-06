# upsgi repository layout

This document defines the authoritative maintained layout for the fork.

## Top-level entry points
- `README.md` - primary human-facing project overview
- `README` - plain-text overview for environments that prefer a non-Markdown entry point
- `INSTALL` - minimal build and local validation instructions
- `CHANGELOG.md` - release-oriented change history
- `LICENSE` - licensing terms

## Build and release entry points
- `Makefile` - default build entry point
- `upsgiconfig.py` - canonical Python build/config engine
- `buildconf/default.toml` - default PSGI-first profile
- `buildconf/psgi.toml` - explicit PSGI-first profile
- `buildconf/minimal.toml` - retained shared minimal base profile
- `scripts/release/` - normalized release-helper scripts
- `dist/` - normalized release-output directory (generated artifacts are created at release time and stay ignored in the source tree)

## Documentation and examples
- `docs/upsgi/` - authoritative fork documentation set
- `examples/upsgi/` - shipped example configs and sample PSGI app

## Tests
- `tests/fork/` - authoritative fork regression, fault, and soak harness
- `tests/upsgi/` - retained legacy R4 smoke assets only
- `t/perl/`, `t/core/`, `t/static/` - retained upstream-era test material still kept for reference/use

## Implementation tree retained by the fork
- `core/` - retained core runtime and request/logging/static logic
- `proto/` - retained protocol/front-door implementation files
- `plugins/psgi/` - PSGI runtime and Perl bridge
- `plugins/logfile/`, `plugins/logsocket/`, `plugins/rsyslog/` - retained bundled logging sinks
- `lib/` - retained low-level helper code still used by the build/runtime
- `upsgi.h`, `upsgi_main.c` - current internal entry-point/header names
- `uwsgi.h` - temporary compatibility shim that includes `upsgi.h`

Unsupported upstream standalone plugin directories and the old external installer helper are no longer part of the maintained fork tree.

## Intentionally retained legacy/internal names
Some retained internal files still use `uwsgi` in the filename or symbol prefix. These are documented in `RETAINED_LEGACY_REFERENCES.md` and remain intentionally out of scope for the narrow public-facing rename sweep.

## Generated paths that are not part of the authoritative repo layout
These may appear during local builds or test runs but should not be treated as source-of-truth tree content:
- `upsgi`
- `bin/uwsgi`
- `uwsgibuild.*`
- `.local-lib/` local linker workarounds
- `tests/fork/artifacts/*`
- `dist/*`
- object files and generated `core/config_py.c` / `core/dot_h.c`
