# upsgi systemd service guidance

systemd is the primary documented Linux service model for upsgi.

This guidance is for the common PSGI-first deployment shape:
- one application service per unit
- YAML runtime config loaded with `--config`
- no daemonization under systemd
- no pidfile required for the normal service path
- journald or an explicit configured log sink

## Recommended service model

Use a dedicated service account and run upsgi in the foreground under systemd.

Recommended baseline:
- `Type=notify`
- dedicated `upsgi` user and group, or a per-app service account
- `WorkingDirectory` set to the deployed app root
- YAML runtime config under `/etc/upsgi/`
- `RuntimeDirectory=upsgi`
- `Restart=on-failure`
- `LimitNOFILE` set explicitly for production
- no `--daemonize`
- no `--pidfile` for the normal systemd path

Why this is the preferred model:
- systemd owns service lifetime and restart behavior
- readiness can be tied to the inherited notify path
- logs can flow cleanly to journald without daemon-mode indirection
- unit-local limits and environment handling are easier to reason about

## Dedicated service user

For most deployments, create a dedicated account and keep ownership narrow.

Example:

```sh
sudo useradd --system --home /srv/myapp --shell /usr/sbin/nologin upsgi
sudo install -d -o upsgi -g upsgi /etc/upsgi /srv/myapp/current /var/log/upsgi
```

For multi-app hosts, a per-application service account can be even cleaner.
The important part is that the app code, writable directories, and config files
have deliberate ownership.

## Recommended file layout

Example layout:

```text
/etc/upsgi/app.yaml
/etc/default/upsgi-app
/srv/myapp/current/app.psgi
/var/log/upsgi/
/run/upsgi/
```

Notes:
- `/etc/upsgi/app.yaml` is the canonical runtime config location in the docs
- `/etc/default/upsgi-app` is optional and can carry environment overrides
- `/run/upsgi/` is managed by systemd through `RuntimeDirectory`

## Primary unit example

A ready-to-adapt example unit is shipped at:
- `examples/systemd/upsgi.service`

Example:

```ini
[Unit]
Description=upsgi PSGI application server
After=network.target
Wants=network.target

[Service]
Type=notify
User=upsgi
Group=upsgi
WorkingDirectory=/srv/myapp/current
EnvironmentFile=-/etc/default/upsgi-app
RuntimeDirectory=upsgi
RuntimeDirectoryMode=0755
ExecStart=/usr/local/bin/upsgi --config /etc/upsgi/app.yaml
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=2
KillSignal=SIGTERM
TimeoutStopSec=30
LimitNOFILE=65535
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

Install and enable:

```sh
sudo cp examples/systemd/upsgi.service /etc/systemd/system/upsgi.service
sudo systemctl daemon-reload
sudo systemctl enable --now upsgi.service
sudo systemctl status upsgi.service
```

## Environment handling

A ready-to-adapt environment file is shipped at:
- `examples/systemd/upsgi.env`

Use an environment file for deployment-local values that should not be hardcoded
into the unit file.

Typical examples:
- `PLACK_ENV=production`
- `PERL5LIB=/srv/myapp/current/lib`
- `PATH=` entries for a Perl runtime manager

Example:

```sh
PLACK_ENV=production
PERL5LIB=/srv/myapp/current/lib
PATH=/usr/local/bin:/usr/bin:/bin
```

## plenv, local::lib, and carton guidance

There are two sane approaches:

### 1. Prefer a stable absolute Perl path

If the host has a stable production Perl install, use absolute paths and keep the
unit simple.

Example:

```ini
ExecStart=/usr/local/bin/upsgi --config /etc/upsgi/app.yaml
```

### 2. Use an explicit wrapper for plenv or carton

If the app depends on plenv or carton, prefer a small wrapper script that sets up
that environment and then execs upsgi. Avoid hiding complex shell setup directly
inside `ExecStart=`.

Example wrapper pattern:

```sh
#!/bin/sh
set -eu
export PLACK_ENV=production
export PATH="/srv/myapp/.plenv/shims:/srv/myapp/.plenv/bin:/usr/local/bin:/usr/bin:/bin"
exec /srv/myapp/.plenv/shims/upsgi --config /etc/upsgi/app.yaml
```

This keeps the systemd unit readable and makes application-runtime issues easier
to debug.

## Log handling

Recommended default:
- keep the service in the foreground
- let systemd capture stdout and stderr into journald
- add an explicit upsgi log sink only when you have a clear reason to do so

Options:
- journald-only for simpler hosts
- `logto:` for an app-local file log
- `syslog:` or the retained rsyslog path if the environment already expects it

A practical rule:
- use journald for normal service supervision
- use file or network sinks only when retention, shipping, or separation needs
  justify them

## Restart behavior

Recommended baseline:
- `Restart=on-failure`
- `RestartSec=2`
- `TimeoutStopSec=30`

This keeps crash recovery automatic while still allowing a normal stop/restart
flow during deployments.

## File descriptor limits

Set `LimitNOFILE` explicitly.

Why:
- listener sockets, accepted sockets, logs, and app-level file activity all
  consume descriptors
- default host limits are often too low for a busy prefork server

Recommended starting points:
- smaller hosts: `LimitNOFILE=8192`
- general production baseline: `LimitNOFILE=65535`
- raise further only with a clear workload reason

## pidfile guidance

For the normal systemd service model, do not use a pidfile.

Why:
- systemd already tracks the main process
- pidfiles add another state file to maintain and clean up
- foreground service mode is the cleaner public story for upsgi

Retain pidfiles only if you have an external integration that explicitly still
requires them.

## Safe production defaults

Reasonable starting service defaults:
- `Type=notify`
- foreground mode
- no pidfile
- no daemonize
- `Restart=on-failure`
- `LimitNOFILE=65535`
- dedicated service account
- runtime config under `/etc/upsgi/app.yaml`
- app root under a dedicated deploy directory

Reasonable YAML config defaults to pair with this:
- `master: true`
- `workers: 2` or more according to workload and memory budget
- `need-app: true`
- `strict: true`
- `vacuum: true`
- `die-on-term: true`

## Optional socket activation

The inherited tree still contains systemd socket-activation support. That can be
useful, but it is not the primary public deployment story for this fork.

A sample socket unit is shipped for advanced operators at:
- `examples/systemd/upsgi.socket`

Use it only when socket activation is a deliberate operational choice.

## Suggested operational workflow

1. Build and install `upsgi`
2. Copy and adapt `examples/upsgi/upsgi.example.yaml` into `/etc/upsgi/app.yaml`
3. Copy and adapt `examples/systemd/upsgi.service`
4. Add `/etc/default/upsgi-app` only if environment overrides are needed
5. `systemctl daemon-reload`
6. `systemctl enable --now upsgi.service`
7. validate with `systemctl status upsgi.service` and `journalctl -u upsgi.service`
