param(
	[string]$ServerUrl = $(if ($env:OFXGGML_EMBEDDING_SERVER_URL) { $env:OFXGGML_EMBEDDING_SERVER_URL } else { "http://127.0.0.1:8081" }),
	[string]$ServerModel = $env:OFXGGML_EMBEDDING_SERVER_MODEL,
	[string]$Model = $(if ($env:OFXGGML_EMBEDDING_MODEL) { $env:OFXGGML_EMBEDDING_MODEL } elseif ($env:OFXGGML_TEXT_MODEL) { $env:OFXGGML_TEXT_MODEL } else { "" }),
	[switch]$Build,
	[switch]$NoAutoServer,
	[switch]$StrictModel,
	[switch]$DryRun,
	[string]$Configuration = "Release",
	[string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Split-Path -Parent $scriptRoot
$exampleRoot = Join-Path $addonRoot "ofxGgmlEmbeddingExample"
$exampleExe = Join-Path $exampleRoot "bin\ofxGgmlEmbeddingExample.exe"
. (Join-Path $scriptRoot "ofxGgml-launch-utils.ps1")

if ($env:OFXGGML_LAUNCH_DRY_RUN_ONLY -eq "1") {
	$Build = $false
	$DryRun = $true
	$NoAutoServer = $true
}

if ($Build) {
	& (Join-Path $scriptRoot "build-embedding-example.ps1") -Configuration $Configuration -Platform $Platform
	if ($LASTEXITCODE -ne 0) {
		exit $LASTEXITCODE
	}
}

if ((Test-Path -LiteralPath $exampleExe -PathType Leaf)) {
	$exampleExeExists = $true
} elseif ($DryRun) {
	$exampleExeExists = $false
	Write-Warning "Embedding example executable was not found: $exampleExe"
} else {
	throw "Embedding example executable was not found: $exampleExe. Run scripts\run-embedding-example.bat -Build or scripts\build-embedding-example.bat first."
}

$Model = Normalize-OfxGgmlPathText $Model
$ServerUrl = Normalize-OfxGgmlPathText $ServerUrl
$ServerModel = Normalize-OfxGgmlPathText $ServerModel
if ([string]::IsNullOrWhiteSpace($ServerUrl)) {
	$ServerUrl = "http://127.0.0.1:8081"
}

if ([string]::IsNullOrWhiteSpace($Model)) {
	$modelDirs = Get-OfxGgmlModelSearchDirectories `
		-AddonRoot $addonRoot `
		-ExampleRoot $exampleRoot `
		-ExtraExampleNames @("ofxGgmlTextExample", "ofxGgmlChatExample")
	if ($StrictModel) {
		$Model = Find-OfxGgmlFirstModelByRole -Directories $modelDirs -PreferredRoles @("embedding")
	} else {
		$Model = Find-OfxGgmlFirstModelByRole -Directories $modelDirs -PreferredRoles @("embedding", "text")
	}
}

$strictModelCandidate = if ([string]::IsNullOrWhiteSpace($ServerModel)) { $Model } else { $ServerModel }
$strictModelRole = Get-OfxGgmlModelRoleHint -Name ([IO.Path]::GetFileName($strictModelCandidate))
if ($StrictModel -and [string]::IsNullOrWhiteSpace($strictModelCandidate)) {
	throw "Strict embedding mode requires an embedding model path. Pass -Model or -ServerModel."
}
if ($StrictModel -and $strictModelRole -ne "embedding") {
	throw "Strict embedding mode requires an embedding model path; '$strictModelCandidate' does not match embedding model naming patterns."
}

$env:OFXGGML_EMBEDDING_SERVER_URL = $ServerUrl
if (![string]::IsNullOrWhiteSpace($ServerModel)) {
	$env:OFXGGML_EMBEDDING_SERVER_MODEL = $ServerModel
} elseif (![string]::IsNullOrWhiteSpace($Model)) {
	$env:OFXGGML_EMBEDDING_SERVER_MODEL = $Model
}
if (![string]::IsNullOrWhiteSpace($Model)) {
	$env:OFXGGML_EMBEDDING_MODEL = $Model
	Write-OfxGgmlStep "Using embedding model: $Model"
} else {
	if ($StrictModel) {
		throw "No embedding model found for strict mode. Pass -Model with an embedding GGUF."
	}
	Write-Warning "No GGUF model found. The example can still connect to an already-running server."
}
if ($StrictModel) {
	$env:OFXGGML_EMBEDDING_STRICT_MODEL = "1"
} else {
	$env:OFXGGML_EMBEDDING_STRICT_MODEL = "0"
}

Write-OfxGgmlStep "Using embedding server: $ServerUrl"
if (![string]::IsNullOrWhiteSpace($ServerModel)) {
	Write-OfxGgmlStep "Using server model: $ServerModel"
} elseif (![string]::IsNullOrWhiteSpace($Model)) {
	Write-OfxGgmlStep "Using server model: $Model"
}
if ($StrictModel) {
	Write-OfxGgmlStep "Strict embedding model filtering: enabled"
}
if ($DryRun) {
	Write-OfxGgmlStep "Executable: $exampleExe"
	Write-OfxGgmlStep "Auto server: $(if ($NoAutoServer) { 'off' } else { 'on' })"
	return
}
Start-OfxGgmlBundledLlamaServerIfNeeded `
	-ScriptRoot $scriptRoot `
	-AddonRoot $addonRoot `
	-ServerUrl $ServerUrl `
	-Model $Model `
	-LogDir (Join-Path $addonRoot "build\llama-embedding-server") `
	-MissingModelWarning "No GGUF model found. Put an embedding GGUF under addons\models or pass -Model C:\path\to\embedding-model.gguf." `
	-StartMessage "embedding llama-server is not responding; starting bundled server" `
	-NoAutoServer:$NoAutoServer `
	-Embeddings

Write-OfxGgmlStep "Starting ofxGgmlEmbeddingExample"
& $exampleExe
exit $LASTEXITCODE
