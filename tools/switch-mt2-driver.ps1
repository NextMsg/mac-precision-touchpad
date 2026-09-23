#Requires -RunAsAdministrator
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$InfPath)
$ErrorActionPreference = 'Stop'
if (![Environment]::Is64BitProcess) { throw 'Run in 64-bit PowerShell.' }
$hardwareId = 'USB\VID_05AC&PID_0265&MI_01'
$inf = (Resolve-Path -LiteralPath $InfPath).Path
if ([IO.Path]::GetExtension($inf) -ne '.inf') { throw 'Expected an INF file.' }
$devices = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "$hardwareId\*" })
if ($devices.Count -ne 1) { throw "Expected exactly one attached wired Magic Trackpad 2; found $($devices.Count)." }
if (!(Select-String -LiteralPath $inf -SimpleMatch $hardwareId -Quiet)) { throw 'INF does not list the exact Magic Trackpad 2 USB hardware ID.' }
if (-not ('Mt2DriverInstaller' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Mt2DriverInstaller {
    [DllImport("newdev.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool UpdateDriverForPlugAndPlayDevicesW(
        IntPtr parent, string hardwareId, string infPath, uint flags,
        [MarshalAs(UnmanagedType.Bool)] out bool rebootRequired);
}
'@
}
$rebootRequired = $false
# FORCE also permits restoring an older backup; NONINTERACTIVE avoids dialogs.
$success = [Mt2DriverInstaller]::UpdateDriverForPlugAndPlayDevicesW(
    [IntPtr]::Zero, $hardwareId, $inf, 5, [ref]$rebootRequired)
if (!$success) {
    $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "Driver update failed: $code / 0x$($code.ToString('X8')). Check C:\Windows\INF\setupapi.dev.log."
}
[pscustomobject]@{ Updated=$true; RebootRequired=$rebootRequired; InfPath=$inf }
# The caller verifies device status; never restart Windows automatically.
