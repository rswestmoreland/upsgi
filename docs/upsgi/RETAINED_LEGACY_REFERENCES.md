# retained legacy references

This note documents the compatibility-oriented legacy references that remain
intentional in the fork after the public rename sweep.

## Retained config compatibility references
These references remain because the runtime loader still accepts older config
formats and explicit compatibility loaders.

Retained examples include:
- `--ini`
- `--xml`
- `--json`
- older config files passed through `--config existing.ini`

Primary retained locations include:
- `core/upsgi.c`
- `core/ini.c`
- `core/xmlconf.c`
- `core/json.c`
- `core/yaml.c`
- `docs/upsgi/RUNTIME_CONFIG_POLICY.md`
- `docs/upsgi/COMPATIBILITY.md`
- `tests/fork/configs/*.ini.in`

Reason:
- these paths preserve migration compatibility for existing deployments even
  though new operator-facing examples are YAML-first

## Retained Perl/XS and PSGI interface references
These references remain because they are part of the PSGI and Perl surface owned
by the PSGI plugin.

Current retained locations include:
- `plugins/psgi/psgi.h`
- `plugins/psgi/psgi_loader.c`
- `plugins/psgi/psgi_plugin.c`
- `t/perl/*`
- `docs/upsgi/PSGI_BOUNDARY.md`

Reason:
- these names are part of the retained Perl/XS interface
- renaming them would create an API and compatibility project, not a runtime
  config or documentation cleanup

## Explicit legacy migration wording
Retained examples include:
- migration docs that describe moving from older deployments to upsgi
- compatibility-only flag examples such as `--perl-no-die-catch`
- tests that intentionally exercise legacy option parsing

Primary retained locations include:
- `docs/upsgi/MIGRATION.md`
- `docs/upsgi/COMPATIBILITY.md`
- `tests/fork/regression/compatibility.t`
- `tests/fork/configs/legacy_compatible.ini.in`

Reason:
- these references document compatibility instead of presenting old naming or
  older config formats as the active product identity

## Provenance-retained references
These references remain because they explain project origin or historical
context without using the former public product name.

Retained examples include:
- `upsgi is a PSGI-first fork of the original upstream codebase`
- source comments and historical notes that mention upstream subsystem lineage

Primary retained locations include:
- `README`
- `README.md`
- `docs/upsgi/MIGRATION.md`
- comments in retained upstream-derived source files such as `upsgi.h`

Reason:
- the fork should acknowledge lineage clearly, but provenance wording should not
  dominate the supported user path

## Rule for future cleanup
A retained compatibility-oriented reference should stay only if it is clearly one of:
- config compatibility
- Perl/XS compatibility
- migration documentation
- provenance / attribution
- deferred low-value internal naming

Anything outside those buckets should be treated as a candidate for future
cleanup.
