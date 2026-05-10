param(
	[string]$TextServerUrl = $(if ($env:OFXGGML_TEXT_SERVER_URL) { $env:OFXGGML_TEXT_SERVER_URL } else { "http://127.0.0.1:8080" }),
	[string]$EmbeddingServerUrl = $(if ($env:OFXGGML_EMBEDDING_SERVER_URL) { $env:OFXGGML_EMBEDDING_SERVER_URL } else { "http://127.0.0.1:8081" }),
	[switch]$Strict,
	[switch]$NoServerProbe
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot "..")
. (Join-Path $scriptRoot "ofxGgml-launch-utils.ps1")

$script:Warnings = 0

function Write-Check {
	param(
		[string]$State,
		[string]$Name,
		[string]$Detail = ""
	)
	$line = "{0,-5} {1}" -f $State, $Name
	if (![string]::IsNullOrWhiteSpace($Detail)) {
		$line += " - $Detail"
	}
	Write-Host $line
	if ($State -eq "WARN") {
		$script:Warnings++
	}
}

function Test-CommandAvailable {
	param([string]$Name)
	return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Resolve-FirstExecutable {
	param([string[]]$Candidates)
	foreach ($candidate in $Candidates) {
		if (![string]::IsNullOrWhiteSpace($candidate) -and
			(Test-Path -LiteralPath $candidate -PathType Leaf)) {
			return (Resolve-Path -LiteralPath $candidate).Path
		}
	}
	return ""
}

function Test-ServerHealth {
	param([string]$Url)
	if ($NoServerProbe) {
		return "skipped"
	}
	if ([string]::IsNullOrWhiteSpace($Url)) {
		return "unset"
	}
	try {
		$response = Invoke-WebRequest -Uri ($Url.TrimEnd("/") + "/health") -UseBasicParsing -TimeoutSec 1 -ErrorAction Stop
		return "HTTP $($response.StatusCode)"
	} catch {
		return "not reachable"
	}
}

$isWindowsHost = !($IsLinux -or $IsMacOS)
$exeSuffix = if ($isWindowsHost) { ".exe" } else { "" }
$addonParent = Split-Path -Parent $addonRoot

Write-Host "ofxGgml doctor"
Write-Host "Root  $addonRoot"
Write-Host ""

Write-Check "OK" "addon root" $addonRoot

foreach ($tool in @("git", "cmake")) {
	if (Test-CommandAvailable $tool) {
		Write-Check "OK" $tool ((Get-Command $tool).Source)
	} else {
		Write-Check "WARN" $tool "not found in PATH"
	}
}

if ($isWindowsHost) {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
		Write-Check "OK" "Visual Studio locator" $vswhere
	} else {
		Write-Check "WARN" "Visual Studio locator" "vswhere.exe not found"
	}
} elseif (Test-CommandAvailable "pwsh") {
	Write-Check "OK" "PowerShell 7" ((Get-Command pwsh).Source)
} else {
	Write-Check "WARN" "PowerShell 7" "pwsh not found"
}

$ofxImGui = Join-Path $addonParent "ofxImGui"
if (Test-Path -LiteralPath $ofxImGui -PathType Container) {
	Write-Check "OK" "ofxImGui" $ofxImGui
} else {
	Write-Check "WARN" "ofxImGui" "install beside ofxGgml before building GUI examples"
}

$ggmlInclude = Join-Path $addonRoot "libs\ggml\include"
$ggmlLib = Join-Path $addonRoot "libs\ggml\lib"
if ((Test-Path -LiteralPath $ggmlInclude -PathType Container) -and
	(Test-Path -LiteralPath $ggmlLib -PathType Container)) {
	Write-Check "OK" "ggml runtime" "include/lib are present"
} else {
	Write-Check "WARN" "ggml runtime" "run scripts\setup-ggml.bat"
}

$llamaBin = Join-Path $addonRoot "libs\llama\bin"
$llamaServer = Resolve-FirstExecutable @((Join-Path $llamaBin "llama-server$exeSuffix"))
$llamaCli = Resolve-FirstExecutable @((Join-Path $llamaBin "llama-cli$exeSuffix"))
$llamaEmbedding = Resolve-FirstExecutable @((Join-Path $llamaBin "llama-embedding$exeSuffix"))
if ($llamaServer -and $llamaCli -and $llamaEmbedding) {
	Write-Check "OK" "llama.cpp tools" $llamaBin
} else {
	Write-Check "WARN" "llama.cpp tools" "run scripts\build-llama-server.bat"
}

$textModel = Find-OfxGgmlFirstModel (Get-OfxGgmlModelSearchDirectories `
	-AddonRoot $addonRoot `
	-ExampleRoot (Join-Path $addonRoot "ofxGgmlTextExample") `
	-ExtraExampleNames @("ofxGgmlChatExample"))
if ($textModel) {
	Write-Check "OK" "text model" $textModel
} else {
	Write-Check "WARN" "text model" "put a GGUF under addons\models, ofxGgml\models, or pass -Model"
}

$embeddingModel = Find-OfxGgmlFirstModel (Get-OfxGgmlModelSearchDirectories `
	-AddonRoot $addonRoot `
	-ExampleRoot (Join-Path $addonRoot "ofxGgmlEmbeddingExample") `
	-ExtraExampleNames @("ofxGgmlTextExample", "ofxGgmlChatExample"))
if ($embeddingModel) {
	Write-Check "OK" "embedding model" $embeddingModel
} else {
	Write-Check "WARN" "embedding model" "use an embedding-tuned GGUF for meaningful vectors"
}

$examples = @(
	@{
		Name = "ofxGgmlSimpleExample"
		Command = "scripts\build-simple-example.bat"
	},
	@{
		Name = "ofxGgmlTextExample"
		Command = "scripts\run-text-example.bat -Build"
	},
	@{
		Name = "ofxGgmlChatExample"
		Command = "scripts\run-chat-example.bat -Build"
	},
	@{
		Name = "ofxGgmlEmbeddingExample"
		Command = "scripts\run-embedding-example.bat -Build"
	}
)
foreach ($example in $examples) {
	$exampleName = $example.Name
	$exe = Join-Path $addonRoot "$exampleName\bin\$exampleName$exeSuffix"
	if (Test-Path -LiteralPath $exe -PathType Leaf) {
		Write-Check "OK" $exampleName "built"
	} else {
		Write-Check "WARN" $exampleName "run $($example.Command)"
	}
}

$textHealth = Test-ServerHealth $TextServerUrl
if ($textHealth -like "HTTP *" -or $textHealth -eq "skipped") {
	Write-Check "OK" "text server" "$TextServerUrl ($textHealth)"
} else {
	Write-Check "WARN" "text server" "$TextServerUrl ($textHealth)"
}

$embeddingHealth = Test-ServerHealth $EmbeddingServerUrl
if ($embeddingHealth -like "HTTP *" -or $embeddingHealth -eq "skipped") {
	Write-Check "OK" "embedding server" "$EmbeddingServerUrl ($embeddingHealth)"
} else {
	Write-Check "WARN" "embedding server" "$EmbeddingServerUrl ($embeddingHealth)"
}

Write-Host ""
if ($script:Warnings -eq 0) {
	Write-Host "Doctor passed."
} else {
	Write-Host "Doctor found $script:Warnings warning(s)."
	if ($Strict) {
		exit 1
	}
}
