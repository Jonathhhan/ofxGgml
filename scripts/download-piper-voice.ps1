param(
    [Parameter(Mandatory=$true)]
    [string]$VoiceName,
    [string]$RuntimeRoot = "",
    [string]$OutputDir = "",
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

if ([string]::IsNullOrWhiteSpace($RuntimeRoot)) {
    $RuntimeRoot = Get-DefaultRuntimeRoot -AddonRoot $addonRoot
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $addonRoot 'models\piper'
}

$venvPython = Join-Path $RuntimeRoot 'venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython) -and -not $DryRun) {
    throw "Piper runtime was not found at $venvPython. Run scripts\install-piper.ps1 first."
}

if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

Write-Step "Downloading Piper voice '$VoiceName'"
Invoke-LoggedCommand $venvPython @(
    '-m',
    'piper.download_voices',
    $VoiceName,
    '--data-dir',
    $OutputDir
)

$voiceOnnxPath = Join-Path $OutputDir ($VoiceName + '.onnx')
$voiceJsonPath = Join-Path $OutputDir ($VoiceName + '.onnx.json')
if (-not $DryRun) {
    if (-not (Test-Path -LiteralPath $voiceOnnxPath)) {
        throw "Expected Piper voice model was not downloaded: $voiceOnnxPath"
    }
    if (-not (Test-Path -LiteralPath $voiceJsonPath)) {
        throw "Expected Piper voice config was not downloaded: $voiceJsonPath"
    }
}

Write-Host ""
Write-Host "Piper voice download complete."
Write-Host "  voice:  $VoiceName"
Write-Host "  model:  $voiceOnnxPath"
Write-Host "  config: $voiceJsonPath"
