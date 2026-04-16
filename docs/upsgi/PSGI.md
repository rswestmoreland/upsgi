# PSGI support

upsgi embeds the `psgi` plugin by default.

## Default behavior

- `psgi.input` and `psgi.errors` are constructed directly from the retained C / XS bridge
- `psgi-enable-psgix-io` is optional and off by default
- when enabled, `psgix.io` now wraps a duplicated client fd, preserves keepalive safety, and uses a direct Perl core handle construction path
