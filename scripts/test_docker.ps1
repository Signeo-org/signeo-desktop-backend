<#
.SYNOPSIS
    Runs unit tests, integration tests, and benchmarks inside a Docker container.
    Generates and optionally serves code coverage reports.

.DESCRIPTION
    This script builds a special "testing" Docker image (based on the main Dockerfile)
    with coverage flags enabled. It then runs the tests and benchmarks, extracts
    the coverage report to the host, and optionally serves the report via HTTP.

.PARAMETER Cuda
    If set, uses the NVIDIA CUDA base image and enables CUDA in the build.
    Requires NVIDIA Container Toolkit to be installed and working.

.PARAMETER Serve
    If set, starts a local Python HTTP server to view the coverage report after generation.

.PARAMETER Port
    Port number for the HTTP server (default: 8000).

.PARAMETER Clean
    If set, removes the previous coverage directory before starting.

.EXAMPLE
    .\scripts\docker_test.ps1
    Runs standard tests on CPU.

.EXAMPLE
    .\scripts\docker_test.ps1 -Cuda
    Runs tests with CUDA support.

.EXAMPLE
    .\scripts\docker_test.ps1 -Serve
    Runs tests and opens the coverage report in the browser.
#>

[CmdletBinding()]
param (
    [switch]$Cuda,
    [switch]$Serve,
    [string]$Name = "realtime-subtitler-test",
    [int]$Port = 8080,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# Determine Repo Root (Since this script is in scripts/)
$RepoRoot = Resolve-Path "$PSScriptRoot/.."
Set-Location $RepoRoot

# Configuration
$ContainerName = $Name
$CoverageDir = "coverage"

# Default Base Image (CPU)
$BaseImage = "ubuntu:24.04"
$GgmlCuda = "OFF"
# Docker run options (array to avoid empty string arguments)
$DockerRunOptions = @()

# Force Legacy Builder for better stability/logging
$env:DOCKER_BUILDKIT = 0

# CUDA Configuration
if ($Cuda) {
    Write-Host "[INFO] Enabling CUDA support..." -ForegroundColor Cyan
    # Use NVIDIA CUDA base image (devel version for nvcc)
    $BaseImage = "nvidia/cuda:12.4.1-devel-ubuntu22.04" 
    $GgmlCuda = "ON"
    $DockerRunOptions += "--gpus", "all"
}

# Cleanup
if ($Clean) {
    if (Test-Path $CoverageDir) {
        Write-Host "[INFO] Cleaning up coverage directory..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $CoverageDir
    }
}

# 1. Build Docker Image (Test Mode)
Write-Host "`n[STEP 1] Building Docker Image ($ContainerName)..." -ForegroundColor Green
Write-Host "         Base Image: $BaseImage"
Write-Host "         CUDA:       $GgmlCuda"

# Pass build args to Docker
# We target 'builder' stage because it contains GCC, coverage tools, and build artifacts (.gcno)
# required for coverage reporting. The runtime stage is too slim.
docker build -t $ContainerName . `
    --target builder `
    --build-arg BASE_IMAGE=$BaseImage `
    --build-arg BUILD_TYPE=Debug `
    --build-arg ENABLE_COVERAGE=ON `
    --build-arg GGML_CUDA=$GgmlCuda

if ($LASTEXITCODE -ne 0) {
    Write-Error "Docker build failed."
}

# 2. Run Tests in Container
Write-Host "`n[STEP 2] Running Tests & Benchmarks..." -ForegroundColor Green

# We run a composite command inside the container:
# 1. Run Unit Tests -> generates .gcda files in build/
# 2. Run Integration Tests -> generates .gcda files in build/
# 3. Run Benchmarks -> just for perf metrics
# 4. Run lcov/genhtml -> generates HTML report
# 5. Tar the report to stdout so we can pipe it to the host (avoiding volume permission issues)

# Note: Binaries are in ./build/
$TestCommand = "
    echo '--- Running Unit Tests ---';
    ./build/unit_tests --gtest_output=xml:unit_tests_report.xml > unit_tests_output.txt 2>&1
    if [ $? -ne 0 ]; then cat unit_tests_output.txt; exit 1; fi
    cat unit_tests_output.txt
    
    echo '--- Running Integration Tests ---';
    ./build/integration_tests --gtest_output=xml:integration_tests_report.xml > integration_tests_output.txt 2>&1
    if [ $? -ne 0 ]; then cat integration_tests_output.txt; exit 1; fi
    cat integration_tests_output.txt
    
    echo '--- Running Benchmarks ---';
    ./build/benchmarks > benchmark_results.txt
    cat benchmark_results.txt || exit 1;
    
    echo '--- Generating Coverage Report ---';
    mkdir -p coverage_out;
    # --rc geninfo_unexecuted_blocks=1 suppresses warnings about unexecuted blocks in headers
    lcov --capture --directory . --output-file coverage.info --no-external --ignore-errors mismatch,empty,negative,gcov --rc geninfo_unexecuted_blocks=1;
    # Filter out system headers and third-party deps
    lcov --remove coverage.info '/usr/*' '*/_deps/*' '*/tests/*' '*/build/*' --output-file coverage_filtered.info --ignore-errors unused,negative;
    genhtml coverage_filtered.info --output-directory coverage_out --ignore-errors source;
    
    echo '--- Packaging Report ---';
    # Archive coverage HTML
    tar -czf coverage.tar.gz -C coverage_out .
    # Archive test results
    tar -czf test_results.tar.gz unit_tests_report.xml integration_tests_report.xml benchmark_results.txt benchmark_results.xml
" -replace "`r", ""

# We pipe the output of the tar command to a file on the host
# Note: Since we are mixing stdout (logs) and the tar stream, we need to be careful.
# A cleaner way is to use a volume or 'docker cp'. Let's use 'docker cp' methodology
# by running the container, generating the files, and then copying them out before it dies.
# But since we use --rm, it dies immediately.
# Let's run without --rm, execute, copy, then remove.

$ContainerId = "$ContainerName-runner"

# Check if container exists and remove it (failed run cleanup)
if (docker ps -a --format '{{.Names}}' | Select-String -Pattern "^$ContainerId$") {
    Write-Host "[INFO] Removing existing container $ContainerId..."
    docker rm -f $ContainerId | Out-Null
}

Write-Host "Starting container $ContainerId..."
# We use 'sh -c' to run multiple commands.
docker run --name $ContainerId $DockerRunOptions $ContainerName sh -c "$TestCommand"

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Tests failed inside the container." -ForegroundColor Red
    # Don't remove container yet so user can debug
    Write-Host "Container $ContainerId kept for inspection."
    exit 1
}

# 3. Extract Coverage Report & Test Results
Write-Host "`n[STEP 3] Extracting Artifacts..." -ForegroundColor Green

if (Test-Path $CoverageDir) {
    Remove-Item -Recurse -Force $CoverageDir
}
New-Item -ItemType Directory -Path $CoverageDir | Out-Null

$ResultsDir = "test_results"
if (Test-Path $ResultsDir) {
    Remove-Item -Recurse -Force $ResultsDir
}
New-Item -ItemType Directory -Path $ResultsDir | Out-Null

# Copy the artifacts
docker cp "${ContainerId}:/app/coverage_out/." "$CoverageDir"
docker cp "${ContainerId}:/app/unit_tests_report.xml" "$ResultsDir/"
docker cp "${ContainerId}:/app/integration_tests_report.xml" "$ResultsDir/"
docker cp "${ContainerId}:/app/benchmark_results.txt" "$ResultsDir/"
docker cp "${ContainerId}:/app/benchmark_results.xml" "$ResultsDir/"
docker cp "${ContainerId}:/app/unit_tests_output.txt" "$ResultsDir/"
docker cp "${ContainerId}:/app/integration_tests_output.txt" "$ResultsDir/"

# Cleanup Container
docker rm $ContainerId | Out-Null

Write-Host "[SUCCESS] Coverage report extracted to '$CoverageDir/'" -ForegroundColor Green
Write-Host "[SUCCESS] Test results extracted to '$ResultsDir/'" -ForegroundColor Green

# 4. Generate Unified Dashboard
Write-Host "`n[STEP 4] Generating Unified Dashboard..." -ForegroundColor Green
try {
    if (Test-Path "scripts/generate_dashboard.py") {
        # Note: We assume python is available on host as checked in Serve step, but check here too
        if (Get-Command "python" -ErrorAction SilentlyContinue) {
            python scripts/generate_dashboard.py
        } elseif (Get-Command "python3" -ErrorAction SilentlyContinue) {
            python3 scripts/generate_dashboard.py
        } else {
            Write-Warning "Python not found. Skipping dashboard generation."
        }
    } else {
        Write-Warning "Dashboard script not found."
    }
} catch {
    Write-Warning "Failed to generate dashboard: $_"
}

# 5. Serve Report (Optional)
if ($Serve) {
    Write-Host "`n[STEP 5] Serving Report on http://localhost:$Port" -ForegroundColor Cyan
    Write-Host "Press Ctrl+C to stop the server."
    
    # We serve the parent folder (root of repo? No, too risky). 
    # We want to serve 'test_results' as root, but 'coverage' is outside.
    # To make links work, we should copy coverage INSIDE test_results temporarily or use symlinks.
    # Easiest: Copy coveragedir into resultsdir/coverage
    
    $ServeDir = $ResultsDir
    $InnerCoverageDir = "$ResultsDir/coverage"
    if (Test-Path $InnerCoverageDir) { Remove-Item -Recurse -Force $InnerCoverageDir }
    Copy-Item -Recurse $CoverageDir $InnerCoverageDir

    try {
        if (Get-Command "python" -ErrorAction SilentlyContinue) {
            python -m http.server $Port --directory $ServeDir
        } elseif (Get-Command "python3" -ErrorAction SilentlyContinue) {
            python3 -m http.server $Port --directory $ServeDir
        } else {
            Write-Warning "Python not found. Cannot serve report. Please open '$ServeDir/index.html' manually."
        }
    } catch {
        Write-Warning "Failed to start server on port $Port. It might be in use or blocked."
        Write-Warning "Try running with a different port using: -Port <number>"
        Write-Warning "Detailed Error: $_"
    }
}
