$ErrorActionPreference = 'Stop'

$version = 'v0.5.4'
$archive = 'toolchain-llvm-18.tar.gz'
$expectedSha256 = '3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526'
$cacheDirectory = Join-Path $PSScriptRoot '..\.cache'
$destination = Join-Path $cacheDirectory $archive

New-Item -ItemType Directory -Force -Path $cacheDirectory | Out-Null

if (-not (Test-Path -LiteralPath $destination)) {
    gh release download $version `
        --repo OpenOrbis/OpenOrbis-PS4-Toolchain `
        --pattern $archive `
        --dir $cacheDirectory

    if ($LASTEXITCODE -ne 0) {
        throw 'OpenOrbis download failed.'
    }
}

$actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
    throw "OpenOrbis checksum mismatch. Expected $expectedSha256, received $actualSha256."
}

Write-Output "Verified $destination"
