param(
    [ValidateSet('1','2','both')]
    [string]$Preset = 'both',
    [string]$OutputDir = '',
    [int]$Retries = 3,
    [int]$TimeoutSec = 1800
)

# Downloads only text-generation GGUF presets.
# Whisper / speech and multimodal vision models are configured separately.

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot '..')
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $addonRoot 'ofxGgmlGuiExample\bin\data\models'
}

$models = @(
    @{ Id = '1'; Name = 'qwen2.5-1.5b-instruct-q4_k_m.gguf'; Url = 'https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf' },
    @{ Id = '2'; Name = 'qwen2.5-coder-1.5b-instruct-q4_k_m.gguf'; Url = 'https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf' }
)

if ($Preset -eq 'both') {
    $targets = $models
} else {
    $targets = $models | Where-Object { $_.Id -eq $Preset }
    if (-not $targets) {
        throw "Invalid preset '$Preset'. Valid values are 1, 2, both."
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function Download-Model {
    param(
        [Parameter(Mandatory=$true)][hashtable]$Model,
        [Parameter(Mandatory=$true)][string]$Directory,
        [Parameter(Mandatory=$true)][int]$RetryCount,
        [Parameter(Mandatory=$true)][int]$Timeout
    )

    $target = Join-Path $Directory $Model.Name
    if (Test-Path -LiteralPath $target) {
        Write-Host "  - $($Model.Name) already exists, skipping."
        return
    }

    Write-Host "  - Downloading $($Model.Name)"
    for ($attempt = 1; $attempt -le $RetryCount; $attempt++) {
        try {
            Invoke-WebRequest -Uri $Model.Url -OutFile $target -UseBasicParsing -TimeoutSec $Timeout
            $size = (Get-Item -LiteralPath $target).Length
            if ($size -le 0) {
                throw "Downloaded file is empty"
            }
            Write-Host "    Saved to $target"
            return
        } catch {
            if (Test-Path -LiteralPath $target) {
                Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
            }
            if ($attempt -eq $RetryCount) {
                throw "Failed to download $($Model.Name): $($_.Exception.Message)"
            }
            Start-Sleep -Seconds ([Math]::Min(2 * $attempt, 8))
        }
    }
}

foreach ($model in $targets) {
    Download-Model -Model $model -Directory $OutputDir -RetryCount $Retries -Timeout $TimeoutSec
}

Write-Host "Model download complete."
