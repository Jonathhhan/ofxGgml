param(
    [string]$Project = "ofxGgmlGuiExample\\ofxGgmlGuiExample.vcxproj",
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$BuildPath = "",
    [string[]]$Files = @(),
    [string]$Checks = "",
    [switch]$Fix,
    [switch]$UseMsBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ClangTidyPath {
    $candidates = @(
        "clang-tidy.exe",
        "C:\\Program Files\\LLVM\\bin\\clang-tidy.exe",
        "C:\\Program Files (x86)\\LLVM\\bin\\clang-tidy.exe"
    )

    foreach ($candidate in $candidates) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Get-CompileCommandsPath {
    param([string]$ExplicitBuildPath)

    $candidates = @()
    if ($ExplicitBuildPath) {
        $candidates += (Join-Path $ExplicitBuildPath "compile_commands.json")
        if ((Split-Path $ExplicitBuildPath -Leaf) -ieq "compile_commands.json") {
            $candidates += $ExplicitBuildPath
        }
    }

    $candidates += @(
        "compile_commands.json",
        (Join-Path "build" "compile_commands.json"),
        (Join-Path "ofxGgmlGuiExample" "compile_commands.json"),
        (Join-Path "ofxGgmlGuiExample" "build\\compile_commands.json")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Get-TargetFiles {
    param([string[]]$RequestedFiles)

    if ($RequestedFiles -and $RequestedFiles.Count -gt 0) {
        return $RequestedFiles
    }

    $srcFiles = Get-ChildItem -Path "src", "ofxGgmlGuiExample\\src" -Recurse -Include *.cpp,*.cxx,*.cc,*.c -File |
        ForEach-Object { $_.FullName }
    return $srcFiles
}

$clangTidy = Resolve-ClangTidyPath
if (-not $clangTidy) {
    throw "clang-tidy was not found. Install LLVM clang-tools or add clang-tidy.exe to PATH."
}

$compileCommands = Get-CompileCommandsPath -ExplicitBuildPath $BuildPath
$targetFiles = Get-TargetFiles -RequestedFiles $Files

if ($compileCommands) {
    $compileDbDir = Split-Path $compileCommands -Parent
    $arguments = @()
    if ($Checks) {
        $arguments += "-checks=$Checks"
    }
    if ($Fix.IsPresent) {
        $arguments += "-fix"
    }
    $arguments += "-p"
    $arguments += $compileDbDir
    $arguments += $targetFiles

    Write-Host "Running clang-tidy with compile database: $compileCommands"
    & $clangTidy @arguments
    exit $LASTEXITCODE
}

if (-not $UseMsBuild.IsPresent) {
    throw "No compile_commands.json found. Re-run with -UseMsBuild for Visual Studio integration or provide -BuildPath."
}

$msbuild = "C:\\Program Files\\Microsoft Visual Studio\\18\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
    $msbuildCommand = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($msbuildCommand) {
        $msbuild = $msbuildCommand.Source
    } else {
        throw "MSBuild.exe was not found."
    }
}

$projectPath = (Resolve-Path $Project).Path
$propertyParts = @(
    "Configuration=$Configuration",
    "Platform=$Platform",
    "EnableClangTidyCodeAnalysis=true"
)
if ($Checks) {
    $propertyParts += "ClangTidyChecks=$Checks"
}
if ($Fix.IsPresent) {
    $propertyParts += "ClangTidyFix=true"
}

Write-Host "Running Visual Studio Clang-Tidy integration for $projectPath"
& $msbuild $projectPath "/t:ClCompile" "/p:$($propertyParts -join ';')" /m
exit $LASTEXITCODE
