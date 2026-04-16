# Quickstart

## Build

```sh
CPUCOUNT=1 python3 upsgiconfig.py --build default
./upsgi --version
```

If your local toolchain already matches the default profile closely, `make` also
builds `./upsgi`.

## Run the baseline example

```sh
./upsgi --config examples/upsgi/baseline.yaml
```

Then visit `http://127.0.0.1:9090/`.

## Run the debug-exceptions example

```sh
./upsgi --config examples/upsgi/debug_exceptions.yaml
```

## Validate the maintained fork harness

```sh
prove -I tests/fork/lib tests/fork/regression/*.t tests/fork/fault/*.t
```
