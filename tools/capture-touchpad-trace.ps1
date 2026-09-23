#Requires -RunAsAdministrator
[CmdletBinding()]
param([ValidateRange(10,60)][int]$Seconds=40)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$kit=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64'
$folder=Join-Path $repo ('build\jitter-diagnostics\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Force -Path $folder | Out-Null
$deviceId='USB\VID_05AC&PID_0265&MI_01\*'
$binding=@(Get-CimInstance Win32_PnPSignedDriver | Where-Object DeviceID -like $deviceId)
if($binding.Count -ne 1){throw 'Expected exactly one wired Magic Trackpad 2.'}
$binding | Select-Object DeviceID,DriverVersion,InfName | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $folder 'device.json')
$pdb=Join-Path $repo "build\mt2-usb-edge-$($binding[0].DriverVersion)\AmtPtpDeviceUsbUm.pdb"
if(!(Test-Path -LiteralPath $pdb)){
    $binary=Get-Item -LiteralPath (Join-Path $repo 'build\AmtPtpDeviceUsbUm\x64\Release\AmtPtpDeviceUsbUm.dll')
    if($binary.VersionInfo.FileVersion -ne $binding[0].DriverVersion){throw 'No matching driver PDB. Use the original build for the installed version.'}
    $pdb=[IO.Path]::ChangeExtension($binary.FullName,'.pdb')
}
& (Join-Path $kit 'tracepdb.exe') -f $pdb -p $folder
if($LASTEXITCODE -ne 0){throw 'Trace format extraction failed; use the PDB matching the installed driver.'}
if(-not ('Mt2CursorProbe' -as [type])){
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Mt2CursorProbe {
    [StructLayout(LayoutKind.Sequential)] public struct Point {public int X; public int Y;}
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
}
'@
}
$session='MT2Jitter-'+[guid]::NewGuid().ToString('N')
$rows=[Collections.Generic.List[object]]::new()
& (Join-Path $kit 'tracelog.exe') -start $session -guid '#efc3ce99-43ff-4b59-afe4-c856e1afd8b0' -flag 16 -level 4 -f (Join-Path $folder 'contacts.etl')
if($LASTEXITCODE -ne 0){throw 'Trace start failed'}
try {
    Write-Output "Recording $Seconds seconds. Keep one finger stationary, then gently lift and replace it."
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt $Seconds){
        $point=[Mt2CursorProbe+Point]::new()
        if([Mt2CursorProbe]::GetCursorPos([ref]$point)){
            $rows.Add([pscustomobject]@{Time=(Get-Date).ToString('yyyy-MM-dd HH:mm:ss.fff');X=$point.X;Y=$point.Y})
        }
        Start-Sleep -Milliseconds 10
    }
} finally {
    & (Join-Path $kit 'tracelog.exe') -stop $session
    $rows | Export-Csv -LiteralPath (Join-Path $folder 'cursor.csv') -NoTypeInformation
}
& (Join-Path $kit 'tracefmt.exe') (Join-Path $folder 'contacts.etl') -p $folder -o (Join-Path $folder 'contacts.txt') -nosummary
if($LASTEXITCODE -ne 0){throw "Trace formatting failed. Original ETL retained at $folder"}
Write-Output "Saved to $folder. Decode with analyze-scroll-trace.py, then analyze-jitter-trace.py."
# Recording is bounded and opt-in. Never install a driver or change pointer settings.
