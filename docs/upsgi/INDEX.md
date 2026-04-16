# upsgi docs index

This is the curated operator entrypoint for upsgi.

## Start here

- [Quickstart](QUICKSTART.md)
- [Configuration guide](CONFIG_GUIDE.md)
- [Argument reference](ARGUMENT_REFERENCE.md)
- [Runtime defaults](RUNTIME_DEFAULTS.md)
- [Logging](LOGGING.md)
- [Hardening](HARDENING.md)
- [Support boundary](SUPPORT_BOUNDARY.md)
- [Removal scope summary](REMOVAL_SCOPE_SUMMARY.md)
- [Packaging](PACKAGING.md)
- [Repository layout](REPO_LAYOUT.md)
- [Known limitations](KNOWN_LIMITATIONS.md)
- [Release checklist](RELEASE_CHECKLIST.md)

## Current baseline summary

- thunder lock is enabled by default
- body scheduler is enabled by default
- logging queues remain enabled by default
- the default validated build profile keeps `routing = false` and `pcre = false`
- a reduced local cache subset remains for static-path caching and SSL session cache support
- The conservative family-removal cleanup workstream is complete.

## Removal and compatibility summary

- Queue / sharedarea / subscription has been removed from the supported parser and runtime surface.
- Spooler / mule / farm has been removed from the supported parser and runtime surface.
- Generic cache management and cache-backed response lookup have been removed.
- Deferred from removal
  - Routing family

Historical work notes are intentionally not part of this curated public entry path.
