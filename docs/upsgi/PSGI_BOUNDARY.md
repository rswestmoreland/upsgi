# upsgi PSGI boundary map

This note records the intended maintenance split for the retained PSGI subtree in upsgi v1.

## Files and ownership

### `plugins/psgi/psgi_plugin.c`
Owns:
- PSGI option registration
- compatibility-only PSGI option parsing
- request dispatch entry point
- plugin callback wiring

Does not own:
- response marshalling
- Perl interpreter construction
- Perl app loading internals

### `plugins/psgi/psgi_response.c`
Owns:
- PSGI response validation
- status/header preparation
- informational-response emission before the final response
- body emission
- sendfile/object/iterator body handling
- async body resume handling

Does not own:
- request parsing
- app lookup/loading
- Perl XS bootstrap

### `plugins/psgi/psgi_loader.c`
Owns:
- XS bootstrap for `upsgi::*`, including `psgix.logger`, streaming writer, and `psgix.informational` callbacks
- Perl interpreter construction
- PSGI app loading and registration helpers
- opt-in exception-hook installation for debugging
- preinit/init app loading flow
- retained Perl helper execution support

Does not own:
- top-level option parsing
- generic logging core behavior
- static-map request interception

### `plugins/psgi/upsgi_plmodule.c`
Owns:
- retained Perl extras/supporting XS helpers kept in v1

These helpers remain intentionally outside the baseline request/response boundary.

### `plugins/psgi/psgi.h`
Owns:
- shared PSGI/Perl state
- shared declarations used by the PSGI subtree

## Practical maintenance rule

When changing PSGI behavior in upsgi:
1. change request routing/selection in `psgi_plugin.c`
2. change response shaping/body emission in `psgi_response.c`
3. change Perl bridge/app loading in `psgi_loader.c`
4. keep optional Perl extras isolated in `upsgi_plmodule.c`

This keeps the fork easier to audit and prepares the code for later cleanup and, eventually, a separate Rust reimplementation.
