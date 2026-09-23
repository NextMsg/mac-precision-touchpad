# Changelog

## v0.1.0-alpha.1 — source preview (2026-09-23)

Target: Magic Trackpad 2 Lightning, wired USB, Windows 11 x64. Local development binary version: 2026.922.4.0.

- Add edge-origin rejection with a default 5% margin on each side.
- Distinguish hovering fingers from confirmed surface contact.
- Debounce excessive contact area over three confirmed-touch samples.
- Deliver stable, single UP reports and preserve contact identity across missed reads.
- Correct scan-time conversion, coordinate bounds and output-error completion.
- Add a dedicated Windows 11 USB INF, build/package scripts and host-side regression tests.

Validation: native MSVC tests, GCC ASan/UBSan tests, recorded-input replay, WDK Release build, INF/catalog checks and one-device user testing. Public signing, generic backup/rollback tooling and broader compatibility testing remain release work.
