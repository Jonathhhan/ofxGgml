param(
    [string]$ProjectDir = "",
    [string]$BinDir = "",
    [switch]$Stable,
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

function Get-WslPath {
    param([string]$WindowsPath)

    if ($DryRun) {
        return $WindowsPath
    }

    $converted = & wsl.exe wslpath -a $WindowsPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "Failed to convert Windows path to WSL path: $WindowsPath"
    }
    return ($converted | Select-Object -First 1).Trim()
}

function New-Utf8File {
    param(
        [string]$Path,
        [string]$Content
    )

    if ($DryRun) {
        Write-Host "Write file: $Path"
        return
    }

    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Get-DefaultRuntimeRoot {
    param([string]$AddonRoot)

    $localAppData = [Environment]::GetFolderPath('LocalApplicationData')
    if (-not [string]::IsNullOrWhiteSpace($localAppData)) {
        return (Join-Path $localAppData 'ofxGgml\mojo')
    }
    return (Join-Path $AddonRoot '.runtime\mojo')
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = (Resolve-Path (Join-Path $scriptRoot '..')).Path
$defaultRuntimeRoot = Get-DefaultRuntimeRoot -AddonRoot $addonRoot

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
    $ProjectDir = Join-Path $defaultRuntimeRoot 'project'
}
if ([string]::IsNullOrWhiteSpace($BinDir)) {
    $BinDir = Join-Path $addonRoot 'libs\mojo\bin'
}

$wslStatus = & wsl.exe --status 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "WSL is required for Mojo on Windows. Install WSL first, then rerun this script."
}

if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $ProjectDir | Out-Null
    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
}

$projectDirWsl = Get-WslPath $ProjectDir
$binDirWsl = Get-WslPath $BinDir
$mojoIndex = if ($Stable) {
    "https://modular.gateway.scarf.sh/simple/"
} else {
    "https://whl.modular.com/nightly/simple/"
}
$mojoInstallCommand = if ($Stable) {
    'uv pip install --upgrade mojo --extra-index-url https://modular.gateway.scarf.sh/simple/'
} else {
    'uv pip install --upgrade mojo --index https://whl.modular.com/nightly/simple/ --prerelease allow'
}

$wrapperShPath = Join-Path $BinDir 'mojo.sh'
$wrapperBatPath = Join-Path $BinDir 'mojo.bat'
$setupShPath = Join-Path $BinDir 'install-mojo-wsl.sh'
$crawlerScriptPath = Join-Path $BinDir 'mojo_crawl.py'
$crawlerTemplatePath = Join-Path $scriptRoot 'mojo-crawl.py'
$crawlerScriptWsl = Get-WslPath $crawlerScriptPath

# Clean up any potentially corrupted old files
if (-not $DryRun) {
    if (Test-Path $setupShPath) {
        Write-Host "Removing old install-mojo-wsl.sh to ensure clean generation..."
        Remove-Item $setupShPath -Force
    }
}

$wrapperSh = @'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="__PROJECT_DIR__"
MOJO_BIN="$PROJECT_DIR/.venv/bin/mojo"
if [[ ! -x "$MOJO_BIN" ]]; then
  echo "[Error] Mojo is not installed in $PROJECT_DIR. Run scripts/install-mojo.ps1 first." >&2
  exit 1
fi
CRAWLER_SCRIPT="__CRAWLER_SCRIPT__"
case "${1:-}" in
  ""|--help|-h|help|--version|-V|version|run|build|test|repl|format|doc)
    exec "$MOJO_BIN" "$@"
    ;;
esac
if [[ -f "$CRAWLER_SCRIPT" ]]; then
  exec python3 "$CRAWLER_SCRIPT" "$@"
fi
echo "[Error] Mojo crawler script was not found at $CRAWLER_SCRIPT." >&2
exit 1
'@
$wrapperSh = $wrapperSh.Replace('__PROJECT_DIR__', $projectDirWsl)
$wrapperSh = $wrapperSh.Replace('__CRAWLER_SCRIPT__', $crawlerScriptWsl)

$wrapperBat = @'
@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "WRAPPER_SH=%SCRIPT_DIR%mojo.sh"
if not exist "%WRAPPER_SH%" (
  echo [Error] Mojo wrapper was not found at "%WRAPPER_SH%".
  exit /b 1
)
for /f "usebackq delims=" %%I in (`wsl.exe wslpath -a "%WRAPPER_SH%"`) do set "WSL_WRAPPER=%%I"
if not defined WSL_WRAPPER (
  echo [Error] Failed to resolve the WSL path for "%WRAPPER_SH%".
  exit /b 1
)
wsl.exe -e bash "%WSL_WRAPPER%" %*
exit /b %ERRORLEVEL%
'@

Write-Step "Writing Mojo wrapper scripts"
if (-not (Test-Path $crawlerTemplatePath)) {
    throw "Crawler template was not found at $crawlerTemplatePath"
}
$crawlerScript = Get-Content $crawlerTemplatePath -Raw
New-Utf8File -Path $crawlerScriptPath -Content $crawlerScript
New-Utf8File -Path $wrapperShPath -Content $wrapperSh
New-Utf8File -Path $wrapperBatPath -Content $wrapperBat

$setupScript = @'
#!/usr/bin/env bash
set -euo pipefail
if ! command -v curl >/dev/null 2>&1; then
  echo "[Error] curl is required inside WSL to install Mojo dependencies." >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "[Error] python3 is required inside WSL to create the Mojo environment." >&2
  exit 1
fi
if ! command -v uv >/dev/null 2>&1; then
  curl -LsSf https://astral.sh/uv/install.sh | sh
  export PATH="$HOME/.local/bin:$PATH"
fi
mkdir -p "__PROJECT_DIR__"
cd "__PROJECT_DIR__"
if [[ ! -d ".venv" ]]; then
  uv venv
fi
__INSTALL_COMMAND__
".venv/bin/mojo" --version
'@
$setupScript = $setupScript.Replace('__PROJECT_DIR__', $projectDirWsl)
$setupScript = $setupScript.Replace('__INSTALL_COMMAND__', $mojoInstallCommand)

# Validate that the script starts correctly
if (-not $setupScript.StartsWith("#!/usr/bin/env bash`nset -euo pipefail")) {
    throw "Generated install script has invalid header. Please report this bug."
}

New-Utf8File -Path $setupShPath -Content $setupScript
$setupShPathWsl = Get-WslPath $setupShPath

Write-Step "Installing Mojo in the local WSL environment"
try {
    Invoke-LoggedCommand 'wsl.exe' @('-e', 'bash', $setupShPathWsl)
} catch {
    Write-Host ""
    Write-Host "[Error] Failed to install Mojo in WSL. This may be due to:"
    Write-Host "  1. Missing dependencies (curl, python3) in WSL"
    Write-Host "  2. Network connectivity issues"
    Write-Host "  3. WSL configuration problems"
    Write-Host ""
    Write-Host "To diagnose, you can manually run:"
    Write-Host "  wsl.exe -e bash `"$setupShPathWsl`""
    throw
}

Write-Step "Mojo is ready at $wrapperBatPath"
Write-Host "  runtime: $ProjectDir"
