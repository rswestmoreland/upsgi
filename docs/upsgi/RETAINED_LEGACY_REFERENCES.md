# Retained legacy references

upsgi is a PSGI-first fork of the upstream uWSGI codebase. This document locks the
legacy references that are intentionally still present after the public-facing
rename sweep.

The goal is to prevent future cleanup from turning into a blind global rename.
Each retained reference should fit one of these buckets:
- compatibility retention
- provenance retention
- deferred internal rename

## Compatibility-retained references

These references remain because changing them would alter supported config,
Perl, or migration behavior.

### INI section headers
Retained as:
- `[uwsgi]`

Current retained locations include:
- `examples/upsgi/*.ini`
- `tests/fork/configs/*.ini.in`
- `tests/fork/fault/unknown_option.t`
- `docs/upsgi/QUICKSTART.md`
- `docs/upsgi/MIGRATION.md`
- retained vassal examples under `vassals/`
- retained `t/static/*.ini` and `t/cachebitmap.ini`

Reason:
- changing the section name would widen X2 from naming cleanup into a config
  compatibility change
- the current fork still accepts the upstream-compatible INI section shape

### Perl and XS namespace surface
Retained as:
- `uwsgi::*`
- `uwsgi::input`
- `uwsgi::error`
- related helper namespaces exposed by the PSGI plugin

Current retained locations include:
- `plugins/psgi/psgi.h`
- `plugins/psgi/psgi_loader.c`
- `plugins/psgi/psgi_plugin.c`
- `t/perl/*`
- `docs/upsgi/PSGI_BOUNDARY.md`

Reason:
- these names are part of the retained Perl/XS interface
- renaming them would create an API and compatibility project, not a branding
  cleanup

### Explicit legacy migration wording
Retained examples include:
- migration docs that describe moving from upstream `uWSGI` to `upsgi`
- compatibility-only flag examples such as `--perl-no-die-catch`
- test fixtures that intentionally exercise legacy option parsing

Primary retained locations include:
- `docs/upsgi/MIGRATION.md`
- `docs/upsgi/COMPATIBILITY.md`
- `examples/upsgi/migration_legacy.ini`
- `tests/fork/regression/compatibility.t`
- `tests/fork/configs/legacy_compatible.ini.in`

Reason:
- these references document compatibility instead of presenting the old brand as
  the active product identity

## Provenance-retained references

These references remain because they explain project origin or historical
context.

Retained examples include:
- `upsgi is a PSGI-first fork of the uWSGI codebase`
- source comments and historical notes that mention old upstream versions or
  upstream subsystem names

Primary retained locations include:
- `README`
- `README.md`
- `docs/upsgi/MIGRATION.md`
- comments in retained upstream-derived source files such as `upsgi.h`

Reason:
- the fork should acknowledge lineage clearly, but provenance wording should not
  dominate the supported user path

## Deferred internal renames

These references remain because the churn is high and the user value is low.
Some internal filename cleanup has now landed, but the remaining items are still
out of scope for the public-facing rename sweep.

Examples include:
- `uwsgi.h` compatibility shim
- `plugins/psgi/uwsgi_plmodule.c`
- broad internal `uwsgi_*` symbol families
- workflow names that still mention `uWSGI`

Reason:
- renaming these now would create large review noise and a higher regression
  risk without meaningfully improving the supported product surface
- these belong under later deep normalization work if still worth doing after
  X3, Z, and Y

## Rule for future cleanup

A retained `uwsgi` or `uWSGI` reference should stay only if it is clearly one of:
- config compatibility
- Perl/XS compatibility
- migration documentation
- provenance / attribution
- deferred low-value internal naming

Anything outside those buckets should be treated as a candidate for future
cleanup.
