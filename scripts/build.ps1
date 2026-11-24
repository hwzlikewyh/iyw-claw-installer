param([Parameter(Mandatory=$true)][string]$WxSource,
    [ValidateSet('Win32','x64')][string]$Architecture='Win32',
    [string]$WebView2RuntimeDirectory)
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
$buildOptions = @()
# 正式向导签名作业按标签重建，使用与已验收适配版一致的微软运行库。
if (!$WebView2RuntimeDirectory -and $env:INSTALLER_TAG) {
    $WebView2RuntimeDirectory = Join-Path $projectRoot '.cache/webview2-x64'
    New-Item -ItemType Directory -Force -Path $WebView2RuntimeDirectory | Out-Null
    $redistributable = Join-Path $WebView2RuntimeDirectory 'MicrosoftEdgeWebView2RuntimeInstallerX64.exe'
    $digest = '47bc51387aff95c1275ce38cfbbb5fbab99b43b33214049877bbcdbcc7a970d5'
    if (!(Test-Path -LiteralPath $redistributable) -or (Get-FileHash -LiteralPath $redistributable).Hash -ne $digest) {
        $url = 'https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/63f7af19-5d22-43ab-af18-239a7b23b84c/MicrosoftEdgeWebView2RuntimeInstallerX64.exe'
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $redistributable
        if ((Get-FileHash -LiteralPath $redistributable).Hash -ne $digest) { throw 'WebView2 release digest mismatch.' }
    }
}
if ($WebView2RuntimeDirectory) {
    $runtime = Get-Item -LiteralPath (Join-Path $WebView2RuntimeDirectory 'MicrosoftEdgeWebView2RuntimeInstallerX64.exe')
    $signature = Get-AuthenticodeSignature -LiteralPath $runtime.FullName
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.GetNameInfo('SimpleName', $false) -ne 'Microsoft Corporation') {
        throw 'WebView2 standalone installer must have a valid Microsoft signature.'
    }
    $directory = [Security.SecurityElement]::Escape($runtime.DirectoryName)
    $props = Join-Path $outputRoot 'webview2-runtime.props'
    @"
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemDefinitionGroup><ResourceCompile>
    <PreprocessorDefinitions>IYW_EMBED_WEBVIEW2_X64;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    <AdditionalIncludeDirectories>$directory;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
  </ResourceCompile></ItemDefinitionGroup>
</Project>
"@ | Set-Content -LiteralPath $props -Encoding UTF8
    $buildOptions = @('--', "/p:ForceImportBeforeCppTargets=$props")
}
& $cmakePath --build $outputRoot --config Release --parallel 6 @buildOptions
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Write-Output (Join-Path $outputRoot 'bin\原助理安装向导.exe')
