param(
    [string]$PythonExecutable = "",
    [string]$RuntimeRoot = "",
    [string]$InstallDir = "",
    [string]$VoiceName = "en_US-lessac-medium",
    [string]$VoicesDir = "",
    [switch]$SkipVoiceDownload,
    [switch]$ForceReinstall,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message"
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

function Resolve-PythonExecutable {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return $RequestedPath
    }

    foreach ($candidate in @('py.exe', 'python.exe', 'python')) {
        try {
            $command = Get-Command $candidate -ErrorAction Stop
            if ($command -and -not [string]::IsNullOrWhiteSpace($command.Source)) {
                return $command.Source
            }
        } catch {
        }
    }

    throw "Python was not found in PATH. Install Python 3 first or pass -PythonExecutable."
}

function Get-DefaultRuntimeRoot {
    param([string]$AddonRoot)

    $localAppData = [Environment]::GetFolderPath('LocalApplicationData')
    if (-not [string]::IsNullOrWhiteSpace($localAppData)) {
        return (Join-Path $localAppData 'ofxGgml\piper-runtime')
    }
    return (Join-Path $AddonRoot '.runtime\piper-runtime')
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = (Resolve-Path (Join-Path $scriptRoot '..')).Path
$defaultRuntimeRoot = Get-DefaultRuntimeRoot -AddonRoot $addonRoot

if ([string]::IsNullOrWhiteSpace($RuntimeRoot)) {
    $RuntimeRoot = $defaultRuntimeRoot
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $addonRoot 'libs\piper\bin'
}
if ([string]::IsNullOrWhiteSpace($VoicesDir)) {
    $VoicesDir = Join-Path $addonRoot 'models\piper'
}

$python = Resolve-PythonExecutable -RequestedPath $PythonExecutable
$venvDir = Join-Path $RuntimeRoot 'venv'
$venvPython = Join-Path $venvDir 'Scripts\python.exe'
$voiceOnnxPath = Join-Path $VoicesDir ($VoiceName + '.onnx')
$voiceJsonPath = Join-Path $VoicesDir ($VoiceName + '.onnx.json')

if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $RuntimeRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    New-Item -ItemType Directory -Force -Path $VoicesDir | Out-Null
}

if ($ForceReinstall -and -not $DryRun -and (Test-Path -LiteralPath $venvDir)) {
    Write-Step "Removing existing Piper virtual environment"
    Remove-Item -LiteralPath $venvDir -Recurse -Force
}

if (-not (Test-Path -LiteralPath $venvPython)) {
    Write-Step "Creating Piper virtual environment"
    if ((Split-Path -Leaf $python).ToLowerInvariant() -eq 'py.exe') {
        Invoke-LoggedCommand $python @('-3', '-m', 'venv', $venvDir)
    } else {
        Invoke-LoggedCommand $python @('-m', 'venv', $venvDir)
    }
} else {
    Write-Step "Using existing Piper virtual environment at $venvDir"
}

Write-Step "Installing/updating piper-tts"
Invoke-LoggedCommand $venvPython @('-m', 'pip', 'install', '--upgrade', 'pip')
Invoke-LoggedCommand $venvPython @('-m', 'pip', 'install', '--upgrade', 'piper-tts')

$launcherPath = Join-Path $InstallDir 'piper.bat'
$downloadLauncherPath = Join-Path $InstallDir 'piper-download-voices.bat'
if (-not $DryRun) {
    $launcherLines = @(
        '@echo off',
        'setlocal',
        "set ""PIPER_PYTHON=$venvPython""",
        'if not exist "%PIPER_PYTHON%" (',
        '  echo Piper runtime is missing. Run scripts\install-piper.ps1 first.',
        '  exit /b 1',
        ')',
        '"%PIPER_PYTHON%" -m piper %*'
    )
    Set-Content -LiteralPath $launcherPath -Value $launcherLines -Encoding ASCII

    $downloadLauncherLines = @(
        '@echo off',
        'setlocal',
        "set ""PIPER_PYTHON=$venvPython""",
        'if not exist "%PIPER_PYTHON%" (',
        '  echo Piper runtime is missing. Run scripts\install-piper.ps1 first.',
        '  exit /b 1',
        ')',
        '"%PIPER_PYTHON%" -m piper.download_voices %*'
    )
    Set-Content -LiteralPath $downloadLauncherPath -Value $downloadLauncherLines -Encoding ASCII
}

if (-not $SkipVoiceDownload) {
    Write-Step "Downloading Piper voice '$VoiceName'"
    Invoke-LoggedCommand $venvPython @(
        '-m',
        'piper.download_voices',
        $VoiceName,
        '--data-dir',
        $VoicesDir
    )

    if (-not $DryRun) {
        if (-not (Test-Path -LiteralPath $voiceOnnxPath)) {
            throw "Expected Piper voice model was not downloaded: $voiceOnnxPath"
        }
        if (-not (Test-Path -LiteralPath $voiceJsonPath)) {
            throw "Expected Piper voice config was not downloaded: $voiceJsonPath"
        }
    }
}

Write-Host ""
Write-Host "Piper install helper complete."
Write-Host "  upstream: https://github.com/OHF-Voice/piper1-gpl"
Write-Host "  python:  $python"
Write-Host "  runtime: $RuntimeRoot"
Write-Host "  venv:    $venvPython"
Write-Host "  install: $InstallDir"
Write-Host "  launch:  $launcherPath"
if (-not $SkipVoiceDownload) {
    Write-Host "  voice:   $VoiceName"
    Write-Host "  model:   $voiceOnnxPath"
    Write-Host "  config:  $voiceJsonPath"
}
Write-Host ""
Write-Host "Recommended TTS profile:"
Write-Host "  - Piper Voice (.onnx)"
Write-Host "  - Executable: leave blank to auto-discover libs/piper/bin/piper.bat"
Write-Host "  - Model path: $voiceOnnxPath"
