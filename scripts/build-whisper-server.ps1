param(
    [string]$Branch = "master",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$InstallDir = "",
    [int]$Jobs = 0,
    [switch]$Cuda,
    [switch]$CpuOnly,
    [switch]$Refetch,
    [switch]$Clean,
    [switch]$KeepArtifacts,
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

function Resolve-VisualStudioAsmCompiler {
    $preferredRoots = @(
        'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\MSVC',
        'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC',
        'C:\Program Files\Microsoft Visual Studio\17\Professional\VC\Tools\MSVC',
        'C:\Program Files\Microsoft Visual Studio\17\Community\VC\Tools\MSVC'
    )

    foreach ($root in $preferredRoots) {
        if (-not (Test-Path -LiteralPath $root)) {
            continue
        }
        $versions = Get-ChildItem -LiteralPath $root -Directory | Sort-Object Name -Descending
        foreach ($version in $versions) {
            $candidate = Join-Path $version.FullName 'bin\Hostx64\x64\ml64.exe'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    return $null
}

function Test-CudaAvailable {
    if ($CpuOnly) {
        return $false
    }
    if ($Cuda) {
        return $true
    }
    if ($env:CUDA_PATH -and (Test-Path -LiteralPath $env:CUDA_PATH)) {
        return $true
    }
    if (Get-CommandPathOrNull 'nvcc.exe') {
        return $true
    }
    if (Get-CommandPathOrNull 'nvidia-smi.exe') {
        return $true
    }
    return $false
}

function Get-DefaultRuntimeRoot {
    param([string]$AddonRoot)

    $localAppData = [Environment]::GetFolderPath('LocalApplicationData')
    if (-not [string]::IsNullOrWhiteSpace($localAppData)) {
        return (Join-Path $localAppData 'ofxGgml\whisper-runtime')
    }
    return (Join-Path $AddonRoot '.runtime\whisper-runtime')
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = (Resolve-Path (Join-Path $scriptRoot '..')).Path
$defaultRuntimeRoot = Get-DefaultRuntimeRoot -AddonRoot $addonRoot

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $defaultRuntimeRoot 'source'
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $defaultRuntimeRoot 'build'
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $addonRoot 'libs\whisper\bin'
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

$useCuda = Test-CudaAvailable
$asmCompiler = $null
if ($useCuda) {
    $asmCompiler = Resolve-VisualStudioAsmCompiler
    if (-not $asmCompiler) {
        throw "CUDA build requested or detected, but ml64.exe was not found. Open a Visual Studio developer shell or install MSVC build tools."
    }
}

Write-Step "Preparing whisper.cpp source"
if ($Clean) {
    if (Test-Path -LiteralPath $BuildDir) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }
}

if ($Refetch -and (Test-Path -LiteralPath $SourceDir)) {
    Remove-Item -LiteralPath $SourceDir -Recurse -Force
}

if (-not (Test-Path -LiteralPath $SourceDir)) {
    $cloneArgs = @('clone', '--depth', '1', '--branch', $Branch, 'https://github.com/ggml-org/whisper.cpp.git', $SourceDir)
    Write-Step "Cloning whisper.cpp ($Branch)"
    if ($DryRun) {
        Write-Host "$git $($cloneArgs -join ' ')"
    } else {
        & $git @cloneArgs
    }
} else {
    Write-Step "Using existing whisper.cpp source at $SourceDir"
    $fetchArgs = @('-C', $SourceDir, 'fetch', 'origin', $Branch, '--depth', '1')
    $checkoutArgs = @('-C', $SourceDir, 'checkout', $Branch)
    $pullArgs = @('-C', $SourceDir, 'pull', '--ff-only', 'origin', $Branch)
    if ($DryRun) {
        Write-Host "$git $($fetchArgs -join ' ')"
        Write-Host "$git $($checkoutArgs -join ' ')"
        Write-Host "$git $($pullArgs -join ' ')"
    } else {
        & $git @fetchArgs
        & $git @checkoutArgs
        & $git @pullArgs
    }
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$configureArgs = @(
    '-S', $SourceDir,
    '-B', $BuildDir
)
if ($useCuda) {
    $configureArgs += @('-DGGML_CUDA=ON', "-DCMAKE_ASM_COMPILER=$asmCompiler")
} else {
    $configureArgs += '-DGGML_CUDA=OFF'
}

Write-Step ("Configuring whisper.cpp for " + ($(if ($useCuda) { 'CUDA' } else { 'CPU-only' })) + " speech runtime build")
if ($DryRun) {
    Write-Host "$cmake $($configureArgs -join ' ')"
} else {
    & $cmake @configureArgs
}

$buildTargets = @('whisper-cli', 'whisper-server')
foreach ($target in $buildTargets) {
    $buildArgs = @(
        '--build', $BuildDir,
        '--config', 'Release',
        '--target', $target,
        '--parallel', $Jobs
    )

    Write-Step "Building $target"
    if ($DryRun) {
        Write-Host "$cmake $($buildArgs -join ' ')"
    } else {
        & $cmake @buildArgs
    }
}

$releaseBinDir = Join-Path $BuildDir 'bin\Release'
$cliExe = Join-Path $releaseBinDir 'whisper-cli.exe'
$serverExe = Join-Path $releaseBinDir 'whisper-server.exe'
if (-not $DryRun -and -not (Test-Path -LiteralPath $cliExe)) {
    throw "Build finished but whisper-cli.exe was not found at $cliExe"
}
if (-not $DryRun -and -not (Test-Path -LiteralPath $serverExe)) {
    throw "Build finished but whisper-server.exe was not found at $serverExe"
}

Write-Step "Installing whisper.cpp runtime into $InstallDir"
if ($DryRun) {
    Write-Host "Copy $cliExe -> $InstallDir"
    Write-Host "Copy $serverExe -> $InstallDir"
    Write-Host "Copy DLLs from $releaseBinDir -> $InstallDir"
} else {
    Copy-Item -LiteralPath $cliExe -Destination $InstallDir -Force
    Copy-Item -LiteralPath $serverExe -Destination $InstallDir -Force
    Get-ChildItem -LiteralPath $releaseBinDir -Filter '*.dll' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Force
    }

    if (-not $KeepArtifacts) {
        Write-Step "Pruning whisper.cpp source/build artifacts"
        if ((Test-Path -LiteralPath $BuildDir) -and ($BuildDir -ne $InstallDir)) {
            Remove-Item -LiteralPath $BuildDir -Recurse -Force
        }
        if ((Test-Path -LiteralPath $SourceDir) -and ($SourceDir -ne $InstallDir)) {
            Remove-Item -LiteralPath $SourceDir -Recurse -Force
        }
    }
}

Write-Host ""
Write-Host "whisper.cpp speech runtime build complete."
Write-Host "  source:  $SourceDir"
Write-Host "  build:   $BuildDir"
Write-Host "  install: $InstallDir"
Write-Host "  cli:     $(Join-Path $InstallDir 'whisper-cli.exe')"
Write-Host "  server:  $(Join-Path $InstallDir 'whisper-server.exe')"
Write-Host "  cuda:    $useCuda"
if (-not $KeepArtifacts) {
    Write-Host "  cache:   pruned after install"
}
