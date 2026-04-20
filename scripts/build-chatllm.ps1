param(
    [string]$Branch = "master",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$InstallDir = "",
    [int]$Jobs = 0,
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

function Get-DefaultRuntimeRoot {
    param([string]$AddonRoot)

    $localAppData = [Environment]::GetFolderPath('LocalApplicationData')
    if (-not [string]::IsNullOrWhiteSpace($localAppData)) {
        return (Join-Path $localAppData 'ofxGgml\chatllm-runtime')
    }
    return (Join-Path $AddonRoot '.runtime\chatllm-runtime')
}

function Resolve-BuiltExecutable {
    param([string]$BuildDir)

    $preferredNames = @('chatllm.exe', 'main.exe')
    $searchRoots = @(
        (Join-Path $BuildDir 'bin\Release'),
        (Join-Path $BuildDir 'build\bin\Release'),
        (Join-Path $BuildDir 'Release'),
        $BuildDir
    )

    foreach ($root in $searchRoots) {
        if (-not (Test-Path -LiteralPath $root)) {
            continue
        }
        foreach ($name in $preferredNames) {
            $candidate = Get-ChildItem -LiteralPath $root -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($candidate) {
                return $candidate.FullName
            }
        }
    }

    foreach ($name in $preferredNames) {
        $candidate = Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    return $null
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
    $InstallDir = Join-Path $addonRoot 'libs\chatllm\bin'
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

Write-Step "Preparing chatllm.cpp source"
if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

if ($Refetch -and (Test-Path -LiteralPath $SourceDir)) {
    Remove-Item -LiteralPath $SourceDir -Recurse -Force
}

if (-not (Test-Path -LiteralPath $SourceDir)) {
    $cloneArgs = @(
        'clone',
        '--depth', '1',
        '--branch', $Branch,
        '--recursive',
        'https://github.com/foldl/chatllm.cpp.git',
        $SourceDir
    )
    Write-Step "Cloning chatllm.cpp ($Branch)"
    if ($DryRun) {
        Write-Host "$git $($cloneArgs -join ' ')"
    } else {
        & $git @cloneArgs
    }
} else {
    Write-Step "Using existing chatllm.cpp source at $SourceDir"
    $fetchArgs = @('-C', $SourceDir, 'fetch', 'origin', $Branch, '--depth', '1')
    $checkoutArgs = @('-C', $SourceDir, 'checkout', $Branch)
    $pullArgs = @('-C', $SourceDir, 'pull', '--ff-only', 'origin', $Branch)
    $submoduleArgs = @('-C', $SourceDir, 'submodule', 'update', '--init', '--recursive')
    if ($DryRun) {
        Write-Host "$git $($fetchArgs -join ' ')"
        Write-Host "$git $($checkoutArgs -join ' ')"
        Write-Host "$git $($pullArgs -join ' ')"
        Write-Host "$git $($submoduleArgs -join ' ')"
    } else {
        & $git @fetchArgs
        & $git @checkoutArgs
        & $git @pullArgs
        & $git @submoduleArgs
    }
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$configureArgs = @(
    '-S', $SourceDir,
    '-B', $BuildDir
)

Write-Step "Configuring chatllm.cpp"
if ($DryRun) {
    Write-Host "$cmake $($configureArgs -join ' ')"
} else {
    & $cmake @configureArgs
}

$buildArgs = @(
    '--build', $BuildDir,
    '--config', 'Release',
    '--parallel', $Jobs
)

Write-Step "Building chatllm.cpp"
if ($DryRun) {
    Write-Host "$cmake $($buildArgs -join ' ')"
} else {
    & $cmake @buildArgs
}

$builtExe = Resolve-BuiltExecutable -BuildDir $BuildDir
if (-not $DryRun -and [string]::IsNullOrWhiteSpace($builtExe)) {
    throw "Build finished but neither chatllm.exe nor main.exe was found under $BuildDir"
}

$builtExeDir = if ($DryRun -or [string]::IsNullOrWhiteSpace($builtExe)) {
    Join-Path $BuildDir 'bin\Release'
} else {
    Split-Path -Parent $builtExe
}

Write-Step "Installing chatllm.cpp runtime into $InstallDir"
if ($DryRun) {
    Write-Host "Copy $builtExe -> $InstallDir"
    Write-Host "Copy DLLs from $builtExeDir -> $InstallDir"
} else {
    Copy-Item -LiteralPath $builtExe -Destination $InstallDir -Force
    $installedExe = Join-Path $InstallDir (Split-Path -Leaf $builtExe)
    if ((Split-Path -Leaf $builtExe).ToLowerInvariant() -ne 'chatllm.exe') {
        Copy-Item -LiteralPath $builtExe -Destination (Join-Path $InstallDir 'chatllm.exe') -Force
    }
    Get-ChildItem -LiteralPath $builtExeDir -Filter '*.dll' -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Force
    }

    if (-not $KeepArtifacts) {
        Write-Step "Pruning chatllm.cpp source/build artifacts"
        if ((Test-Path -LiteralPath $BuildDir) -and ($BuildDir -ne $InstallDir)) {
            Remove-Item -LiteralPath $BuildDir -Recurse -Force
        }
        if ((Test-Path -LiteralPath $SourceDir) -and ($SourceDir -ne $InstallDir)) {
            Remove-Item -LiteralPath $SourceDir -Recurse -Force
        }
    }
}

Write-Host ""
Write-Host "chatllm.cpp TTS runtime build complete."
Write-Host "  source:  $SourceDir"
Write-Host "  build:   $BuildDir"
Write-Host "  install: $InstallDir"
Write-Host "  exe:     $(Join-Path $InstallDir 'chatllm.exe')"
if (-not $KeepArtifacts) {
    Write-Host "  cache:   pruned after install"
}
