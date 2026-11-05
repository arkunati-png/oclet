# Fetches upstream sources into vendor/. Pin versions here so builds are reproducible.
$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent $PSScriptRoot
$vendor = Join-Path $root 'vendor'
New-Item -ItemType Directory -Force $vendor | Out-Null

# Pin to a release tag you have verified. Update deliberately.
$bitcoinTag = 'v32.1'
$randomxTag = 'v1.2.1'

function Get-Repo($url, $tag, $dir) {
    if (Test-Path $dir) { Write-Host "$dir already exists, skipping"; return }
    git clone --depth 1 --branch $tag $url $dir
}

Get-Repo 'https://github.com/bitcoin/bitcoin.git'     $bitcoinTag (Join-Path $vendor 'bitcoin')
Get-Repo 'https://github.com/tevador/RandomX.git'     $randomxTag (Join-Path $vendor 'RandomX')

Write-Host "Done. Sources are in $vendor"
