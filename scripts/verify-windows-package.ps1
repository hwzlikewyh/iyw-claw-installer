param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedThumbprint
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$executable = Join-Path $root 'YuanAssistant-Setup-windows.exe'
$archive = Join-Path $root 'YuanAssistant-Setup-windows.zip'
$manifest = Join-Path $root 'SHA256SUMS.txt'

foreach ($path in @($executable, $archive, $manifest)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Windows package file is missing: $path"
    }
}

$signature = Get-AuthenticodeSignature -LiteralPath $executable
if ($signature.Status -ne 'Valid') {
    throw "Windows package signature is not valid: $($signature.Status)"
}
$expected = $ExpectedThumbprint.Replace(':', '').Replace(' ', '').ToUpperInvariant()
$actual = ''
if ($null -ne $signature.SignerCertificate) {
    $actual = $signature.SignerCertificate.Thumbprint.ToUpperInvariant()
}
if ($actual -ne $expected) {
    throw "Windows package signer mismatch: $actual"
}
if ($null -eq $signature.TimeStamperCertificate) {
    throw 'Windows package is missing an RFC 3161 timestamp'
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) "iyw-windows-package-$([guid]::NewGuid())"
try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $temporary -Force
    $embedded = @(Get-ChildItem -LiteralPath $temporary -Filter '*.exe' -File -Recurse)
    if ($embedded.Count -ne 1) {
        throw "Windows ZIP must contain exactly one EXE, found $($embedded.Count)"
    }
    $standaloneHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    $embeddedHash = (Get-FileHash -LiteralPath $embedded[0].FullName -Algorithm SHA256).Hash
    if ($standaloneHash -ne $embeddedHash) {
        throw 'Standalone EXE and ZIP EXE differ'
    }

    foreach ($line in [IO.File]::ReadAllLines($manifest, [Text.Encoding]::UTF8)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -notmatch '^([0-9a-fA-F]{64})\s+(.+)$') {
            throw "Invalid SHA256SUMS entry: $line"
        }
        $path = Join-Path $root $Matches[2]
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "SHA256SUMS entry is missing: $($Matches[2])"
        }
        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actualHash -ne $Matches[1]) {
            throw "SHA256SUMS mismatch: $($Matches[2])"
        }
    }
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Verified Windows package signer $actual and timestamp"
