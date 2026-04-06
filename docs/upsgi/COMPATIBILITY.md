# upsgi compatibility notes

## Compatibility-only flags
The following options remain accepted so older PSGI-oriented configs can still parse:

- `--http-modifier1`
- `--http-modifier2`
- `--http-socket-modifier1`
- `--http-socket-modifier2`
- `--https-socket-modifier1`
- `--https-socket-modifier2`
- `--perl-no-die-catch`

These flags do not change runtime behavior in the PSGI-only fork.

## Centralized compatibility shim handling
These compatibility-only flags are intentionally routed through a single parse-only shim callback in the source tree:

- `uwsgi_opt_compat_noop()`

That keeps the current behavior silent by default and gives the fork one future debug-only trace point if compatibility diagnostics are ever needed later.

## Exception logging
Historically, some deployments used `--perl-no-die-catch` to suppress default PSGI exception logging. In upsgi, default PSGI exception catch logging is already disabled. Use `--log-exceptions` when explicit server-side exception visibility is needed for debugging.

## Static-map
`--static-map` remains a baseline feature through the core request path. It does not require `router_static` solely for baseline file serving.

## New examples
New quickstart examples should use the baseline option surface and should not include compatibility-only flags. Keep compatibility-only examples in migration-focused docs and tests only.


See also: `docs/upsgi/RETAINED_LEGACY_REFERENCES.md` for the legacy names and references that remain intentionally present in the fork.
