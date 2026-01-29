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
# Force UTF-8 encoding for console output
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# Path resolution
$ScriptRoot = $PSScriptRoot
$ProjectRoot = Resolve-Path "$ScriptRoot\.."
$BuildDir = "$ProjectRoot\build"  # Always use build/ (GPU flag is passed to CMake)

# Default to Dev if no mode specified
if (-not ($Dev -or $Prod)) { $Dev = $true }

# =============================================================================
# Development Build
# =============================================================================
if ($Dev) {
    Write-Host "`n🔧 Backend - Development Build`n" -ForegroundColor Cyan
    
    if ($Gpu) { Write-Host "  GPU: Enabled" -ForegroundColor Green }
    Write-Host "  Compiler: $Compiler" -ForegroundColor DarkGray
    Write-Host "  Build: $BuildDir" -ForegroundColor DarkGray
    
    # Clean
    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host "  Cleaning..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDir
    }
    
    # Check MSVC environment if using MSVC or Clang (clang-cl requires MSVC env)
    if (($Compiler -eq "MSVC" -or $Compiler -eq "Clang" -or $Gpu) -and -not (Get-Command "cl.exe" -ErrorAction SilentlyContinue)) {
        Write-Host "Initializing MSVC environment..." -ForegroundColor Yellow
        
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        $vcvars = $null

        if (Test-Path $vswhere) {
            $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($path) {
                $vcvars = Join-Path $path "VC\Auxiliary\Build\vcvars64.bat"
            }
        }
        
        # Fallback to hardcoded paths if vswhere fails/missing (unlikely but good safety)
        if (-not ($vcvars -and (Test-Path $vcvars))) {
            $vcvarsPaths = @(
                "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
                "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
                "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
            )
            $vcvars = $vcvarsPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
        }

        if ($vcvars -and (Test-Path $vcvars)) {
            Write-Host "  Using vcvars: $vcvars" -ForegroundColor DarkGray
            $tempFile = [System.IO.Path]::GetTempFileName()
            cmd /c "`"$vcvars`" && set > `"$tempFile`""
            Get-Content $tempFile | ForEach-Object {
                if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "env:\$($matches[1])" -Value $matches[2] }
            }
            Remove-Item $tempFile
        }
        else {
            Write-Error "MSVC not found but required for MSVC/Clang/GPU build. Run from Developer Command Prompt."
            exit 1
        }
    }
    
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
    
    & cmake $CMakeArgs
    if ($LASTEXITCODE -ne 0) { exit 1 }
    
    # Build
    Write-Host "`n[2/2] Building..." -ForegroundColor Cyan
    cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { exit 1 }
    
    Write-Host "`n✅ Build Successful!" -ForegroundColor Green
}

# =============================================================================
# Production Build
# =============================================================================
elseif ($Prod) {
    $PackageName = if ($Gpu) { "Signeo-Backend-Win64-GPU" } else { "Signeo-Backend-Win64" }
    $DistDir = "$ProjectRoot\dist"
    $InstallPrefix = "$DistDir\$PackageName"
    
    Write-Host "`n📦 Backend - Production Build`n" -ForegroundColor Magenta
    Write-Host "  Package: $PackageName" -ForegroundColor White
    if ($Gpu) { Write-Host "  GPU: Enabled" -ForegroundColor Green }
    
    # Clean
    if ($Clean -or -not (Test-Path $BuildDir)) {
        Write-Host "  Cleaning..." -ForegroundColor Yellow
        if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }
    
    # Initialize MSVC environment (required for Release builds)
    if (-not (Get-Command "cl.exe" -ErrorAction SilentlyContinue)) {
        Write-Host "Initializing MSVC environment..." -ForegroundColor Yellow
        
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        $vcvars = $null

        if (Test-Path $vswhere) {
            $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($path) {
                $vcvars = Join-Path $path "VC\Auxiliary\Build\vcvars64.bat"
            }
        }
        
        if (-not ($vcvars -and (Test-Path $vcvars))) {
            $vcvarsPaths = @(
                "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
                "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
                "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
            )
            $vcvars = $vcvarsPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
        }

        if ($vcvars -and (Test-Path $vcvars)) {
            Write-Host "  Using vcvars: $vcvars" -ForegroundColor DarkGray
            $tempFile = [System.IO.Path]::GetTempFileName()
            cmd /c "`"$vcvars`" && set > `"$tempFile`""
            Get-Content $tempFile | ForEach-Object {
                if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "env:\$($matches[1])" -Value $matches[2] }
            }
            Remove-Item $tempFile
        }
        else {
            Write-Error "MSVC not found. Run from Developer Command Prompt or install Visual Studio."
            exit 1
        }
    }
    
    # Configure (Release)
    Write-Host "`n[1/3] Configuring (Release)..." -ForegroundColor Cyan
    $CMakeArgs = @("-S", $ProjectRoot, "-B", $BuildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
    $CMakeArgs += "-DENABLE_GPU=$(if ($Gpu) {'ON'} else {'OFF'})"
    if ($Gpu) { $CMakeArgs += "-DCMAKE_C_COMPILER=cl"; $CMakeArgs += "-DCMAKE_CXX_COMPILER=cl" }
    
    & cmake $CMakeArgs
    if ($LASTEXITCODE -ne 0) { exit 1 }
    
    # Build
    Write-Host "`n[2/3] Building..." -ForegroundColor Cyan
    cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) { exit 1 }
    
    # Install
    Write-Host "`n[3/3] Installing..." -ForegroundColor Cyan
    if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
    cmake --install $BuildDir --config Release --prefix $InstallPrefix --component Application
    
    Write-Host "`n✅ Build complete: $InstallPrefix" -ForegroundColor Green
}
