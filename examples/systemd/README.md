# systemd examples

This directory contains sample files for running upsgi under systemd.

Files:
- `upsgi.service` - baseline service unit
- `upsgi.socket` - optional socket activation example
- `upsgi.env` - optional environment file

Expected runtime config:
- `/etc/upsgi/app.yaml`

Expected binary path in the sample service unit:
- `/usr/local/bin/upsgi`

Adjust paths, user/group, and working directory to match the deployment.
