# release.ps1 - Automated Release Packaging for Signeo Backend
# Relies on correct CMake install rules logic.

param (
    [switch]$Clean,
    [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

# Determine Repo Root (Since this script is in scripts/)
$RepoRoot = Resolve-Path "$PSScriptRoot/.."
Set-Location $RepoRoot

# Configuration
$BuildDir = "build"
$DistDir = "dist"
$PackageName = "Signeo-Backend-Win64"
$InstallPrefix = "$DistDir/$PackageName"

Write-Host "🚀 Starting Release Process..." -ForegroundColor Cyan

# 1. Clean
if ($Clean -or !(Test-Path $BuildDir)) {
    Write-Host "🧹 Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path $BuildDir) { Remove-Item -Path $BuildDir -Recurse -Force }
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# 2. Configure (Release)
Write-Host "🔧 Configuring..." -ForegroundColor Cyan
& cmake -S . -B $BuildDir -G $Generator -DCMAKE_BUILD_TYPE=Release
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
