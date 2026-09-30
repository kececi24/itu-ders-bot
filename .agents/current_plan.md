# Current work

- Workstream: unified cross-platform port; `.agents/exec_plans/active/cross-platform-port.md`.
- Native preflight passed (localCMake4.4.3/AppleClang21/Python3.12.3/SDKcurl/loopback); no installs or live calls.
- Implementing platform adapters/setup, portable tests, dependency bootstrap/package/CI in parallel.
- Primary integrating shared main/clock/http and updating policy/docs.
- Next: native integration build, affected tests, complete offline suite and archive smoke.
- Preserve deleted data/example_config.json; sanitized packaging template will replace its archive role.
- Windows/Linux native CI and desktop/live acceptance remain unverified; macOS14/15 runtime checks deferred.
- Git read-only; dependencies project-local and ignored; no real registration/retry/publishing.
