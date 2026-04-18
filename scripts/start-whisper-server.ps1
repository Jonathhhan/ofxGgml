param(
    [string]$ModelPath = "",
    [string]$ServerExe = "",
    [string]$BindHost = "127.0.0.1",
    [int]$Port = 8081,
    [switch]$Detached,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot '..')

function Resolve-FirstExistingPath {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

if ([string]::IsNullOrWhiteSpace($ServerExe)) {
    $ServerExe = Resolve-FirstExistingPath @(
        (Join-Path $addonRoot 'libs\whisper\bin\whisper-server.exe'),
        (Join-Path $addonRoot 'build\whisper.cpp-build\bin\whisper-server.exe'),
        (Join-Path $addonRoot 'build\whisper.cpp-build\bin\Release\whisper-server.exe')
    )
}

if ([string]::IsNullOrWhiteSpace($ServerExe)) {
    if ($DryRun) {
        $ServerExe = Join-Path $addonRoot 'libs\whisper\bin\whisper-server.exe'
    } else {
        throw "Could not find whisper-server.exe. Expected it in libs\whisper\bin or build\whisper.cpp-build\bin\Release."
    }
}

if ([string]::IsNullOrWhiteSpace($ModelPath)) {
    $ModelPath = Resolve-FirstExistingPath @(
        (Join-Path $addonRoot 'models\ggml-large-v3-turbo.bin'),
        (Join-Path $addonRoot 'models\ggml-small.bin'),
        (Join-Path $addonRoot 'models\ggml-base.en.bin'),
        (Join-Path $addonRoot 'models\ggml-base.bin'),
        (Join-Path $addonRoot 'ofxGgmlGuiExample\bin\data\models\ggml-large-v3-turbo.bin'),
        (Join-Path $addonRoot 'ofxGgmlGuiExample\bin\data\models\ggml-small.bin'),
        (Join-Path $addonRoot 'ofxGgmlGuiExample\bin\data\models\ggml-base.en.bin'),
        (Join-Path $addonRoot 'ofxGgmlGuiExample\bin\data\models\ggml-base.bin')
    )
}

if ([string]::IsNullOrWhiteSpace($ModelPath)) {
    if ($DryRun) {
        $ModelPath = Join-Path $addonRoot 'models\ggml-base.en.bin'
    } else {
        throw "Could not find a default Whisper model. Pass -ModelPath explicitly."
    }
}

if (-not $DryRun -and -not (Test-Path -LiteralPath $ModelPath)) {
    throw "Model file not found: $ModelPath"
}

$arguments = @(
    '--host', $BindHost,
    '--port', $Port.ToString(),
    '-m', $ModelPath
)

$commandPreview = '"' + $ServerExe + '" ' + (($arguments | ForEach-Object {
    if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
}) -join ' ')

Write-Host "Starting whisper-server with:"
Write-Host "  exe:    $ServerExe"
Write-Host "  model:  $ModelPath"
Write-Host "  host:   $BindHost"
Write-Host "  port:   $Port"
$modeLabel = if ($Detached) { 'detached' } else { 'foreground' }
Write-Host "  mode:   $modeLabel"
Write-Host ""
Write-Host $commandPreview

if ($DryRun) {
    return
}

$workingDir = Split-Path -Parent $ServerExe

if ($Detached) {
    $process = Start-Process -FilePath $ServerExe -ArgumentList $arguments -WorkingDirectory $workingDir -PassThru
    Write-Host ""
    Write-Host "whisper-server started in the background (PID $($process.Id))."
    Write-Host "Use the GUI with Server URL: http://$BindHost`:$Port"
} else {
    Write-Host ""
    Write-Host "whisper-server is starting in the current console. Press Ctrl+C to stop it."
    & $ServerExe @arguments
}
