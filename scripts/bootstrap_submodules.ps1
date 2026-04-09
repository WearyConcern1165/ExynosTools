param(
    [string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
)

Set-Location $RepoRoot

if (-not (Test-Path ".git")) {
    Write-Host "Not a git repository. Initialize git first."
    exit 1
}

if (-not (Test-Path "external")) {
    New-Item -ItemType Directory -Path "external" | Out-Null
}

if (-not (Test-Path "external\Vulkan-Headers")) {
    git submodule add https://github.com/KhronosGroup/Vulkan-Headers.git external/Vulkan-Headers
}

if (-not (Test-Path "external\Vulkan-Utility-Libraries")) {
    git submodule add https://github.com/KhronosGroup/Vulkan-Utility-Libraries.git external/Vulkan-Utility-Libraries
}

if (-not (Test-Path "external\VulkanMemoryAllocator")) {
    git submodule add https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git external/VulkanMemoryAllocator
}

git submodule update --init --recursive
Write-Host "Submodules ready."

