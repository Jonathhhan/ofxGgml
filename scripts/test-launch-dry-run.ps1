param(
	[string]$Configuration = "Release",
	[string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

function Write-Step {
	param([string]$Message)
	Write-Host "==> $Message"
}

function Invoke-DryRun {
	param(
		[string]$Label,
		[string]$Script,
		[hashtable]$Parameters
	)
	Write-Step $Label
	$previousDryRunOnly = $env:OFXGGML_LAUNCH_DRY_RUN_ONLY
	$previousTextBackend = $env:OFXGGML_TEXT_BACKEND
	$previousTextServerUrl = $env:OFXGGML_TEXT_SERVER_URL
	$previousTextServerModel = $env:OFXGGML_TEXT_SERVER_MODEL
	$previousTextModel = $env:OFXGGML_TEXT_MODEL
	$previousLlamaCli = $env:OFXGGML_LLAMA_CLI
	$previousEmbeddingServerUrl = $env:OFXGGML_EMBEDDING_SERVER_URL
	$previousEmbeddingServerModel = $env:OFXGGML_EMBEDDING_SERVER_MODEL
	$env:OFXGGML_LAUNCH_DRY_RUN_ONLY = "1"
	try {
		$output = & $Script @Parameters *>&1 | ForEach-Object { $_.ToString() }
		if (!$?) {
			throw "$Label failed."
		}
	} finally {
		if ($null -eq $previousDryRunOnly) {
			Remove-Item Env:\OFXGGML_LAUNCH_DRY_RUN_ONLY -ErrorAction SilentlyContinue
		} else {
			$env:OFXGGML_LAUNCH_DRY_RUN_ONLY = $previousDryRunOnly
		}
		if ($null -eq $previousTextBackend) {
			Remove-Item Env:\OFXGGML_TEXT_BACKEND -ErrorAction SilentlyContinue
		} else {
			$env:OFXGGML_TEXT_BACKEND = $previousTextBackend
		}
		if ($null -eq $previousTextServerUrl) {
			Remove-Item Env:\OFXGGML_TEXT_SERVER_URL -ErrorAction SilentlyContinue
		} else {
			$env:OFXGGML_TEXT_SERVER_URL = $previousTextServerUrl
		}
		if ($null -eq $previousTextServerModel) {
			Remove-Item Env:\OFXGGML_TEXT_SERVER_MODEL -ErrorAction SilentlyContinue
		} else {
			$env:OFXGGML_TEXT_SERVER_MODEL = $previousTextServerModel
		}
		if ($null -eq $previousTextModel) {
			Remove-Item Env:\OFXGGML_TEXT_MODEL -ErrorAction SilentlyContinue
		} else {
			$env:OFXGGML_TEXT_MODEL = $previousTextModel
		}
		if ($null -eq $previousLlamaCli) {
			Remove-Item Env:\OFXGGML_LLAMA_CLI -ErrorAction SilentlyContinue
		} else {
			$env:OFXGGML_LLAMA_CLI = $previousLlamaCli
		}
		if ($null -eq $previousEmbeddingServerUrl) {
			Remove-Item Env:\OFXGGML_EMBEDDING_SERVER_URL -ErrorAction SilentlyContinue
		} else {
			$env:OFXGGML_EMBEDDING_SERVER_URL = $previousEmbeddingServerUrl
		}
		if ($null -eq $previousEmbeddingServerModel) {
			Remove-Item Env:\OFXGGML_EMBEDDING_SERVER_MODEL -ErrorAction SilentlyContinue
		} else {
			$env:OFXGGML_EMBEDDING_SERVER_MODEL = $previousEmbeddingServerModel
		}
	}
	return @($output)
}

function Assert-Contains {
	param(
		[string[]]$Output,
		[string]$Needle,
		[string]$Label
	)
	$text = $Output -join "`n"
	if ($text -notlike "*$Needle*") {
		throw "$Label did not contain expected text: $Needle`n$text"
	}
}

function Assert-NotContains {
	param(
		[string[]]$Output,
		[string]$Needle,
		[string]$Label
	)
	$text = $Output -join "`n"
	if ($text -like "*$Needle*") {
		throw "$Label contained unexpected text: $Needle`n$text"
	}
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot "..")
$scratchDir = Join-Path $addonRoot "build\launch-dry-run-smoke"
New-Item -ItemType Directory -Force -Path $scratchDir | Out-Null

$modelPath = Join-Path $scratchDir "dry-run-model.gguf"
$serverExe = Join-Path $scratchDir "llama-server.exe"
$llamaCliExe = Join-Path $scratchDir "llama-cli.exe"
if (!(Test-Path -LiteralPath $modelPath -PathType Leaf)) {
	New-Item -ItemType File -Path $modelPath | Out-Null
}
if (!(Test-Path -LiteralPath $serverExe -PathType Leaf)) {
	New-Item -ItemType File -Path $serverExe | Out-Null
}
if (!(Test-Path -LiteralPath $llamaCliExe -PathType Leaf)) {
	New-Item -ItemType File -Path $llamaCliExe | Out-Null
}

$textOutput = Invoke-DryRun `
	-Label "Text example dry-run" `
	-Script (Join-Path $scriptRoot "run-text-example.ps1") `
	-Parameters @{
		DryRun = $true
		NoAutoServer = $true
		ServerUrl = "http://127.0.0.1:9080"
		ServerModel = "dry-text-model"
		Model = $modelPath
		Configuration = $Configuration
		Platform = $Platform
	}
Assert-Contains $textOutput "Using llama-server: http://127.0.0.1:9080" "Text dry-run"
Assert-Contains $textOutput "Using server model: dry-text-model" "Text dry-run"
Assert-Contains $textOutput "Using text model: $modelPath" "Text dry-run"
Assert-Contains $textOutput "Executable:" "Text dry-run"
Assert-Contains $textOutput "Auto server: off" "Text dry-run"
Assert-NotContains $textOutput "Starting ofxGgmlTextExample" "Text dry-run"

$textCliOutput = Invoke-DryRun `
	-Label "Text example CLI dry-run" `
	-Script (Join-Path $scriptRoot "run-text-example.ps1") `
	-Parameters @{
		DryRun = $true
		Backend = "cli"
		LlamaCli = $llamaCliExe
		Model = $modelPath
		Configuration = $Configuration
		Platform = $Platform
	}
Assert-Contains $textCliOutput "Using llama.cpp CLI: $llamaCliExe" "Text CLI dry-run"
Assert-Contains $textCliOutput "Using text model: $modelPath" "Text CLI dry-run"
Assert-Contains $textCliOutput "Executable:" "Text CLI dry-run"
Assert-Contains $textCliOutput "Auto server: off" "Text CLI dry-run"
Assert-NotContains $textCliOutput "Using llama-server:" "Text CLI dry-run"
Assert-NotContains $textCliOutput "Starting ofxGgmlTextExample" "Text CLI dry-run"

$chatOutput = Invoke-DryRun `
	-Label "Chat example dry-run" `
	-Script (Join-Path $scriptRoot "run-chat-example.ps1") `
	-Parameters @{
		DryRun = $true
		NoAutoServer = $true
		ServerUrl = "http://127.0.0.1:9080"
		ServerModel = "dry-chat-model"
		Model = $modelPath
		Configuration = $Configuration
		Platform = $Platform
	}
Assert-Contains $chatOutput "Using llama-server: http://127.0.0.1:9080" "Chat dry-run"
Assert-Contains $chatOutput "Using server model: dry-chat-model" "Chat dry-run"
Assert-Contains $chatOutput "Using text model: $modelPath" "Chat dry-run"
Assert-Contains $chatOutput "Executable:" "Chat dry-run"
Assert-Contains $chatOutput "Auto server: off" "Chat dry-run"
Assert-NotContains $chatOutput "Starting ofxGgmlChatExample" "Chat dry-run"

$chatCliOutput = Invoke-DryRun `
	-Label "Chat example CLI dry-run" `
	-Script (Join-Path $scriptRoot "run-chat-example.ps1") `
	-Parameters @{
		DryRun = $true
		Backend = "cli"
		LlamaCli = $llamaCliExe
		Model = $modelPath
		Configuration = $Configuration
		Platform = $Platform
	}
Assert-Contains $chatCliOutput "Using llama.cpp CLI: $llamaCliExe" "Chat CLI dry-run"
Assert-Contains $chatCliOutput "Using text model: $modelPath" "Chat CLI dry-run"
Assert-Contains $chatCliOutput "Executable:" "Chat CLI dry-run"
Assert-Contains $chatCliOutput "Auto server: off" "Chat CLI dry-run"
Assert-NotContains $chatCliOutput "Using llama-server:" "Chat CLI dry-run"
Assert-NotContains $chatCliOutput "Starting ofxGgmlChatExample" "Chat CLI dry-run"

$embeddingOutput = Invoke-DryRun `
	-Label "Embedding example dry-run" `
	-Script (Join-Path $scriptRoot "run-embedding-example.ps1") `
	-Parameters @{
		DryRun = $true
		NoAutoServer = $true
		ServerUrl = "http://127.0.0.1:9081"
		ServerModel = "dry-embedding-model"
		Model = $modelPath
		Configuration = $Configuration
		Platform = $Platform
	}
Assert-Contains $embeddingOutput "Using embedding server: http://127.0.0.1:9081" "Embedding dry-run"
Assert-Contains $embeddingOutput "Using server model: dry-embedding-model" "Embedding dry-run"
Assert-Contains $embeddingOutput "Using embedding model: $modelPath" "Embedding dry-run"
Assert-Contains $embeddingOutput "Executable:" "Embedding dry-run"
Assert-Contains $embeddingOutput "Auto server: off" "Embedding dry-run"
Assert-NotContains $embeddingOutput "Starting ofxGgmlEmbeddingExample" "Embedding dry-run"

$serverOutput = Invoke-DryRun `
	-Label "llama-server dry-run" `
	-Script (Join-Path $scriptRoot "start-llama-server.ps1") `
	-Parameters @{
		DryRun = $true
		ServerExe = $serverExe
		ModelPath = $modelPath
		HostName = "127.0.0.1"
		Port = 9082
		NoCudaGraphs = $true
	}
Assert-Contains $serverOutput "exe:       $serverExe" "Server dry-run"
Assert-Contains $serverOutput "model:     $modelPath" "Server dry-run"
Assert-Contains $serverOutput "url:       http://127.0.0.1:9082" "Server dry-run"
Assert-Contains $serverOutput "cudaGraph: off" "Server dry-run"
Assert-Contains $serverOutput "--no-cuda-graphs" "Server dry-run"

$embeddingServerOutput = Invoke-DryRun `
	-Label "embedding llama-server dry-run" `
	-Script (Join-Path $scriptRoot "start-llama-server.ps1") `
	-Parameters @{
		DryRun = $true
		Embeddings = $true
		ServerExe = $serverExe
		ModelPath = $modelPath
	}
Assert-Contains $embeddingServerOutput "url:       http://127.0.0.1:8081" "Embedding server dry-run"
Assert-Contains $embeddingServerOutput "embeddings: on" "Embedding server dry-run"
Assert-Contains $embeddingServerOutput "pooling:   mean" "Embedding server dry-run"
Assert-Contains $embeddingServerOutput "--embeddings" "Embedding server dry-run"
Assert-Contains $embeddingServerOutput "--pooling mean" "Embedding server dry-run"

$previousEmbeddingModel = $env:OFXGGML_EMBEDDING_MODEL
$env:OFXGGML_EMBEDDING_MODEL = $modelPath
try {
	$embeddingServerEnvOutput = Invoke-DryRun `
		-Label "embedding llama-server env model dry-run" `
		-Script (Join-Path $scriptRoot "start-llama-server.ps1") `
		-Parameters @{
			DryRun = $true
			Embeddings = $true
			ServerExe = $serverExe
		}
} finally {
	if ($null -eq $previousEmbeddingModel) {
		Remove-Item Env:\OFXGGML_EMBEDDING_MODEL -ErrorAction SilentlyContinue
	} else {
		$env:OFXGGML_EMBEDDING_MODEL = $previousEmbeddingModel
	}
}
Assert-Contains $embeddingServerEnvOutput "model:     $modelPath" "Embedding server env model dry-run"
Assert-Contains $embeddingServerEnvOutput "url:       http://127.0.0.1:8081" "Embedding server env model dry-run"

Write-Step "Launch dry-run smoke coverage passed"
