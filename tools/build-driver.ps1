[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$FullSolution
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 with the components in .vsconfig first.' }
$msbuild = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\Current\Bin\amd64\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'Visual Studio 2022 C++ MSBuild was not found.' }
$kitVersion = '10.0.26100.0'
$kitRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
if (!(Test-Path -LiteralPath (Join-Path $kitRoot "build\$kitVersion\WindowsDriver.common.targets"))) {
    throw 'Install Windows SDK and WDK 26100 (VS 2022 compatible) first.'
}
$buildScope = if ($FullSolution) { 'solution' } else { 'usb' }
$logDirectory = Join-Path $repoRoot "build\logs\$Configuration\$buildScope"
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$target = if ($FullSolution) { Join-Path $repoRoot 'AmtPtpDriver.sln' } else {
    Join-Path $repoRoot 'src\AmtPtpDeviceUsbUm\MagicTrackpad2PtpDevice.vcxproj'
}
$buildArguments = @(
    $target, '/m', '/t:Build', '/nologo', '/verbosity:minimal',
    "/p:Configuration=$Configuration", '/p:Platform=x64',
    '/p:PreferredToolArchitecture=x64',
    "/p:WindowsTargetPlatformVersion=$kitVersion", "/p:SolutionDir=$repoRoot\",
    '/p:SignMode=Off', "/bl:$(Join-Path $logDirectory 'driver.binlog')",
    '/fl', "/flp:logfile=$(Join-Path $logDirectory 'driver.log');verbosity=normal"
)
& $msbuild @buildArguments
if ($LASTEXITCODE -ne 0) { throw "Driver build failed ($LASTEXITCODE). See $logDirectory." }
Write-Output 'Build succeeded. Output is unsigned; no driver was installed.'
