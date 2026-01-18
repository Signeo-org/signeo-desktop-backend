
# Determine Repo Root (Since this script is in scripts/)
$RepoRoot = Resolve-Path "$PSScriptRoot/.."
Set-Location $RepoRoot

Write-Host "Checking for OpenCppCoverage..."
if (Get-Command "OpenCppCoverage" -ErrorAction SilentlyContinue) {
    Write-Host "OpenCppCoverage found. Running coverage analysis..." -ForegroundColor Green
    
    $exePath = "build/Debug/unit_tests.exe"
    if (!(Test-Path $exePath)) {
        $exePath = "build/unit_tests.exe"
    }
    
    if (!(Test-Path $exePath)) {
        Write-Host "Error: Cannot find unit_tests.exe in build/Debug/ or build/" -ForegroundColor Red
        Exit 1
    }

    # Create coverage directory
    New-Item -ItemType Directory -Force -Path "coverage" | Out-Null
    
    # Run coverage
    # --sources specifies which source files to include (filters out system headers)
    # --excluded_sources filters out dependencies
    $cwd = (Get-Location).Path
    OpenCppCoverage --sources "$cwd\src" --excluded_sources "$cwd\build" --excluded_sources "$cwd\_deps" --excluded_sources "googletest" --modules "$cwd" --export_type html:coverage/report -- "$exePath"
    
    Write-Host "Coverage report generated in coverage/report/index.html" -ForegroundColor Cyan
    Start-Process "coverage/report/index.html"
} else {
    Write-Host "OpenCppCoverage not found in PATH." -ForegroundColor Red
    Write-Host "Please install OpenCppCoverage to check code coverage on Windows."
    Write-Host "Download: https://github.com/OpenCppCoverage/OpenCppCoverage/releases"
    Write-Host ""
    Write-Host "Alternatively, use the VSCode extension 'OpenCppCoverage'"
}
