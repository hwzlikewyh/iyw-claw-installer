param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$project = Split-Path -Parent $PSScriptRoot
$caseRoot = Join-Path $project ('.cache/update-lock-probe-' + [guid]::NewGuid().ToString('N'))
$application = Join-Path $caseRoot 'app'
$backup = Join-Path $caseRoot 'previous-app'
$process = $null
$idleTimeoutMs = 15000
$exitTimeoutMs = 5000
[IO.Directory]::CreateDirectory($application) | Out-Null
try {
    # 只启动欢迎页，不执行安装；模拟父应用传给向导的 app 工作目录。
    $process = Start-Process -FilePath $executablePath -WorkingDirectory $application -WindowStyle Hidden -PassThru
    if (!$process.WaitForInputIdle($idleTimeoutMs)) { throw 'Installer did not initialize' }
    $process.Refresh()
    if ($process.HasExited) { throw 'Installer exited before the directory test' }
    [IO.Directory]::Move($application, $backup)
    [IO.Directory]::Move($backup, $application)
    Write-Output 'Inherited app directory can be renamed while the installer remains running'
} finally {
    if ($process -and !$process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (!$process.WaitForExit($exitTimeoutMs)) { $process.Kill(); $process.WaitForExit() }
    }
    # 只删除本脚本创建的空目录；意外出现文件时保留现场。
    foreach ($directory in @($application, $backup, $caseRoot)) {
        if (Test-Path -LiteralPath $directory) { [IO.Directory]::Delete($directory) }
    }
}
