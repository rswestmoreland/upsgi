# Validation notes

This follow-up fix addresses three concrete post-cleanup issues:

- default build profiles now include YAML runtime config support
- `--config` now uses extension-based loading for supported config formats
- maintained fork tests sanitize harness-only environment variables before launching the server under strict mode

## Post-YAML harness stabilization
- Maintained fork tests now append extra config as YAML, not INI-style lines.
- HTTP/1.1 profile rewriting updates YAML keys, not legacy INI keys.
- Test-side port allocation avoids duplicate ports within a single Perl process.
