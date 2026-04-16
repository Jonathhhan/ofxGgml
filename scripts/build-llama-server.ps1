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

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = (Resolve-Path (Join-Path $scriptRoot '..')).Path

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $addonRoot 'build\llama.cpp-src'
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $addonRoot 'build\llama.cpp-build'
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $addonRoot 'libs\llama\bin'
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

Write-Step "Preparing llama.cpp source"
if ($Clean) {
    if (Test-Path -LiteralPath $BuildDir) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }
}

if ($Refetch -and (Test-Path -LiteralPath $SourceDir)) {
    Remove-Item -LiteralPath $SourceDir -Recurse -Force
}

if (-not (Test-Path -LiteralPath $SourceDir)) {
    $cloneArgs = @('clone', '--depth', '1', '--branch', $Branch, 'https://github.com/ggml-org/llama.cpp.git', $SourceDir)
    Write-Step "Cloning llama.cpp ($Branch)"
    if ($DryRun) {
        Write-Host "$git $($cloneArgs -join ' ')"
    } else {
        & $git @cloneArgs
    }
} else {
    Write-Step "Using existing llama.cpp source at $SourceDir"
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

Write-Step ("Configuring llama.cpp for " + ($(if ($useCuda) { 'CUDA' } else { 'CPU-only' })) + " server build")
if ($DryRun) {
    Write-Host "$cmake $($configureArgs -join ' ')"
} else {
    & $cmake @configureArgs
}

$buildArgs = @(
    '--build', $BuildDir,
    '--config', 'Release',
    '--target', 'llama-server',
    '--parallel', $Jobs
)

Write-Step "Building llama-server"
if ($DryRun) {
    Write-Host "$cmake $($buildArgs -join ' ')"
} else {
    & $cmake @buildArgs
}

$releaseBinDir = Join-Path $BuildDir 'bin\Release'
$serverExe = Join-Path $releaseBinDir 'llama-server.exe'
if (-not $DryRun -and -not (Test-Path -LiteralPath $serverExe)) {
    throw "Build finished but llama-server.exe was not found at $serverExe"
}

Write-Step "Installing llama-server runtime into $InstallDir"
if ($DryRun) {
    Write-Host "Copy $serverExe -> $InstallDir"
    Write-Host "Copy DLLs from $releaseBinDir -> $InstallDir"
} else {
    Copy-Item -LiteralPath $serverExe -Destination $InstallDir -Force
    Get-ChildItem -LiteralPath $releaseBinDir -Filter '*.dll' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Force
    }
}

Write-Host ""
Write-Host "llama-server build complete."
Write-Host "  source:  $SourceDir"
Write-Host "  build:   $BuildDir"
Write-Host "  install: $InstallDir"
Write-Host "  server:  $(Join-Path $InstallDir 'llama-server.exe')"
Write-Host "  cuda:    $useCuda"
