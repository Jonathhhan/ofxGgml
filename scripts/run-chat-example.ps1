param(
	[string]$Backend = $(if ($env:OFXGGML_TEXT_BACKEND) { $env:OFXGGML_TEXT_BACKEND } else { "server" }),
	[string]$ServerUrl = $(if ($env:OFXGGML_TEXT_SERVER_URL) { $env:OFXGGML_TEXT_SERVER_URL } else { "http://127.0.0.1:8080" }),
	[string]$ServerModel = $env:OFXGGML_TEXT_SERVER_MODEL,
	[string]$LlamaCli = $env:OFXGGML_LLAMA_CLI,
	[string]$Model = $env:OFXGGML_TEXT_MODEL,
	[switch]$Build,
	[switch]$StrictModel,
	[switch]$NoAutoServer,
	[switch]$DryRun,
	[string]$Configuration = "Release",
	[string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Split-Path -Parent $scriptRoot
$exampleRoot = Join-Path $addonRoot "ofxGgmlChatExample"
$exampleExe = Join-Path $exampleRoot "bin\ofxGgmlChatExample.exe"
. (Join-Path $scriptRoot "ofxGgml-launch-utils.ps1")

if ($env:OFXGGML_LAUNCH_DRY_RUN_ONLY -eq "1") {
	$Build = $false
	$DryRun = $true
	$NoAutoServer = $true
}

if ($Build) {
	& (Join-Path $scriptRoot "build-chat-example.ps1") -Configuration $Configuration -Platform $Platform
	if ($LASTEXITCODE -ne 0) {
		exit $LASTEXITCODE
	}
}

if ((Test-Path -LiteralPath $exampleExe -PathType Leaf)) {
	$exampleExeExists = $true
} elseif ($DryRun) {
	$exampleExeExists = $false
	Write-Warning "Chat example executable was not found: $exampleExe"
} else {
	throw "Chat example executable was not found: $exampleExe. Run scripts\run-chat-example.bat -Build or scripts\build-chat-example.bat first."
}

$LlamaCli = Normalize-OfxGgmlPathText $LlamaCli
$Model = Normalize-OfxGgmlPathText $Model
$Backend = Normalize-OfxGgmlPathText $Backend
$ServerUrl = Normalize-OfxGgmlPathText $ServerUrl
$ServerModel = Normalize-OfxGgmlPathText $ServerModel
if ([string]::IsNullOrWhiteSpace($Backend)) {
	$Backend = "server"
}

if ($Backend -ieq "cli" -and [string]::IsNullOrWhiteSpace($LlamaCli)) {
	$LlamaCli = Find-OfxGgmlLlamaCli -AddonRoot $addonRoot -ExampleRoot $exampleRoot
}

if ([string]::IsNullOrWhiteSpace($Model)) {
	$modelDirs = Get-OfxGgmlModelSearchDirectories `
		-AddonRoot $addonRoot `
		-ExampleRoot $exampleRoot
	if ($StrictModel) {
		$Model = Find-OfxGgmlFirstModelByRole -Directories $modelDirs -PreferredRoles @("text")
	} else {
		$Model = Find-OfxGgmlFirstModelByRole -Directories $modelDirs -PreferredRoles @("text", "embedding")
	}
}

$strictModelCandidate = if ([string]::IsNullOrWhiteSpace($ServerModel)) { $Model } else { $ServerModel }
$strictModelRole = Get-OfxGgmlModelRoleHint -Name ([IO.Path]::GetFileName($strictModelCandidate))
if ($StrictModel -and [string]::IsNullOrWhiteSpace($strictModelCandidate)) {
	throw "Strict text model mode requires a text GGUF. Pass -Model or -ServerModel."
}
if ($StrictModel -and $strictModelRole -ne "text") {
	throw "Strict text model mode requires a text GGUF model; '$strictModelCandidate' does not match text model naming patterns."
}

if ($Backend -ieq "server") {
	$env:OFXGGML_TEXT_BACKEND = "server"
	$env:OFXGGML_TEXT_SERVER_URL = $ServerUrl
	if (![string]::IsNullOrWhiteSpace($ServerModel)) {
		$env:OFXGGML_TEXT_SERVER_MODEL = $ServerModel
	}
	Write-OfxGgmlStep "Using llama-server: $ServerUrl"
	if (![string]::IsNullOrWhiteSpace($ServerModel)) {
		Write-OfxGgmlStep "Using server model: $ServerModel"
	}
	if (!$DryRun) {
		Start-OfxGgmlBundledLlamaServerIfNeeded `
			-ScriptRoot $scriptRoot `
			-AddonRoot $addonRoot `
			-ServerUrl $ServerUrl `
			-Model $Model `
			-LogDir (Join-Path $addonRoot "build\llama-server") `
			-MissingModelWarning "No GGUF model found. Put one under addons\models or pass -Model C:\path\to\model.gguf." `
			-StartMessage "llama-server is not responding; starting bundled server" `
			-NoAutoServer:$NoAutoServer
	}
} elseif (![string]::IsNullOrWhiteSpace($LlamaCli)) {
	$env:OFXGGML_TEXT_BACKEND = "cli"
	$env:OFXGGML_LLAMA_CLI = $LlamaCli
	Write-OfxGgmlStep "Using llama.cpp CLI: $LlamaCli"
} else {
	Write-Warning "No llama.cpp CLI found. The example will show setup instructions."
}

if (![string]::IsNullOrWhiteSpace($Model)) {
	$env:OFXGGML_TEXT_MODEL = $Model
	Write-OfxGgmlStep "Using text model: $Model"
} elseif ($Backend -ieq "cli") {
	Write-Warning "No GGUF model found. The example will show setup instructions."
}
if ($StrictModel) {
	$env:OFXGGML_TEXT_STRICT_MODEL = "1"
} else {
	$env:OFXGGML_TEXT_STRICT_MODEL = "0"
}
if ($StrictModel) {
	Write-OfxGgmlStep "Strict text model filtering: enabled"
}

if ($DryRun) {
	Write-OfxGgmlStep "Executable: $exampleExe"
	Write-OfxGgmlStep "Auto server: $(if ($NoAutoServer) { 'off' } else { 'on' })"
	return
}

Write-OfxGgmlStep "Starting ofxGgmlChatExample"
& $exampleExe
exit $LASTEXITCODE
