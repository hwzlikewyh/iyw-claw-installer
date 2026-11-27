R"ps(param([string]$AppDirectory, [string]$StateFile, [switch]$Restore)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$env:PSModulePath = [IO.Path]::Combine($PSHOME, 'Modules')
$registrationTimeoutMs = 30000

function Initialize-Identity([string]$Directory) {
    $script:app = [IO.Path]::GetFullPath($Directory)
    $script:share = Join-Path $app 'native-share'
    $script:config = Get-Content -LiteralPath (Join-Path $share 'config.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (!$config.signed -or $config.receiver -ne 'main') { throw '当前应用没有已签名的主程序身份包。' }
    $script:packageName = if ($config.packageName) { $config.packageName } else { 'IywAssistant.Desktop' }
    if ($packageName -notmatch '^[A-Za-z0-9.-]+$') { throw '应用身份包名称无效。' }
    $script:packagePath = Join-Path $share 'IywAssistant.ShareTarget.msix'
    $signature = Get-AuthenticodeSignature -LiteralPath $packagePath
    if ($signature.Status -ne 'Valid') { throw "身份包签名校验失败：$($signature.Status)；$($signature.StatusMessage)" }
    $script:publisher = $signature.SignerCertificate.Subject
    if ($config.publisher -and $config.publisher -ne $publisher) { throw '身份包发布者与应用配置不匹配。' }
    $executable = Join-Path $app 'iyw-claw.exe'
    $mainSignature = Get-AuthenticodeSignature -LiteralPath $executable
    if ($mainSignature.Status -ne 'Valid' -or $mainSignature.SignerCertificate.Subject -ne $publisher) {
        throw '主程序签名无效或与身份包发布者不匹配。'
    }
}
function Get-IdentityPackages {
    $packages = @(Get-AppxPackage -Name $packageName | Where-Object { $_.Name -eq $packageName })
    if ($packages | Where-Object { $_.Publisher -ne $publisher }) { throw '存在其他发布者的同名身份包，已保留原记录。' }
    return $packages
}
function Get-ExternalLocation($Package) {
    $null = [Windows.Management.Deployment.PackageManager, Windows.Management.Deployment, ContentType=WindowsRuntime]
    $manager = New-Object Windows.Management.Deployment.PackageManager
    $native = $manager.FindPackageForUser('', $Package.PackageFullName)
    if (!$native) { throw '无法读取原身份包的安装位置。' }
    return [IO.Path]::GetFullPath($native.EffectiveLocation.Path)
}
function Save-IdentityState {
    if (Test-Path -LiteralPath $StateFile) { return }
    $original = @()
    foreach ($package in Get-IdentityPackages) {
        $location = ''
        try { $location = Get-ExternalLocation $package }
        catch { [Console]::Error.WriteLine("原身份位置读取失败：$($_.Exception.Message)") }
        $original += @{ Location = $location; FullName = $package.PackageFullName }
    }
    $state = @{ Name = $packageName; Publisher = $publisher; Original = $original }
    $temporary = $StateFile + '.new'
    [IO.File]::WriteAllText($temporary, ($state | ConvertTo-Json -Depth 4), [Text.Encoding]::UTF8)
    [IO.File]::Move($temporary, $StateFile)
}
function Invoke-PayloadRegistration([switch]$CheckOnly) {
    # 独立进程隔离旧脚本的 exit，并显式指定目录，兼容 32 位向导调用 64 位 PowerShell。
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $PSHOME 'powershell.exe'
    $start.Arguments = '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + (Join-Path $share 'register.ps1') + '"'
    if ($CheckOnly) { $start.Arguments += ' -CheckOnly' }
    $start.WorkingDirectory = $share
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [Text.Encoding]::UTF8
    $process = [Diagnostics.Process]::Start($start)
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit($registrationTimeoutMs)) {
            $process.Kill(); $process.WaitForExit()
            throw '应用身份检查超时。'
        }
        [Console]::Out.Write($output.GetAwaiter().GetResult())
        [Console]::Error.Write($errors.GetAwaiter().GetResult())
        if ($process.ExitCode -ne 0) { throw "应用身份检查未通过，退出码：$($process.ExitCode)" }
    } finally { $process.Dispose() }
}
function Assert-RegisteredIdentity {
    $packages = @(Get-IdentityPackages)
    if ($packages.Count -ne 1) { throw '注册后未取得唯一的当前用户身份记录。' }
    $package = $packages[0]
    if ($package.Version -ne [version]$config.packageVersion -or $package.Status -ne 'Ok') {
        throw "注册后身份版本或状态不正确：$($package.Version) / $($package.Status)"
    }
    $location = (Get-ExternalLocation $package).TrimEnd('\')
    if (![string]::Equals($location, $app.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw '注册后身份仍指向其他安装目录。'
    }
    Invoke-PayloadRegistration -CheckOnly
}
function Test-PackageInUse($Failure) {
    for ($cause = $Failure.Exception; $cause; $cause = $cause.InnerException) {
        if ($cause.HResult -eq [int]0x80073D02 -or $cause.Message -match '\b0x80073D02\b') { return $true }
    }
    return $Failure.ErrorDetails.Message -match '\b0x80073D02\b'
}
function Add-Identity {
    $retryDelays = @(1, 2)
    for ($attempt = 0; ; $attempt++) {
        try {
            Add-AppxPackage -Path $packagePath -ExternalLocation $app -ForceUpdateFromAnyVersion -ForceTargetApplicationShutdown
            break
        } catch {
            if (!(Test-PackageInUse $_)) { throw }
            if ($attempt -ge $retryDelays.Count) {
                throw "Windows 应用包仍被占用（0x80073D02）。请关闭原助理及其分享窗口后重试。$($_.Exception.Message)"
            }
            Start-Sleep -Seconds $retryDelays[$attempt]
        }
    }
    Invoke-PayloadRegistration
    Assert-RegisteredIdentity
}
function Repair-Identity {
    try { Assert-RegisteredIdentity; return }
    catch { [Console]::Out.WriteLine("现有身份需要修复：$($_.Exception.Message)") }
    try {
        Add-Identity
        [Console]::Out.WriteLine('主程序身份注册与启动检查通过。')
        return
    } catch {
        if (Test-PackageInUse $_) { throw }
        [Console]::Error.WriteLine("正常注册失败，准备重建当前用户身份：$($_ | Out-String)")
    }
    # 仅重建同名称、同发布者的当前用户身份。新 MSIX 和主程序已验签。
    foreach ($package in Get-IdentityPackages) {
        Remove-AppxPackage -Package $package.PackageFullName
    }
    Add-Identity
    [Console]::Out.WriteLine('已重建当前用户身份，主程序启动检查通过。')
}
function Restore-Identity {
    if (!(Test-Path -LiteralPath $StateFile)) { return }
    $state = Get-Content -LiteralPath $StateFile -Raw -Encoding UTF8 | ConvertFrom-Json
    $script:packageName = $state.Name
    $script:publisher = $state.Publisher
    if ($packageName -notmatch '^[A-Za-z0-9.-]+$' -or !$publisher) { throw '身份备份记录无效。' }
    if (@($state.Original).Count -eq 0) {
        foreach ($package in Get-IdentityPackages) { Remove-AppxPackage -Package $package.PackageFullName }
        [Console]::Out.WriteLine('已移除本次未完成安装创建的身份记录。')
        return
    }
    if (@($state.Original).Count -ne 1 -or !$state.Original[0].Location) {
        throw '原身份位置无法确定，已保留诊断记录，重新安装时将修复身份。'
    }
    $originalLocation = [IO.Path]::GetFullPath($state.Original[0].Location).TrimEnd('\')
    if ([string]::Equals($originalLocation, [IO.Path]::GetFullPath($AppDirectory).TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase) -and
        !(Test-Path -LiteralPath (Join-Path $originalLocation 'iyw-claw.exe') -PathType Leaf)) {
        foreach ($package in Get-IdentityPackages) { Remove-AppxPackage -Package $package.PackageFullName }
        [Console]::Out.WriteLine('原安装缺少主程序，已清理该残留安装的身份记录。')
        return
    }
    Initialize-Identity $state.Original[0].Location
    if ($packageName -ne $state.Name -or $publisher -ne $state.Publisher) { throw '原目录中的应用身份与备份不匹配。' }
    Repair-Identity
}
try {
    if ([Environment]::OSVersion.Version.Build -lt 19041) { exit 23 }
    if ($Restore) { Restore-Identity }
    else {
        Initialize-Identity $AppDirectory
        Save-IdentityState
        Repair-Identity
    }
    exit 0
} catch {
    [Console]::Error.WriteLine(($_ | Out-String))
    exit 1
}
)ps"
