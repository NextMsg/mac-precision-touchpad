# Magic Trackpad 2 USB — Community fixes

Community development fork of [imbushuo/mac-precision-touchpad](https://github.com/imbushuo/mac-precision-touchpad), retaining the upstream history and licenses. This is not an official Apple or upstream-maintainer release.

**Status: source preview. A production-signed public installer is not available yet.** The local development build is `2026.922.4.0`; its test certificate is not a public distribution solution.

[中文说明](README.zh-CN.md) · [Build and test](docs/community-development.md) · [Changes](CHANGELOG.md) · [Release plan](docs/public-release-plan.md) · [Historical upstream README](docs/upstream-README.md)

## Supported scope

The changes and dedicated package target **Magic Trackpad 2 Lightning over USB on Windows 11 x64**, hardware ID `USB\VID_05AC&PID_0265&MI_01`.

One physical device has been tested. Bluetooth, USB-C models, ARM64, Windows 10 and MacBook internal trackpads have not been validated for this fork. The inherited source code for other devices remains in the repository; that is not a claim of new compatibility testing.

## Improvements

- Reject contacts that begin in the outer 5% of each axis, until they lift. A finger starting centrally can continue to the edge.
- Use the device's confirmed-touch state instead of nonzero contact area: a hovering second finger no longer becomes a false press.
- Require three confirmed-touch samples before rejecting excessive contact area, avoiding transient size spikes during scrolling.
- Preserve the last delivered coordinates on release; track contact lifetimes and ID reuse across missed host reads.
- Correct Type 5 scan-time conversion, clamp coordinates, and complete failed output requests.
- Provide a Windows 11 USB-only INF using the system-supplied driver dependencies.

## Limitations

The edge width is currently a build-time setting. Intentional gestures starting at an edge are rejected too. Physical clicks are not filtered by wrist identity. The old settings application's sensitivity controls are not connected to this Type 5 path.

Automated tests and local user validation passed, including the reproducible hovering-finger drift case. Broader device, sleep/resume and clean-install testing is still needed. The inherited all-device package is not the package being released here.

## Installation and feedback

There is currently no general-user installation recommendation. Do not follow the historical upstream README's download/install instructions for this modified build. Developers can build and run the tests described in the development guide; trusted public packaging is tracked in the release plan.

When reporting a problem, include the device model, connection type, Windows build, driver version, reproduction steps and whether a rollback fixes it. Review diagnostic traces before sharing them; they may contain device identifiers and pointer coordinates.

## License and credits

The modified USB driver remains under [GPLv2](LICENSE-GPL.md), as specified by [the upstream license notice](LICENSE.md). Retain the upstream authors' copyright notices. Any future binary release must provide its corresponding modified source and build materials. Unmodified components retain their existing licenses, including MIT for the SPI component.
