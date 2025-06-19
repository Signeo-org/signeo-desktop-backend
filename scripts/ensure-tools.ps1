Write-Host "Installing missing tools (CMake/Ninja)..."

$toolsDir = "$PSScriptRoot/tools"
$cmakeDir = "$toolsDir/cmake"
$ninjaDir = "$toolsDir/ninja"

# Add tools to PATH (for current session)
$env:PATH = "$cmakeDir/bin;$ninjaDir;$env:PATH"

function Download-And-Extract {
    param (
        [string]$Url,
        [string]$OutputZip,
        [string]$ExtractTo
    )

    try {
        Invoke-WebRequest -Uri $Url -OutFile $OutputZip -UseBasicParsing
    }
    catch {
        Write-Error "Download failed: $Url"
        return $false
    }

    try {
        Expand-Archive -Path $OutputZip -DestinationPath $ExtractTo -Force
        Remove-Item $OutputZip
    }
    catch {
        Write-Error "Unzip failed: $OutputZip"
        return $false
    }

    return $true
}

# Ensure base tools dir exists
if (-not (Test-Path $toolsDir)) {
    New-Item -ItemType Directory -Path $toolsDir | Out-Null
}

# ----------------
# CMake Check
# ----------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "CMake not found. Downloading..."

    # Use an exact version and working download link
    $cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v3.29.3/cmake-3.29.3-windows-x86_64.zip"
    $cmakeZip = "$toolsDir/cmake.zip"

    if (Download-And-Extract -Url $cmakeUrl -OutputZip $cmakeZip -ExtractTo $toolsDir) {
        $cmakeExtracted = Get-ChildItem $toolsDir -Directory | Where-Object { $_.Name -like "cmake-*" } | Select-Object -First 1
        if ($cmakeExtracted) {
            Rename-Item -Path $cmakeExtracted.FullName -NewName "cmake"
            Write-Host "CMake installed successfully."
        }
        else {
            Write-Error "CMake was downloaded but could not be found after extraction."
        }
    }
}

# ----------------
# Ninja Check
# ----------------
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Host "Ninja not found. Downloading..."

    $ninjaUrl = "https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-win.zip"
    $ninjaZip = "$toolsDir/ninja.zip"

    if (-not (Test-Path $ninjaDir)) {
        New-Item -ItemType Directory -Path $ninjaDir | Out-Null
    }

    if (Download-And-Extract -Url $ninjaUrl -OutputZip $ninjaZip -ExtractTo $ninjaDir) {
        Write-Host "Ninja installed successfully."
    }
}

# ----------------
# MinGW Check
# ----------------
$mingwDir = "$toolsDir/mingw"

# Add to current session PATH first (in case already installed)
$mingwBinPath = Join-Path $mingwDir "bin"
if (-not ($env:PATH.Split(';') -contains $mingwBinPath)) {
    $env:PATH = "$mingwBinPath;$env:PATH"
}

if (-not (Get-Command gcc -ErrorAction SilentlyContinue) -or -not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Host "MinGW not found. Downloading ZIP archive..."

    $mingwUrl = "https://github.com/brechtsanders/winlibs_mingw/releases/download/15.1.0posix-13.0.0-ucrt-r2/winlibs-x86_64-posix-seh-gcc-15.1.0-mingw-w64ucrt-13.0.0-r2.zip"
    $mingwZip = "$toolsDir\mingw.zip"

    Invoke-WebRequest -Uri $mingwUrl -OutFile $mingwZip

    if (Test-Path $mingwDir) {
        Remove-Item -Recurse -Force $mingwDir
    }

    Write-Host "Extracting MinGW ZIP (this may take a while)..."
    Expand-Archive -Path $mingwZip -DestinationPath $toolsDir -Force
    Remove-Item $mingwZip

    # The extracted folder has a long name; rename to 'mingw' for consistency
    $extracted = Get-ChildItem $toolsDir -Directory | Where-Object { $_.Name -like "winlibs-*" } | Select-Object -First 1
    if ($extracted) {
        Rename-Item -Path $extracted.FullName -NewName "mingw"
        Write-Host "MinGW installed successfully."

        # Add MinGW bin path to current session PATH (after rename)
        $mingwBinPath = Join-Path $mingwDir "./tools/mingw64/bin"
        if (-not ($env:PATH.Split(';') -contains $mingwBinPath)) {
            Write-Host "Adding MinGW bin path to current session PATH..."
            $env:PATH = "$mingwBinPath;$env:PATH"
        }

        # Add MinGW bin path permanently to User PATH environment variable
        $currentUserPath = [Environment]::GetEnvironmentVariable("Path", [EnvironmentVariableTarget]::User)

        if (-not ($currentUserPath.Split(';') -contains $mingwBinPath)) {
            Write-Host "Adding MinGW bin path to User PATH environment variable..."
            $newUserPath = "$currentUserPath;$mingwBinPath"
            [Environment]::SetEnvironmentVariable("Path", $newUserPath, [EnvironmentVariableTarget]::User)
            Write-Host "MinGW bin path added to User PATH. Restart your PowerShell session to apply changes."
        }
        else {
            Write-Host "MinGW bin path already in User PATH."
        }
    }
    else {
        Write-Error "Failed to locate extracted MinGW folder."
        exit 1
    }
}


# ----------------
# ASIO SDK Check (only for Ninja build path)
# ----------------
$asioDir = "$PSScriptRoot/../external/portaudio/build"
$asioZip = "$asioDir/asiosdk.zip"
$asioExtractDir = "$asioDir/asiosdk"

if (-not (Test-Path $asioExtractDir)) {
    Write-Host "ASIO SDK not found. Downloading and extracting..."

    if (-not (Test-Path $asioDir)) {
        New-Item -ItemType Directory -Path $asioDir -Force | Out-Null
    }

    $asioUrl = "https://www.steinberg.net/asiosdk" # NOTE: This may require user to agree to license; auto download may fail.

    try {
        Invoke-WebRequest -Uri $asioUrl -OutFile $asioZip -UseBasicParsing
        Expand-Archive -Path $asioZip -DestinationPath $asioExtractDir -Force
        Remove-Item $asioZip
        Write-Host "ASIO SDK extracted to: $asioExtractDir"
    }
    catch {
        Write-Warning "ASIO SDK could not be downloaded automatically. Please download it manually from:"
        Write-Warning "https://www.steinberg.net/en/company/developers.html"
        Write-Warning "Then extract the ZIP to: $asioDir"
    }
}
else {
    Write-Host "ASIO SDK already present. Skipping download."
}


# ----------------
# Final Check
# ----------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -or -not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Error "Failed to install MinGW, CMake, Ninja or ASIO."
    exit 1
}

Write-Host "All required tools are ready."
