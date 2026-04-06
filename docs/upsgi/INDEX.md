# upsgi documentation index

This is the authoritative documentation entry point for the fork.

## Start here
- `QUICKSTART.md` - baseline launch shape and shipped examples
- `MIGRATION.md` - how to move an existing PSGI deployment from upstream uWSGI to upsgi
- `OPTION_SURFACE.md` - baseline, advanced, and compatibility-only options
- `DEFERRED_SCOPE.md` - what is intentionally not part of the supported fork story
- `PACKAGING.md` - release artifact layout and release-output paths
- `RELEASE_READINESS.md` - current release-candidate posture and remaining non-blocking follow-ups
- `REPO_LAYOUT.md` - the authoritative repo layout for the maintained fork tree
- `HARDENING.md` - operator-facing hardening notes and trusted-front-end guidance
- `WORKER_LIFECYCLE.md` - graceful restart, paced chain reload, cheaper mode, and operator controls
- `ACCEPT_PATH.md` - accept-side distribution policy, thunder-lock bypass rules, and reuse-port guidance
- `DEPLOYMENT.md` - reverse-proxy-first deployment, keepalive scope, and PSGI informational responses

## Boundary and implementation notes
- `PSGI_BOUNDARY.md` - PSGI subtree ownership and layer split
- `LOGGING_BOUNDARY.md` - logging-core versus sink/backend ownership
- `REQUEST_STATIC_BOUNDARY.md` - request parsing/static interception versus file-serving ownership
- `LOGGING.md` - logging behavior and retained bundled sinks
- `COMPATIBILITY.md` - compatibility-only behavior retained for migration
- `RETAINED_LEGACY_REFERENCES.md` - intentional retained `uwsgi` references and why they still exist

## Release notes
- `releases/0.1.2.md`
- `releases/0.1.2-decision.md`
- `releases/0.1.1.md`
- `releases/0.1.1-decision.md`
- `releases/0.1.0-rc1.md`
- `releases/0.1.0-rc1-decision.md`
- `releases/0.1.0-rc2.md`
- `releases/0.1.0-rc2-decision.md`
- `releases/0.1.0.md`
- `releases/0.1.0-decision.md`
