param(
    [string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path,
    [string]$BuildDir = "build-android",
    [string]$Abi = "arm64-v8a",
    [string]$Platform = "android-29",
    [string]$VulkanReposRoot = "",
    [string]$Generator = "Ninja"
)

if (-not $VulkanReposRoot) {
    $VulkanReposRoot = (Resolve-Path "$RepoRoot\..\vulkan_repositories_extracted\vulkan_repos").Path
}

if (-not $env:ANDROID_NDK) {
    $defaultNdkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk\ndk"
    if (Test-Path $defaultNdkRoot) {
        $candidate = Get-ChildItem -Path $defaultNdkRoot -Directory |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if ($candidate) {
            $env:ANDROID_NDK = $candidate.FullName
            Write-Host "ANDROID_NDK not set. Using detected NDK: $($env:ANDROID_NDK)"
        }
    }
}

if (-not $env:ANDROID_NDK) {
    Write-Error "ANDROID_NDK env var is not set and no local NDK was auto-detected."
    exit 1
}

$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $ninja) {
    $wingetNinja = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe"
    if (Test-Path (Join-Path $wingetNinja "ninja.exe")) {
        $env:PATH = "$wingetNinja;$env:PATH"
        $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    }
}

if ($Generator -eq "Ninja" -and -not $ninja) {
    Write-Error "Ninja generator selected but ninja.exe was not found."
    exit 1
}

$toolchainFile = Join-Path $env:ANDROID_NDK "build\cmake\android.toolchain.cmake"
if (-not (Test-Path $toolchainFile)) {
    Write-Error "Android CMake toolchain not found at: $toolchainFile"
    exit 1
}

Set-Location $RepoRoot

cmake -S . -B $BuildDir `
    -G "$Generator" `
    -DEXYNOS_LAYER_USE_SUBMODULE_DEPS=ON `
    -DEXYNOS_LAYER_USE_LOCAL_VULKAN_REPOS=ON `
    -DEXYNOS_VULKAN_REPOS_ROOT="$VulkanReposRoot" `
    -DANDROID=ON `
    -DCMAKE_TOOLCHAIN_FILE="$toolchainFile" `
    -DANDROID_ABI="$Abi" `
    -DANDROID_PLATFORM="$Platform"

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build $BuildDir --config Release
exit $LASTEXITCODE
