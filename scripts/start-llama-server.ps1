param(
	[string]$ModelPath = $(if ($env:OFXGGML_TEXT_MODEL) { $env:OFXGGML_TEXT_MODEL } else { "" }),
	[string]$ServerExe = $(if ($env:OFXGGML_LLAMA_SERVER) { $env:OFXGGML_LLAMA_SERVER } else { "" }),
	[string]$HostName = "127.0.0.1",
	[int]$Port = 8080,
	[int]$GpuLayers = 28,
	[int]$ContextSize = 4096,
	[string]$EmbeddingPooling = "mean",
	[switch]$NoCudaGraphs,
	[switch]$Embeddings,
	[switch]$Detached,
	[switch]$ForceNew,
	[switch]$NoHealthCheck,
	[int]$StartupTimeoutSeconds = 30,
	[string]$LogDir = "",
	[switch]$DryRun
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot "..")
. (Join-Path $scriptRoot "ofxGgml-launch-utils.ps1")
Normalize-OfxGgmlWindowsPathEnvironment

if ($env:OFXGGML_LAUNCH_DRY_RUN_ONLY -eq "1") {
	$DryRun = $true
	$Detached = $false
	$NoHealthCheck = $true
}

function Get-ServerUrl {
	param([string]$HostValue, [int]$PortValue)
	return "http://$HostValue`:$PortValue"
}

function Get-HealthUrl {
	param([string]$ServerUrl)
	return "$ServerUrl/health"
}

function Test-LlamaServer {
	param([string]$ServerUrl)
	$result = [ordered]@{
		Reachable = $false
		Ready = $false
		StatusCode = 0
		Message = ""
	}
	try {
		$response = Invoke-WebRequest `
			-Uri (Get-HealthUrl $ServerUrl) `
			-UseBasicParsing `
			-TimeoutSec 2 `
			-ErrorAction Stop
		$result.Reachable = $true
		$result.Ready = ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300)
		$result.StatusCode = [int]$response.StatusCode
		$result.Message = ($response.Content | Out-String).Trim()
	} catch {
		if ($_.Exception.Response) {
			$result.Reachable = $true
			$result.StatusCode = [int]$_.Exception.Response.StatusCode
			$result.Message = $_.Exception.Message
		} else {
			$result.Message = $_.Exception.Message
		}
	}
	return [pscustomobject]$result
}

function Wait-LlamaServer {
	param([string]$ServerUrl, [int]$TimeoutSeconds)
	$deadline = (Get-Date).AddSeconds([Math]::Max(1, $TimeoutSeconds))
	do {
		$health = Test-LlamaServer $ServerUrl
		if ($health.Ready) {
			return $health
		}
		Start-Sleep -Milliseconds 500
	} while ((Get-Date) -lt $deadline)
	return Test-LlamaServer $ServerUrl
}

function Join-ProcessArguments {
	param([string[]]$Arguments)
	$quoted = foreach ($argument in $Arguments) {
		if ($null -eq $argument) {
			'""'
		} elseif ($argument -match '[\s"]') {
			'"' + ($argument.Replace('"', '\"')) + '"'
		} else {
			$argument
		}
	}
	return ($quoted -join " ")
}

if ([string]::IsNullOrWhiteSpace($ServerExe)) {
	$serverName = if ($IsLinux -or $IsMacOS) { "llama-server" } else { "llama-server.exe" }
	$ServerExe = Resolve-OfxGgmlFirstFile @(
		(Join-Path $addonRoot "libs\llama\bin\$serverName"),
		(Join-Path $addonRoot "libs\llama.cpp\build\bin\Release\$serverName"),
		(Join-Path $addonRoot "libs\llama.cpp\build\bin\$serverName"),
		(Join-Path $addonRoot "libs\llama.cpp\build\$serverName")
	)
}
if ([string]::IsNullOrWhiteSpace($ServerExe)) {
	throw "Could not find llama-server. Build it with scripts\build-llama-server.bat or pass -ServerExe."
}

if ([string]::IsNullOrWhiteSpace($ModelPath) -and $Embeddings -and $env:OFXGGML_EMBEDDING_MODEL) {
	$ModelPath = $env:OFXGGML_EMBEDDING_MODEL
}

if ([string]::IsNullOrWhiteSpace($ModelPath)) {
	$primaryExample = if ($Embeddings) { "ofxGgmlEmbeddingExample" } else { "ofxGgmlTextExample" }
	$extraExamples = if ($Embeddings) {
		@("ofxGgmlTextExample", "ofxGgmlChatExample")
	} else {
		@("ofxGgmlChatExample")
	}
	$ModelPath = Find-OfxGgmlFirstModel (Get-OfxGgmlModelSearchDirectories `
		-AddonRoot $addonRoot `
		-ExampleRoot (Join-Path $addonRoot $primaryExample) `
		-ExtraExampleNames $extraExamples)
}
if ([string]::IsNullOrWhiteSpace($ModelPath)) {
	$modelEnv = if ($Embeddings) { "OFXGGML_EMBEDDING_MODEL" } else { "OFXGGML_TEXT_MODEL" }
	throw "Could not find a GGUF model. Pass -ModelPath or set $modelEnv."
}
if (!(Test-Path -LiteralPath $ModelPath -PathType Leaf)) {
	throw "Model file was not found: $ModelPath"
}
$ModelPath = (Resolve-Path -LiteralPath $ModelPath).Path
$ServerExe = (Resolve-Path -LiteralPath $ServerExe).Path

if ($Embeddings -and !$PSBoundParameters.ContainsKey("Port")) {
	$Port = 8081
}

$serverUrl = Get-ServerUrl $HostName $Port
if (!$NoHealthCheck -and !$DryRun) {
	$existing = Test-LlamaServer $serverUrl
	if ($existing.Reachable -and !$ForceNew) {
		Write-Host "llama-server is already reachable."
		Write-Host "  url:       $serverUrl"
		Write-Host "  health:    HTTP $($existing.StatusCode) $(if ($existing.Ready) { '(ready)' } else { '(starting/busy)' })"
		if (![string]::IsNullOrWhiteSpace($existing.Message)) {
			Write-Host "  response:  $($existing.Message)"
		}
		Write-Host ""
		Write-Host "Reusing the existing server. Pass -ForceNew to start another process anyway."
		Write-Host "Use OFXGGML_TEXT_SERVER_URL=$serverUrl"
		return
	}
}

$arguments = @(
	"-m", $ModelPath,
	"--host", $HostName,
	"--port", $Port.ToString(),
	"-ngl", ([Math]::Max(0, $GpuLayers)).ToString(),
	"-c", ([Math]::Max(512, $ContextSize)).ToString()
)
if ($NoCudaGraphs) {
	$arguments += "--no-cuda-graphs"
}
if ($Embeddings) {
	$arguments += "--embeddings"
	if (![string]::IsNullOrWhiteSpace($EmbeddingPooling)) {
		$arguments += "--pooling"
		$arguments += $EmbeddingPooling
	}
}

Write-Host "Starting llama-server"
Write-Host "  exe:       $ServerExe"
Write-Host "  model:     $ModelPath"
Write-Host "  url:       $serverUrl"
Write-Host "  backend:   llama.cpp auto"
Write-Host "  ngl:       $GpuLayers"
Write-Host "  ctx:       $ContextSize"
Write-Host "  cudaGraph: $(if ($NoCudaGraphs) { 'off' } else { 'on' })"
Write-Host "  embeddings: $(if ($Embeddings) { 'on' } else { 'off' })"
if ($Embeddings) {
	Write-Host "  serverMode: embeddings only"
	Write-Host "  pooling:   $EmbeddingPooling"
}
Write-Host "  mode:      $(if ($Detached) { 'detached' } else { 'foreground' })"
if (![string]::IsNullOrWhiteSpace($LogDir)) {
	Write-Host "  logs:      $LogDir"
}
Write-Host ""
Write-Host ("`"$ServerExe`" " + (Join-ProcessArguments $arguments))

if ($DryRun) {
	return
}

$workingDir = Split-Path -Parent $ServerExe
if ($Detached) {
	$startInfo = New-Object System.Diagnostics.ProcessStartInfo
	$startInfo.FileName = $ServerExe
	$startInfo.Arguments = Join-ProcessArguments $arguments
	$startInfo.WorkingDirectory = $workingDir
	$startInfo.UseShellExecute = $true
	$startInfo.CreateNoWindow = $true
	$startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
	if (![string]::IsNullOrWhiteSpace($LogDir)) {
		New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
	}
	$process = New-Object System.Diagnostics.Process
	$process.StartInfo = $startInfo
	if (![string]::IsNullOrWhiteSpace($LogDir)) {
		Set-Content -LiteralPath (Join-Path $LogDir "llama-server.command.txt") `
			-Value ("`"$ServerExe`" " + (Join-ProcessArguments $arguments))
	}
	if (!$process.Start()) {
		throw "Failed to start llama-server."
	}
	Write-Host ""
	Write-Host "llama-server started in the background (PID $($process.Id))."
	Write-Host "Use OFXGGML_TEXT_SERVER_URL=$serverUrl"
	if (![string]::IsNullOrWhiteSpace($LogDir)) {
		Write-Host "Command line saved in $LogDir"
	}
	if (!$NoHealthCheck -and $StartupTimeoutSeconds -gt 0) {
		Write-Host "Checking llama-server health once..."
		Start-Sleep -Milliseconds 500
		$health = Test-LlamaServer $serverUrl
		if ($health.Ready) {
			Write-Host "llama-server is ready at $serverUrl"
		} elseif ($health.Reachable) {
			Write-Warning "llama-server is reachable but still loading (HTTP $($health.StatusCode))."
		} else {
			Write-Warning "llama-server is still starting. Check $serverUrl/health in a moment."
		}
	}
	Write-Output "OFXGGML_LLAMA_SERVER_PID=$($process.Id)"
} else {
	Write-Host ""
	Write-Host "llama-server is running in this console. Press Ctrl+C to stop it."
	Write-Host "Use OFXGGML_TEXT_SERVER_URL=$serverUrl"
	& $ServerExe @arguments
}
