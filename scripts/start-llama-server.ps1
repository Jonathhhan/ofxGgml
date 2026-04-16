param(
    [string]$ModelPath = "",
    [string]$ServerExe = "",
    [string]$BindHost = "127.0.0.1",
    [int]$Port = 8080,
    [int]$GpuLayers = 28,
    [int]$ContextSize = 6144,
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
        (Join-Path $addonRoot 'libs\llama\bin\llama-server.exe'),
        (Join-Path $addonRoot 'build\llama.cpp-build\bin\Release\llama-server.exe')
    )
}

if ([string]::IsNullOrWhiteSpace($ServerExe)) {
    throw "Could not find llama-server.exe. Expected it in libs\llama\bin or build\llama.cpp-build\bin\Release."
}

if ([string]::IsNullOrWhiteSpace($ModelPath)) {
    $ModelPath = Resolve-FirstExistingPath @(
        (Join-Path $addonRoot 'ofxGgmlGuiExample\bin\data\models\qwen2.5-coder-7b-instruct-q4_k_m.gguf'),
        (Join-Path $addonRoot 'ofxGgmlGuiExample\bin\data\models\qwen2.5-coder-1.5b-instruct-q4_k_m.gguf'),
        (Join-Path $addonRoot 'ofxGgmlGuiExample\bin\data\models\qwen2.5-1.5b-instruct-q4_k_m.gguf')
    )
}

if ([string]::IsNullOrWhiteSpace($ModelPath)) {
    throw "Could not find a default GGUF model. Pass -ModelPath explicitly."
}

if (-not (Test-Path -LiteralPath $ModelPath)) {
    throw "Model file not found: $ModelPath"
}

$arguments = @(
    '-m', $ModelPath,
    '--host', $BindHost,
    '--port', $Port.ToString(),
    '-ngl', ([Math]::Max(0, $GpuLayers)).ToString(),
    '-c', ([Math]::Max(512, $ContextSize)).ToString()
)

$commandPreview = '"' + $ServerExe + '" ' + (($arguments | ForEach-Object {
    if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
}) -join ' ')

Write-Host "Starting llama-server with:"
Write-Host "  exe:    $ServerExe"
Write-Host "  model:  $ModelPath"
Write-Host "  host:   $BindHost"
Write-Host "  port:   $Port"
Write-Host "  ngl:    $GpuLayers"
Write-Host "  ctx:    $ContextSize"
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
    Write-Host "llama-server started in the background (PID $($process.Id))."
    Write-Host "Use the GUI with Server URL: http://$BindHost`:$Port"
} else {
    Write-Host ""
    Write-Host "llama-server is starting in the current console. Press Ctrl+C to stop it."
    & $ServerExe @arguments
}
