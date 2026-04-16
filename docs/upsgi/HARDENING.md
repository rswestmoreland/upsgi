# Hardening

## Reverse-proxy trust boundary

- `enable-proxy-protocol` should only be enabled behind a trusted proxy boundary
- `log-x-forwarded-for` should only be enabled when the front door is trusted
