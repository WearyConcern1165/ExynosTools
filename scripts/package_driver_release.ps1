param(
    [string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path,
    [string]$BuildDir = "build-android",
    [string]$ReleaseName = "ExynosTools_V3.0_STABLE",
    [string]$DesktopDir = "$env:USERPROFILE\Desktop"
)

$ErrorActionPreference = "Stop"

$buildPath = Join-Path $RepoRoot $BuildDir
$soPath = Join-Path $buildPath "libVkLayer_ExynosTools.so"
if (-not (Test-Path $soPath)) {
    throw "Missing .so: $soPath. Build first with scripts/configure_android_local_repos.ps1"
}

$releaseDir = Join-Path $DesktopDir $ReleaseName
$zipPath = Join-Path $DesktopDir "$ReleaseName.zip"

if (Test-Path $releaseDir) {
    Remove-Item -LiteralPath $releaseDir -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

New-Item -ItemType Directory -Path $releaseDir | Out-Null
New-Item -ItemType Directory -Path (Join-Path $releaseDir "lib\arm64-v8a") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $releaseDir "shaders") -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $RepoRoot "CHANGELOG_V3.0.txt") -Destination (Join-Path $releaseDir "CHANGELOG_V3.0.txt")
Copy-Item -LiteralPath (Join-Path $RepoRoot "exynostools_config.ini") -Destination (Join-Path $releaseDir "exynostools_config.ini")
Copy-Item -LiteralPath (Join-Path $RepoRoot "meta.json") -Destination (Join-Path $releaseDir "meta.json")
Copy-Item -LiteralPath $soPath -Destination (Join-Path $releaseDir "lib\arm64-v8a\libVkLayer_ExynosTools.so")
if (Test-Path (Join-Path $buildPath "shaders")) {
    Get-ChildItem -Path (Join-Path $buildPath "shaders") -Filter "*.spv" -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $releaseDir "shaders\$($_.Name)")
    }
}

# Zip without an extra top-level folder. The zip root contains files directly.
$zipItems = Get-ChildItem -LiteralPath $releaseDir | ForEach-Object { $_.FullName }
Compress-Archive -Path $zipItems -DestinationPath $zipPath -CompressionLevel Optimal

Write-Output "RELEASE_DIR=$releaseDir"
Write-Output "RELEASE_ZIP=$zipPath"
