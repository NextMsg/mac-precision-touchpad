[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$CertificateThumbprint)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$kit = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$infSource = Join-Path $repo 'packaging\mt2-usb-edge\AmtPtpUsbEdge.inf'
$infText = Get-Content -LiteralPath $infSource -Raw
if ($infText -notmatch '(?m)^DriverVer=[^,\r\n]+,([\d.]+)\s*$') { throw 'Missing INF version.' }
$version = $Matches[1]
$output = Join-Path $repo "build\mt2-usb-edge-$version"
New-Item -ItemType Directory -Force -Path $output | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'packaging\mt2-usb-edge\AmtPtpUsbEdge.inf') -Destination $output
Copy-Item -LiteralPath (Join-Path $repo 'build\AmtPtpDeviceUsbUm\x64\Release\AmtPtpDeviceUsbUm.dll') -Destination $output
Copy-Item -LiteralPath (Join-Path $repo 'build\AmtPtpDeviceUsbUm\x64\Release\AmtPtpDeviceUsbUm.pdb') -Destination $output
$signtool = Join-Path $kit 'bin\10.0.26100.0\x64\signtool.exe'
& (Join-Path $kit 'Tools\10.0.26100.0\x64\infverif.exe') /w /v (Join-Path $output 'AmtPtpUsbEdge.inf')
if ($LASTEXITCODE -ne 0) { throw 'INF validation failed.' }
& $signtool sign /fd SHA256 /sha1 $CertificateThumbprint (Join-Path $output 'AmtPtpDeviceUsbUm.dll')
if ($LASTEXITCODE -ne 0) { throw 'DLL signing failed.' }
& (Join-Path $kit 'bin\10.0.26100.0\x86\Inf2Cat.exe') "/driver:$output" /os:10_CO_X64,10_NI_X64,10_GE_X64 /uselocaltime /verbose
if ($LASTEXITCODE -ne 0) { throw 'Catalog generation failed.' }
& $signtool sign /fd SHA256 /sha1 $CertificateThumbprint (Join-Path $output 'AmtPtpUsbEdge.cat')
if ($LASTEXITCODE -ne 0) { throw 'Catalog signing failed.' }
Get-ChildItem -LiteralPath $output -File | Where-Object Extension -in '.inf','.cat','.dll' | ForEach-Object {
    [pscustomobject]@{ Name=$_.Name; SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash; Bytes=$_.Length }
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'sha256.json') -Encoding utf8
Write-Output "Signed development package: $output"
# Trust, installation and device verification are separate, explicit operations.
