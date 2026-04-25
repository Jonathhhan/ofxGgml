param(
    [string]$Ref = "v2.13.5",
    [string]$RepositoryUrl = "https://gitlab.gnome.org/GNOME/libxml2.git",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$InstallDir = "",
    [string]$VendorDir = "",
    [string]$Generator = "Visual Studio 18 2026",
    [string]$Platform = "x64",
    [string]$Configuration = "Release",
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

    & $Executable @Arguments | Out-Host
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Command failed with exit code ${exitCode}: $Executable $($Arguments -join ' ')"
    }
}

function Ensure-Directory {
    param([string]$Path)

    if ($DryRun) {
        Write-Host "mkdir $Path"
        return
    }

    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Remove-DirectoryIfPresent {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    if ($DryRun) {
        Write-Host "rmdir /s /q $Path"
        return
    }

    Remove-Item -LiteralPath $Path -Recurse -Force
}

function Copy-DirectoryContents {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Source directory was not found: $Source"
    }

    Ensure-Directory $Destination
    if ($DryRun) {
        Write-Host "copy $Source\* -> $Destination"
        return
    }

    Copy-Item -Path (Join-Path $Source '*') -Destination $Destination -Recurse -Force
}

function Copy-FileIfPresent {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Required file was not found: $Source"
    }

    $parent = Split-Path -Parent $Destination
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        Ensure-Directory $parent
    }

    if ($DryRun) {
        Write-Host "copy $Source -> $Destination"
        return
    }

    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Assert-PathExists {
    param([string]$Path)

    if ($DryRun) {
        return
    }

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Expected path was not found: $Path"
    }
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = (Resolve-Path (Join-Path $scriptRoot '..')).Path

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $addonRoot 'build\vendor\libxml2-src'
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $addonRoot 'build\vendor\libxml2-build'
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $addonRoot 'build\vendor\libxml2-install'
}
if ([string]::IsNullOrWhiteSpace($VendorDir)) {
    $VendorDir = Join-Path $addonRoot 'libs\libxml2'
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

Write-Step "Preparing libxml2 source"
if ($Refetch) {
    Remove-DirectoryIfPresent $SourceDir
}
if ($Clean) {
    Remove-DirectoryIfPresent $BuildDir
    Remove-DirectoryIfPresent $InstallDir
}

if (-not (Test-Path -LiteralPath $SourceDir)) {
    Ensure-Directory (Split-Path -Parent $SourceDir)
    Invoke-LoggedCommand $git @(
        'clone',
        '--depth', '1',
        $RepositoryUrl,
        $SourceDir
    )
}

Invoke-LoggedCommand $git @('-C', $SourceDir, 'fetch', 'origin', '--tags', '--force')
Invoke-LoggedCommand $git @('-C', $SourceDir, 'checkout', '--force', $Ref)

Write-Step "Configuring libxml2"
Ensure-Directory (Split-Path -Parent $BuildDir)
Ensure-Directory (Split-Path -Parent $InstallDir)
Invoke-LoggedCommand $cmake @(
    '-S', $SourceDir,
    '-B', $BuildDir,
    '-G', $Generator,
    '-A', $Platform,
    '-D', 'BUILD_SHARED_LIBS=ON',
    '-D', "CMAKE_INSTALL_PREFIX=$InstallDir",
    '-D', 'LIBXML2_WITH_HTML=ON',
    '-D', 'LIBXML2_WITH_PROGRAMS=ON',
    '-D', 'LIBXML2_WITH_PYTHON=OFF',
    '-D', 'LIBXML2_WITH_TESTS=OFF',
    '-D', 'LIBXML2_WITH_ICONV=OFF',
    '-D', 'LIBXML2_WITH_ZLIB=OFF',
    '-D', 'LIBXML2_WITH_LZMA=OFF',
    '-D', 'LIBXML2_WITH_HTTP=OFF',
    '-D', 'LIBXML2_WITH_FTP=OFF'
)

Write-Step "Building libxml2"
Invoke-LoggedCommand $cmake @(
    '--build', $BuildDir,
    '--config', $Configuration,
    '--parallel', $Jobs
)

Write-Step "Installing libxml2 into staging"
Invoke-LoggedCommand $cmake @(
    '--install', $BuildDir,
    '--config', $Configuration
)

$installedHeaderRoot = Join-Path $InstallDir 'include\libxml2\libxml'
$installedLib = Join-Path $InstallDir 'lib\libxml2.lib'
$installedDll = Join-Path $InstallDir 'bin\libxml2.dll'
$installedXmllint = Join-Path $InstallDir 'bin\xmllint.exe'
$installedXmlcatalog = Join-Path $InstallDir 'bin\xmlcatalog.exe'
$installedCopyright = Join-Path $SourceDir 'Copyright'

Assert-PathExists $installedHeaderRoot
Assert-PathExists $installedLib
Assert-PathExists $installedDll
Assert-PathExists $installedXmllint

$vendorIncludeDir = Join-Path $VendorDir 'include\libxml'
$vendorLibDir = Join-Path $VendorDir 'lib\vs\x64'
$vendorBinDir = Join-Path $VendorDir 'bin'
$vendorLicenseDir = Join-Path $VendorDir 'license'

Write-Step "Updating vendored libxml2 artifacts"
Copy-DirectoryContents $installedHeaderRoot $vendorIncludeDir
Copy-FileIfPresent $installedLib (Join-Path $vendorLibDir 'libxml2.lib')
Copy-FileIfPresent $installedDll (Join-Path $vendorLibDir 'libxml2.dll')
Copy-FileIfPresent $installedDll (Join-Path $vendorBinDir 'libxml2.dll')
Copy-FileIfPresent $installedXmllint (Join-Path $vendorBinDir 'xmllint.exe')
if (Test-Path -LiteralPath $installedXmlcatalog) {
    Copy-FileIfPresent $installedXmlcatalog (Join-Path $vendorBinDir 'xmlcatalog.exe')
}
if (Test-Path -LiteralPath $installedCopyright) {
    Copy-FileIfPresent $installedCopyright (Join-Path $vendorLicenseDir 'Copyright')
}

Write-Step "Done"
Write-Host "Source:  $SourceDir"
Write-Host "Build:   $BuildDir"
Write-Host "Install: $InstallDir"
Write-Host "Vendor:  $VendorDir"
