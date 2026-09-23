# Build and test the community USB changes

Scope: the USB UMDF Type 5 path and the dedicated Windows 11 x64 INF. See the root README for the narrower tested device scope.

## Dependencies

- Visual Studio 2022 with the C++ workload and components in `.vsconfig` (MSVC v143, Spectre libraries, WDK integration).
- Windows SDK and WDK with target version `10.0.26100.0`. The scripts use the 64-bit MSBuild and compiler host.
- Python 3; GCC for Linux tests, or MSVC in an x64 Developer PowerShell.

## Tests

```sh
python3 tests/run_edge_rejection.py
```

Linux tests use AddressSanitizer and UndefinedBehaviorSanitizer. For native Windows tests in an x64 VS developer shell:

```powershell
python tests/run_edge_rejection.py --msvc
```

Tests run with 5% edge rejection and with that rule disabled. They exercise the actual Type 5 handler with mocked WDF and the real report layouts: hover filtering, stable release positions, ID reuse, missing reads, output failures, contact overflow, edge boundaries, area debounce and scan-time wraparound. They do not replace hardware validation.

Private captured traces can optionally be replayed with `--trace <contacts.json>` after decoding them with the tools under `tools/`. Do not commit personal captures. Keep matching PDBs for trace decoding.

## Driver build

```powershell
.\tools\build-driver.ps1 -Configuration Release
```

Output: `build/AmtPtpDeviceUsbUm/x64/Release/AmtPtpDeviceUsbUm.dll`. This step neither signs nor installs a driver.

`tools/package-mt2-driver.ps1 -CertificateThumbprint <your-certificate-thumbprint>` validates `packaging/mt2-usb-edge/AmtPtpUsbEdge.inf`, signs the DLL, creates and signs the catalog, and writes the package and hashes under `build/mt2-usb-edge-<version>`. It uses an existing certificate in CurrentUser My, and does not create trust or install anything. Local development signatures are not a production release.

The `-FullSolution` build includes the inherited multi-device package, which is not currently passing the modern INF system-driver-reference checks. Use the dedicated USB package for this work.

## Installation tools

`switch-mt2-driver.ps1` is a developer tool, not a general-user installer: it forces the selected INF on exactly one connected matching device, so it can also restore an older package. It does not automatically make a backup. Before use, export the currently bound package with PnPUtil, verify the backup and prepare a rollback path. A future public installer must implement those steps automatically and verify the resulting device state.

The work keeps the upstream USB driver's GPLv2 license. Retain notices and pair binary releases with corresponding source and build instructions.
