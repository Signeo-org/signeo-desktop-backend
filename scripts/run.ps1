# Detect if running in Visual Studio Developer PowerShell
if ($env:VSCMD_VER) {
    Write-Host "Visual Studio Developer PowerShell detected, skipping tool checks."

    # ----------------------------------
    # 1) Initialize and Update Submodules
    # ----------------------------------
    Write-Host "Updating git submodules..."
    git submodule update --init --recursive

    # -----------------------------------------------------
    # 1.5) Download model into ../models/ if not present
    # -----------------------------------------------------
    Write-Host "Checking for model file..."
    $modelsDir = "../models"
    $modelPath = Join-Path $modelsDir "ggml-base.en.bin"

    if (-not (Test-Path -Path $modelPath)) {
        Write-Host "Model not found. Downloading ggml-base.en.bin..."
        if (-not (Test-Path -Path $modelsDir)) {
            New-Item -ItemType Directory -Path $modelsDir | Out-Null
        }
        Invoke-WebRequest -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin" `
            -OutFile $modelPath
    }
    else {
        Write-Host "Model already exists. Skipping download."
    }

    # ----------------------------------
    # 2) Create / Enter Build Directory
    # ----------------------------------
    $vsBuildDir = Join-Path $PSScriptRoot "build_vs"
    if (-not (Test-Path -Path $vsBuildDir)) {
        New-Item -ItemType Directory -Path $vsBuildDir | Out-Null
    }

    if (Test-Path "$vsBuildDir/CMakeCache.txt") {
        Write-Host "Removing old Visual Studio CMake cache and files..."
        Remove-Item "$vsBuildDir/CMakeCache.txt" -Force
    }
    if (Test-Path "$vsBuildDir/CMakeFiles") {
        Remove-Item "$vsBuildDir/CMakeFiles" -Recurse -Force
    }

    Set-Location -Path $vsBuildDir

    Write-Host "Configuring CMake with Visual Studio generator..."
    $sourceDir = Resolve-Path "$PSScriptRoot/../"
    cmake -G "Visual Studio 17 2022" -A x64 -D BUILD_SHARED_LIBS=ON -D CMAKE_BUILD_TYPE=Release $sourceDir

    Write-Host "Building project with Visual Studio generator..."
    cmake --build . --config Release

    $exePath = Join-Path $vsBuildDir "Release\AudioCapturePlayback.exe"
    Start-Process -FilePath $exePath -WorkingDirectory $sourceDir -NoNewWindow -Wait

    Set-Location -Path $PSScriptRoot

}
else {
    # --------------------------------------
    # 0) Ensure MinGW, CMake and Ninja Are Available
    # --------------------------------------
    Write-Host "Checking for GCC (MinGW)..."
    $gccExists = Get-Command gcc -ErrorAction SilentlyContinue
    $gppExists = Get-Command g++ -ErrorAction SilentlyContinue
    $existingMingwPath = "$PSScriptRoot/tools/mingw64/bin"

    if (Test-Path $existingMingwPath) {
        Write-Host "Adding existing MinGW to PATH"
        $env:PATH = "$existingMingwPath;$env:PATH"
    }

    if (-not (Get-Command gcc -ErrorAction SilentlyContinue) -or -not (Get-Command g++ -ErrorAction SilentlyContinue)) {
        Write-Host "Missing compiler (MinGW). Attempting to install..."
        & "$PSScriptRoot/ensure-tools.ps1"
    
        # Relaunch a *new* PowerShell process to reload environment variables and run this script again
        Write-Host "Relaunching build script with updated environment..."
    
        Start-Process -FilePath "powershell.exe" -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Wait
        exit 0
    }

    Write-Host "Checking for CMake and Ninja..."
    $cmakeExists = Get-Command cmake -ErrorAction SilentlyContinue
    $ninjaExists = Get-Command ninja -ErrorAction SilentlyContinue

    if (-not $cmakeExists -or -not $ninjaExists) {
        Write-Host "Missing tools detected. Attempting to install..."
        & "$PSScriptRoot/ensure-tools.ps1"


        Write-Host "Relaunching build script..."
        Start-Process -FilePath "powershell.exe" -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`""
        exit 0
    }

    # Check for ASIO SDK after tool installation (only for Ninja flow)
    $asioBuildDirRaw = "$PSScriptRoot/../external/portaudio/build"
    try {
        $asioBuildDir = Resolve-Path $asioBuildDirRaw -ErrorAction Stop
        $asioSDKPath = Join-Path $asioBuildDir "asiosdk"
    }
    catch {
        Write-Warning "Could not resolve ASIO build directory. Using raw path."
        $asioSDKPath = Join-Path $asioBuildDirRaw "asiosdk"
    }

    if (-not (Test-Path $asioSDKPath)) {
        Write-Host "ASIO SDK not found. Attempting to install..."
        & "$PSScriptRoot/ensure-tools.ps1"


        if (-not (Test-Path $asioSDKPath)) {
            Write-Warning "ASIO SDK installation failed or still missing."
            Write-Warning "Download it manually from:"
            Write-Warning "https://www.steinberg.net/en/company/developers.html"
            Write-Warning "and extract to: $asioSDKPath"
        }
    }
    else {
        Write-Host "ASIO SDK found."
    }

    # ----------------------------------
    # 1) Initialize and Update Submodules
    # ----------------------------------
    Write-Host "Updating git submodules..."
    git submodule update --init --recursive

    # -----------------------------------------------------
    # 1.5) Download model into ../models/ if not present
    # -----------------------------------------------------
    Write-Host "Checking for model file..."
    $modelsDir = "../models"
    $modelPath = Join-Path $modelsDir "ggml-base.en.bin"

    if (-not (Test-Path -Path $modelPath)) {
        Write-Host "Model not found. Downloading ggml-base.en.bin..."
        if (-not (Test-Path -Path $modelsDir)) {
            New-Item -ItemType Directory -Path $modelsDir | Out-Null
        }
        Invoke-WebRequest -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin" `
            -OutFile $modelPath
    }
    else {
        Write-Host "Model already exists. Skipping download."
    }

    # ----------------------------------
    # 2) Create / Enter Build Directory
    # ----------------------------------
    $ninjaBuildDir = Join-Path $PSScriptRoot "build_ninja"

    if (-not (Test-Path -Path $ninjaBuildDir)) {
        New-Item -ItemType Directory -Path $ninjaBuildDir | Out-Null
    }
    if (Test-Path "$ninjaBuildDir/CMakeCache.txt") {
        Write-Host "Removing old Ninja CMake cache and files..."
        Remove-Item "$ninjaBuildDir/CMakeCache.txt" -Force
    }
    if (Test-Path "$ninjaBuildDir/CMakeFiles") {
        Remove-Item "$ninjaBuildDir/CMakeFiles" -Recurse -Force
    }

    Set-Location -Path $ninjaBuildDir

    Write-Host "Configuring CMake with Ninja..."
    $sourceDir = Resolve-Path "$PSScriptRoot/../"
    cmake -G "Ninja" -D BUILD_SHARED_LIBS=ON -D CMAKE_BUILD_TYPE=Release `
      -D CMAKE_C_FLAGS="-march=haswell -mbmi2 -mavx2 -O3" `
      -D CMAKE_CXX_FLAGS="-march=haswell -mbmi2 -mavx2 -O3" `
      $sourceDir

    Write-Host "Building project..."
    ninja

    $exePath = Join-Path $ninjaBuildDir "AudioCapturePlayback.exe"
    Start-Process -FilePath $exePath -WorkingDirectory $sourceDir -NoNewWindow -Wait

    Set-Location -Path $PSScriptRoot
}