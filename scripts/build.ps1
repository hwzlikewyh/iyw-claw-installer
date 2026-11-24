param([Parameter(Mandatory=$true)][string]$WxSource,
    [ValidateSet('Win32','x64')][string]$Architecture='Win32')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = (Resolve-Path -LiteralPath $WxSource).Path
$cmakeTool = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmakeTool) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools with CMake.' }
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
} else { $cmakePath = $cmakeTool.Source }
$outputRoot=Join-Path $projectRoot "build-$Architecture"
& (Join-Path $PSScriptRoot 'windows-dependencies.ps1') -WxSource $sourceRoot -Architecture $Architecture
if ($LASTEXITCODE -ne 0) { throw 'Payload dependencies failed.' }
& $cmakePath -S $projectRoot -B $outputRoot -A $Architecture "-DWX_SOURCE_DIR=$sourceRoot" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmakePath --build $outputRoot --config Release --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$executable = Join-Path $outputRoot 'bin\原助理安装向导.exe'
# 旧客户端只能下载 32 MiB 以内的向导，运行库必须按需获取。
$legacyInstallerLimit = 32MB
if ((Get-Item -LiteralPath $executable).Length -gt $legacyInstallerLimit) {
    throw 'Installer exceeds the legacy 32 MiB limit; do not embed runtime packages.'
}
Write-Output $executable
