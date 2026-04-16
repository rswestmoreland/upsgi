# Logging

upsgi keeps logging enabled by default.

## Queue defaults

- `log-queue-enabled` defaults to on
- `disable-log-queue` disables the queue path for troubleshooting
- `log-queue-records: 512` is the tuned default record cap
- `log-queue-bytes: 512 KiB` is the tuned default byte cap

## Forwarded identity logging

- `log-x-forwarded-for` logs the client IP from `X-Forwarded-For` instead of the raw socket peer address

## Shipped queue-cap examples

- `examples/upsgi/logging_small_host.yaml`
- `examples/upsgi/logging_chatty_sink.yaml`
