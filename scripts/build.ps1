<#
.SYNOPSIS
    Unified build script for Signeo Backend.

.PARAMETER Dev
    Development build (default).

.PARAMETER Prod
    Release build with packaging.

.PARAMETER Gpu
    Enable GPU (CUDA) support.

.PARAMETER Clean
    Clean rebuild.

.PARAMETER Tidy
    Enable clang-tidy.

.PARAMETER Format
    Enable clang-format.

.PARAMETER Compiler
    Compiler selection: Auto, MSVC, Clang.

.EXAMPLE
    .\build.ps1                    # Dev build
    .\build.ps1 -Prod              # Release build
    .\build.ps1 -Gpu               # GPU dev build
    .\build.ps1 -Prod -Gpu -Clean  # Clean GPU release
#>

param (
    [switch]$Dev,
    [switch]$Prod,
    [switch]$Gpu,
    [switch]$Clean,
    [switch]$Tidy,
    [switch]$Format,
    [ValidateSet("Auto", "MSVC", "Clang")]
    [string]$Compiler = "Auto"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# Path resolution
$ScriptRoot   = $PSScriptRoot
$ProjectRoot  = Resolve-Path (Join-Path $ScriptRoot "..")
$BuildDir     = Join-Path $ProjectRoot "build"  # Always use build/ (GPU flag is passed to CMake)

# Default to Dev if no mode specified
if (-not ($Dev -or $Prod)) { $Dev = $true }

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------
function Import-VcVarsEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VcVarsPath
    )

    if (-not (Test-Path $VcVarsPath)) {
        throw "vcvars file not found: $VcVarsPath"
    }

    $tempFile = Join-Path $env:TEMP ("signeo_env_" + [guid]::NewGuid().ToString("N") + ".txt")
    $batFile  = Join-Path $env:TEMP ("signeo_vcvars_" + [guid]::NewGuid().ToString("N") + ".bat")

    try {
        # Build a tiny .bat to avoid using `&&` (prevents PowerShell parsing issues)
        @"
@echo off
call "$VcVarsPath"
set > "$tempFile"
"@ | Set-Content -Path $batFile -Encoding ASCII

        cmd /c "`"$batFile`""
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to run vcvars. (exit=$LASTEXITCODE)"
        }

        if (-not (Test-Path $tempFile)) {
            throw "Environment dump file missing: $tempFile"
        }

        Get-Content $tempFile | ForEach-Object {
            if ($_ -match "^(.*?)=(.*)$") {
                # Set in current process
                Set-Item -Path ("env:\{0}" -f $matches[1]) -Value $matches[2]
            }
        }
    }
    finally {
        Remove-Item -Force $batFile -ErrorAction SilentlyContinue
        Remove-Item -Force $tempFile -ErrorAction SilentlyContinue
    }
}

function Find-VcVars64 {
    # vswhere path (most reliable)
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $path) {
            $candidate = Join-Path $path "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $candidate) { return $candidate }
        }
    }

    # Fallback paths (note: Build Tools/VS is usually in Program Files (x86))
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )

    return ($candidates | Where-Object { Test-Path $_ } | Select-Object -First 1)
}

function Ensure-MsvcEnvIfNeeded {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CompilerChoice,
        [Parameter(Mandatory = $true)]
        [bool]$GpuEnabled
    )

    # Need MSVC env if:
    # - Compiler is MSVC
    # - Compiler is Clang (clang-cl)
    # - GPU build (your script treats GPU as requiring MSVC toolchain)
    $need = ($CompilerChoice -eq "MSVC" -or $CompilerChoice -eq "Clang" -or $GpuEnabled)

    if (-not $need) { return }

    if (Get-Command "cl.exe" -ErrorAction SilentlyContinue) {
        return
    }

    Write-Host "Initializing MSVC environment..." -ForegroundColor Yellow

    $vcvars = Find-VcVars64
    if (-not $vcvars) {
        Write-Error "MSVC not found but required for MSVC/Clang/GPU build. Install VS Build Tools or run from Developer Command Prompt."
        exit 1
    }

    Write-Host "  Using vcvars: $vcvars" -ForegroundColor DarkGray
    Import-VcVarsEnvironment -VcVarsPath $vcvars
}

# =============================================================================
# Development Build
# =============================================================================
if ($Dev) {
    Write-Host "`nBackend - Development Build`n" -ForegroundColor Cyan

    if ($Gpu) { Write-Host "  GPU: Enabled" -ForegroundColor Green }
    Write-Host "  Compiler: $Compiler" -ForegroundColor DarkGray
    Write-Host "  Build: $BuildDir" -ForegroundColor DarkGray

    # Clean
    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host "  Cleaning..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDir
    }

    # Ensure MSVC env if needed
    Ensure-MsvcEnvIfNeeded -CompilerChoice $Compiler -GpuEnabled ([bool]$Gpu)

    # Configure
    Write-Host "`n[1/2] Configuring..." -ForegroundColor Cyan
    $CMakeArgs = @("-S", $ProjectRoot, "-B", $BuildDir, "-G", "Ninja")
    $CMakeArgs += "-DENABLE_GPU=$(if ($Gpu) {'ON'} else {'OFF'})"
    $CMakeArgs += "-DENABLE_CLANG_TIDY=$(if ($Tidy) {'ON'} else {'OFF'})"
    $CMakeArgs += "-DENABLE_CLANG_FORMAT=$(if ($Format) {'ON'} else {'OFF'})"

    # Compiler selection
    if ($Compiler -eq "MSVC") {
        $CMakeArgs += "-DCMAKE_C_COMPILER=cl"
        $CMakeArgs += "-DCMAKE_CXX_COMPILER=cl"
    }
    elseif ($Compiler -eq "Clang") {
        $CMakeArgs += "-DCMAKE_C_COMPILER=clang-cl"
        $CMakeArgs += "-DCMAKE_CXX_COMPILER=clang-cl"
    }

    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) { exit 1 }

    # Build
    Write-Host "`n[2/2] Building..." -ForegroundColor Cyan
    cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { exit 1 }

    Write-Host "`nBuild Successful!" -ForegroundColor Green
}

# =============================================================================
# Production Build
# =============================================================================
elseif ($Prod) {
    $PackageName = if ($Gpu) { "Signeo-Backend-Win64-GPU" } else { "Signeo-Backend-Win64" }
    $DistDir = Join-Path $ProjectRoot "dist"
    $InstallPrefix = Join-Path $DistDir $PackageName

    Write-Host "`nBackend - Production Build`n" -ForegroundColor Magenta
    Write-Host "  Package: $PackageName" -ForegroundColor White
    if ($Gpu) { Write-Host "  GPU: Enabled" -ForegroundColor Green }

    # Clean
    if ($Clean -or -not (Test-Path $BuildDir)) {
        Write-Host "  Cleaning..." -ForegroundColor Yellow
        if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }

    # Ensure MSVC env (your original script required it for Release)
    Ensure-MsvcEnvIfNeeded -CompilerChoice "MSVC" -GpuEnabled $true

    # Configure (Release)
    Write-Host "`n[1/3] Configuring (Release)..." -ForegroundColor Cyan
    $CMakeArgs = @("-S", $ProjectRoot, "-B", $BuildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
    $CMakeArgs += "-DENABLE_GPU=$(if ($Gpu) {'ON'} else {'OFF'})"

    # Keep your original behavior for GPU: enforce cl
    if ($Gpu) {
        $CMakeArgs += "-DCMAKE_C_COMPILER=cl"
        $CMakeArgs += "-DCMAKE_CXX_COMPILER=cl"
    }

    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) { exit 1 }

    # Build
    Write-Host "`n[2/3] Building..." -ForegroundColor Cyan
    cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) { exit 1 }

    # Install
    Write-Host "`n[3/3] Installing..." -ForegroundColor Cyan
    if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
    cmake --install $BuildDir --config Release --prefix $InstallPrefix --component Application
    if ($LASTEXITCODE -ne 0) { exit 1 }

    Write-Host "`nBuild complete: $InstallPrefix" -ForegroundColor Green
}
