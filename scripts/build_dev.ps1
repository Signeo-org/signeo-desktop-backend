<#
.SYNOPSIS
    Builds the project (CPU by default, GPU optional).
    Automatically sets up the Visual Studio environment if needed for GPU builds.

.DESCRIPTION
    This script:
    1. Checks if GPU support is requested (-Gpu).
    2. If GPU is requested, checks/initializes the MSVC environment (cl.exe).
    3. Configures the project using CMake (Ninja Generator).
    4. Builds the project.

.PARAMETER Gpu
    Enables GPU support (CUDA + ONNX Runtime).

.PARAMETER Clean
    Cleans the build directory before building.

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Gpu
    .\scripts\build.ps1 -Gpu -Clean
#>

param (
    [switch]$Gpu,
    [switch]$Clean,
    [switch]$Tidy,
    [switch]$Format
)

$ScriptRoot = $PSScriptRoot
$ProjectRoot = Resolve-Path "$ScriptRoot\.."
$BuildDir = "$ProjectRoot\build"

if ($Gpu) {
    $BuildDir = "$ProjectRoot\build_gpu"
    Write-Host "Building with GPU Support..." -ForegroundColor Cyan
} else {
    Write-Host "Building for CPU..." -ForegroundColor Cyan
}

Write-Host "Project Root: $ProjectRoot" -ForegroundColor Gray

# 0. Clean if requested
if ($Clean) {
    if (Test-Path $BuildDir) {
        Write-Host "Cleaning build directory: $BuildDir" -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDir
    }
}

# 1. Check for MSVC Environment (Critical for CUDA/GPU builds on Windows)
if ($Gpu) {
    if (-not (Get-Command "cl.exe" -ErrorAction SilentlyContinue)) {
        Write-Host "MSVC environment not detected (Required for CUDA). Attempting to initialize..." -ForegroundColor Yellow
        
        # Common vcvars64.bat locations
        $vcvarsPaths = @(
            "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
            "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
            "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
        )

        $vcvars = $null
        foreach ($path in $vcvarsPaths) {
            if (Test-Path $path) {
                $vcvars = $path
                break
            }
        }

        if ($vcvars) {
            Write-Host "Found vcvars64.bat at: $vcvars" -ForegroundColor Green
            
            $tempFile = [System.IO.Path]::GetTempFileName()
            cmd /c " `"$vcvars`" && set > `"$tempFile`" "
            
            Get-Content $tempFile | ForEach-Object {
                if ($_ -match "^(.*?)=(.*)$") {
                    Set-Item -Path "env:\$($matches[1])" -Value $matches[2]
                }
            }
            Remove-Item $tempFile
        } else {
            Write-Error "Could not find vcvars64.bat. Please run this script from a Visual Studio Developer Command Prompt."
            exit 1
        }
    } else {
        Write-Host "MSVC environment detected." -ForegroundColor Green
    }
}

# 2. Configure CMake
Write-Host "`n[1/2] Configuring CMake..." -ForegroundColor Cyan

$CMakeArgs = @("-S", "$ProjectRoot", "-B", "$BuildDir", "-G", "Ninja")

if ($Gpu) {
    $CMakeArgs += "-DENABLE_GPU=ON"
    $CMakeArgs += "-DCMAKE_C_COMPILER=cl"
    $CMakeArgs += "-DCMAKE_CXX_COMPILER=cl"
} else {
    $CMakeArgs += "-DENABLE_GPU=OFF"
}

# Tidy Logic
if ($Tidy) {
    Write-Host "Enabled: Clang-Tidy" -ForegroundColor DarkGray
    $CMakeArgs += "-DENABLE_CLANG_TIDY=ON"
} else {
    $CMakeArgs += "-DENABLE_CLANG_TIDY=OFF"
}

# Format Logic
if ($Format) {
    Write-Host "Enabled: Clang-Format Target" -ForegroundColor DarkGray
    $CMakeArgs += "-DENABLE_CLANG_FORMAT=ON"
} else {
    $CMakeArgs += "-DENABLE_CLANG_FORMAT=OFF"
}

Write-Host "Running: cmake $CMakeArgs" -ForegroundColor DarkGray
& cmake $CMakeArgs

if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed."
    exit 1
}

# 3. Build
Write-Host "`n[2/2] Building..." -ForegroundColor Cyan
cmake --build "$BuildDir"

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild Successful!" -ForegroundColor Green
} else {
    Write-Error "Build failed."
    exit 1
}
