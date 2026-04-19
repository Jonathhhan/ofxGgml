param(
    [string]$Branch = "main",
    [string]$RepositoryUrl = "https://github.com/ServeurpersoCom/acestep.cpp.git",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$InstallDir = "",
    [int]$Jobs = 0,
    [switch]$Refetch,
    [switch]$Clean,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message"
}

function Get-CommandPathOrNull {
    param([string]$Name)
    try {
        return (Get-Command $Name -ErrorAction Stop).Source
    } catch {
        return $null
    }
}

function Invoke-LoggedCommand {
    param(
        [string]$Executable,
        [string[]]$Arguments
    )

    if ($DryRun) {
        Write-Host "$Executable $($Arguments -join ' ')"
        return
    }

    & $Executable @Arguments
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = (Resolve-Path (Join-Path $scriptRoot '..')).Path

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $addonRoot 'libs\acestep\source'
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $addonRoot 'libs\acestep\build'
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $addonRoot 'libs\acestep\bin'
}
if ($Jobs -le 0) {
    $Jobs = [Math]::Max(1, [Environment]::ProcessorCount)
}

$git = Get-CommandPathOrNull 'git.exe'
$cmake = Get-CommandPathOrNull 'cmake.exe'
if (-not $git) {
    throw "git.exe was not found in PATH."
}
if (-not $cmake) {
    throw "cmake.exe was not found in PATH."
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Step "Removing previous AceStep build directory"
    if (-not $DryRun) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }
}

if ($Refetch -and (Test-Path -LiteralPath $SourceDir)) {
    Write-Step "Removing existing AceStep source checkout"
    if (-not $DryRun) {
        Remove-Item -LiteralPath $SourceDir -Recurse -Force
    }
}

if (-not (Test-Path -LiteralPath $SourceDir)) {
    Write-Step "Cloning AceStep source"
    Invoke-LoggedCommand $git @('clone', '--depth', '1', '--branch', $Branch, $RepositoryUrl, $SourceDir)
} else {
    Write-Step "Updating existing AceStep source checkout"
    Invoke-LoggedCommand $git @('-C', $SourceDir, 'fetch', 'origin', $Branch, '--depth', '1')
    Invoke-LoggedCommand $git @('-C', $SourceDir, 'checkout', $Branch)
    Invoke-LoggedCommand $git @('-C', $SourceDir, 'pull', '--ff-only', 'origin', $Branch)
}

$cmakeListsPath = Join-Path $SourceDir 'CMakeLists.txt'
if (-not (Test-Path -LiteralPath $cmakeListsPath)) {
    throw "The AceStep checkout does not contain CMakeLists.txt at $cmakeListsPath"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

Write-Step "Configuring AceStep"
Invoke-LoggedCommand $cmake @(
    '-S', $SourceDir,
    '-B', $BuildDir
)

Write-Step "Building AceStep"
Invoke-LoggedCommand $cmake @(
    '--build', $BuildDir,
    '--config', 'Release',
    '--parallel', $Jobs
)

Write-Step "Installing AceStep runtime artifacts into $InstallDir"
if (-not $DryRun) {
    $artifactPatterns = @('*.exe', '*.dll')
    $copied = 0
    foreach ($pattern in $artifactPatterns) {
        Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter $pattern |
            Where-Object {
                $_.FullName -match '\\(Release|bin)\\' -or $_.DirectoryName -eq $BuildDir
            } |
            ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Force
                $copied++
            }
    }

    if ($copied -eq 0) {
        Write-Warning "No AceStep runtime artifacts were found under $BuildDir. The project may build different target names on this platform."
    }
}

Write-Host ""
Write-Host "AceStep install helper complete."
Write-Host "  source:  $SourceDir"
Write-Host "  build:   $BuildDir"
Write-Host "  install: $InstallDir"
Write-Host "  server url default in ofxGgml: http://127.0.0.1:8085"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Start the AceStep server from the installed binaries or the build tree."
Write-Host "  2. In the GUI example, open Vision -> AceStep Music Backend."
Write-Host "  3. Click 'Check AceStep server' and confirm the configured URL is reachable."
