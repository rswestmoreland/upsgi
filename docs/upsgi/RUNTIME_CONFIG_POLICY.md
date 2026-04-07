# upsgi runtime config policy

This document defines the runtime configuration policy for the public upsgi fork.

## Policy summary

- Runtime configuration remains multi-format where already supported in the inherited codebase.
- YAML is the canonical documented runtime configuration format for upsgi.
- The preferred documented runtime entrypoint is `--config /path/to/file.yaml`.
- Explicit format loaders such as `--ini`, `--yaml`, `--xml`, and `--json` remain supported where they already exist.
- Build configuration remains TOML-based.
- New operator-facing examples should be YAML-first unless a document or test is intentionally demonstrating compatibility behavior.

## Why YAML is the default documented runtime format

YAML is already supported by the inherited runtime config path, so making YAML the public default does not require inventing a new runtime parser family first.

This gives upsgi a cleaner operator story now:
- keep compatibility with existing config loaders
- stop centering INI in new docs and examples
- avoid a risky runtime-config redesign in the same milestone

## Runtime formats currently recognized

The inherited config path already recognizes these families in the current tree:
- INI
- YAML
- XML
- JSON

The repo should present these formats as follows:
- YAML: canonical documented format for new upsgi examples
- INI: compatibility-oriented and still important for migrations from upstream upsgi
- XML and JSON: retained explicit config loaders for operators who already use them

## Preferred CLI entrypoint

The preferred public launch shape is:

```sh
./upsgi --config /path/to/upsgi.yaml
```

This keeps the public operator story format-neutral at the CLI level while still making YAML the documented default through examples and shipped templates.

## Explicit format selectors

These remain useful, but they are not the primary documented path for new upsgi examples:
- `--ini`
- `--yaml`
- `--xml`
- `--json`

Recommended positioning in docs:
- `--config`: preferred and recommended
- explicit format selectors: retained for compatibility, explicitness, and existing workflows

## Extension-based loading

Extension-based loading remains part of the current runtime story.

That means operators can continue to use:
- `--config file.yaml`
- `--config file.yml`
- `--config file.ini`
- `--config file.xml`
- `--config file.json`

The docs should still bias toward YAML examples, but they should not imply that non-YAML formats were removed.

## Emperor and vassal implications

The inherited Emperor path already routes supported extensions to explicit loader flags.

Current public policy:
- keep extension-based Emperor behavior intact
- do not widen Emperor scope in this milestone
- do not present Emperor config management as the primary deployment story for upsgi v1
- keep systemd-managed app services as the preferred deployment direction in public docs

## Canonical shipped config templates

The repo should ship YAML-first runtime templates:
- a full commented canonical config with practical defaults and most options commented out
- a smaller baseline example for quick starts
- a migration-focused example only when intentionally demonstrating compatibility behavior

## Help text and output wording targets

These wording targets are locked for follow-up implementation work.

### Preferred help text direction

`--config`
- target wording: `load configuration from file (format inferred from extension or handled by the pluggable loader system)`

Explicit loaders
- target wording pattern: `load configuration from <format> file`
- INI may additionally be described as compatibility-oriented in docs, but the CLI help should stay concise

### `--show-config`

Public target behavior:
- describe it in docs as a config inspection/export aid
- move the public wording away from `reformatted as ini`

Preferred end-state wording:
- `show the current config as normalized YAML`

Fallback wording if implementation lands in stages:
- `show the current config in normalized form`

The goal is for the public operator story to stop presenting INI as the canonical output or default reference format.

## Build config policy

Build configuration stays TOML-based.

Reason:
- build profiles are already migrated
- TOML is already working cleanly in the repo
- there is no user benefit in replacing build TOML with YAML solely for superficial consistency

Public rule:
- runtime config is YAML-first
- build config is TOML

## Documentation rule going forward

When adding new docs or examples:
- use YAML first for runtime examples
- use `--config file.yaml` first in commands
- mention INI only for migration or compatibility coverage
- do not imply that XML or JSON support was removed if it still exists in code

## Compatibility rule going forward

The fork should distinguish clearly between:
- preferred documented behavior
- retained compatibility behavior

For runtime config, that means:
- YAML is preferred
- multi-format support is retained
- INI is still important for compatibility and migration, but it is no longer the public default story
