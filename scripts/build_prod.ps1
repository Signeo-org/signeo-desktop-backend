# release.ps1 - Automated Release Packaging for Signeo Backend
# Relies on correct CMake install rules logic.

param (
    [switch]$Clean,
    [switch]$Gpu,
    [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

# Determine Repo Root (Since this script is in scripts/)
$RepoRoot = Resolve-Path "$PSScriptRoot/.."
Set-Location $RepoRoot

# Configuration
$BuildDir = "build"
if ($Gpu) { 
    $BuildDir = "build_gpu" 
    $PackageName = "Signeo-Backend-Win64-GPU"
} else {
    $PackageName = "Signeo-Backend-Win64"
}

$DistDir = "dist"
$InstallPrefix = "$DistDir/$PackageName"

Write-Host "🚀 Starting Release Process for $PackageName..." -ForegroundColor Cyan

# 0. Check MSVC for GPU (reuse same logic or assume user has it, or call build.ps1?)
# For simplicity, we assume user environment is correct OR we pass correct flags.
# To be safe, let's just pass ENABLE_GPU=ON and assume environment is set (or rely on cmake to find it)
# Best practice: Re-use build.ps1/bat logic or just require environment.
# Given build.ps1 automates it, we could call it, but release cleans/installs differently.
# We'll just set flags.

# 1. Clean
if ($Clean -or !(Test-Path $BuildDir)) {
    Write-Host "🧹 Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path $BuildDir) { Remove-Item -Path $BuildDir -Recurse -Force }
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# 2. Configure (Release)
Write-Host "🔧 Configuring..." -ForegroundColor Cyan
$CMakeBaseArgs = @("-S", ".", "-B", "$BuildDir", "-G", "$Generator", "-DCMAKE_BUILD_TYPE=Release")

if ($Gpu) {
    $CMakeBaseArgs += "-DENABLE_GPU=ON"
    $CMakeBaseArgs += "-DCMAKE_C_COMPILER=cl"
    $CMakeBaseArgs += "-DCMAKE_CXX_COMPILER=cl"
    # Ensure MSVC env is active if not already? 
    # The user might be running this from a normal shell. 
    # Ideally release script uses build.ps1 logic or simply fails if nvcc can't compile.
}

& cmake $CMakeBaseArgs
if ($LASTEXITCODE -ne 0) { exit 1 }

# 3. Build
Write-Host "🔨 Building..." -ForegroundColor Cyan
& cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { exit 1 }

# 4. Install (Stages files to dist/)
Write-Host "📦 Installing to $InstallPrefix..." -ForegroundColor Cyan
if (Test-Path $DistDir) { Remove-Item -Path $DistDir -Recurse -Force }
& cmake --install $BuildDir --config Release --prefix $InstallPrefix --component Application

# 5. Zip
Write-Host "🤐 Zipping..." -ForegroundColor Cyan
$ZipPath = Join-Path $DistDir "$PackageName.zip"
Compress-Archive -Path "$InstallPrefix/*" -DestinationPath $ZipPath -Force

Write-Host "✅ Done! Package: $ZipPath" -ForegroundColor Green
