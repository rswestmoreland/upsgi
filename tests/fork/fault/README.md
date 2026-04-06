# fault tests

Reserved for second-pass fault tests.


## Current fault tests
- `bad_app_path.t` validates missing PSGI app startup failure
- `unknown_option.t` validates strict unknown-option rejection

- `request_hardening.t` exercises duplicate Host / duplicate Content-Length / duplicate Transfer-Encoding / chunked+Content-Length rejection and confirms the server stays healthy afterward.
- `psgi_hardening.t` exercises malformed PSGI response shapes and invalid advanced XS helper usage to confirm they fail safely without crashing the server.
