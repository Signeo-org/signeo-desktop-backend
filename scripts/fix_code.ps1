<#
.SYNOPSIS
    Applies automatic code formatting and static analysis fixes.
    
.DESCRIPTION
    1. Runs clang-format -i on all source files to fix style (indentation, braces, etc).
    2. Runs clang-tidy --fix on all source files to fix modern C++ issues.
    
    Requires 'build/compile_commands.json' to exist (run cmake first).
#>

$BuildDir = "build"
$CompileCommands = "$PSScriptRoot/../$BuildDir/compile_commands.json"

if (-not (Test-Path $CompileCommands)) {
    Write-Error "compile_commands.json not found in $BuildDir. Please run: cmake -S . -B $BuildDir"
    exit 1
}

# Try to find clang-tidy in PATH or common locations
$ClangTidy = "clang-tidy"
if (-not (Get-Command $ClangTidy -ErrorAction SilentlyContinue)) {
    # Fallback for VS2022 Community default path
    $Fallback = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
    if (Test-Path $Fallback) { $ClangTidy = $Fallback }
    else {
        Write-Warning "clang-tidy not found in PATH. Please install LLVM or add it to PATH."
        # We continue, hoping clang-format works or user knows what to do
    }
}

Write-Host "Using Clang-Tidy: $ClangTidy" -ForegroundColor Cyan

# 1. Format
Write-Host "`n[1/2] Running clang-format (Style)..." -ForegroundColor Green
Get-ChildItem -Path "src","include" -Recurse -Include *.cpp,*.hpp | ForEach-Object {
    Write-Host "Formatting $($_.Name)" -ForegroundColor Gray
    clang-format -i $_.FullName
}

# 2. Fix Static Analysis Issues
Write-Host "`n[2/2] Running clang-tidy --fix (Logic/Modernize)..." -ForegroundColor Green
$Sources = Get-ChildItem -Path "src" -Recurse -Include *.cpp

foreach ($File in $Sources) {
    Write-Host "Analyzing $($File.Name)..." -ForegroundColor Cyan
    # -p points to the build folder with compile_commands.json
    # -fix applies changes
    # -format-style=file uses our .clang-format to reformat after fixing
    & $ClangTidy -p $BuildDir --fix --format-style=file $File.FullName
}

Write-Host "`nDone! Please review changes before committing." -ForegroundColor Green
