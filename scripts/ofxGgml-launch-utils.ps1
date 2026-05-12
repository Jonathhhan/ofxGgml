$ErrorActionPreference = "Stop"

function Write-OfxGgmlStep {
	param([string]$Message)
	Write-Host "==> $Message"
}

function Test-OfxGgmlWindowsHost {
	return !($IsLinux -or $IsMacOS)
}

function Normalize-OfxGgmlWindowsPathEnvironment {
	if (!(Test-OfxGgmlWindowsHost)) {
		return
	}

	$variables = [Environment]::GetEnvironmentVariables("Process")
	$pathNames = New-Object System.Collections.Generic.List[string]
	foreach ($key in $variables.Keys) {
		$name = [string]$key
		if ($name.Equals("Path", [System.StringComparison]::OrdinalIgnoreCase)) {
			$pathNames.Add($name)
		}
	}
	if ($pathNames.Count -le 1) {
		return
	}

	$preferredName = if ($pathNames.Contains("Path")) { "Path" } else { $pathNames[0] }
	$pathValue = [string]$variables[$preferredName]
	if ([string]::IsNullOrWhiteSpace($pathValue)) {
		foreach ($name in $pathNames) {
			$value = [string]$variables[$name]
			if (![string]::IsNullOrWhiteSpace($value)) {
				$pathValue = $value
				break
			}
		}
	}
	foreach ($name in $pathNames) {
		if (!$name.Equals("Path", [System.StringComparison]::Ordinal)) {
			[Environment]::SetEnvironmentVariable($name, $null, "Process")
		}
	}
	[Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")
}

function Normalize-OfxGgmlPathText {
	param([string]$PathText)
	if ([string]::IsNullOrWhiteSpace($PathText)) {
		return ""
	}
	return $PathText.Trim().Trim('"')
}

function Resolve-OfxGgmlFirstFile {
	param([string[]]$Candidates)
	foreach ($candidate in $Candidates) {
		if (![string]::IsNullOrWhiteSpace($candidate) -and
			(Test-Path -LiteralPath $candidate -PathType Leaf)) {
			return (Resolve-Path -LiteralPath $candidate).Path
		}
	}
	return ""
}

function Find-OfxGgmlFirstModel {
	param([string[]]$Directories)
	foreach ($directory in $Directories) {
		if (!(Test-Path -LiteralPath $directory -PathType Container)) {
			continue
		}
		$model = Get-ChildItem -LiteralPath $directory -Filter "*.gguf" -File -ErrorAction SilentlyContinue |
			Sort-Object Name |
			Select-Object -First 1
		if ($model) {
			return $model.FullName
		}
	}
	return ""
}

function Get-OfxGgmlModelRoleHint {
	param([string]$Name)
	$lower = $Name.ToLowerInvariant()
	if ($lower -match "embed|embedding|bge|e5|gte|nomic|jina") {
		return "embedding"
	}
	return "text"
}

function Find-OfxGgmlFirstModelByRole {
	param(
		[string[]]$Directories,
		[string[]]$PreferredRoles
	)
	if ($null -eq $PreferredRoles -or $PreferredRoles.Count -eq 0) {
		$PreferredRoles = @("text")
	}
	$preferredSet = New-Object System.Collections.Generic.HashSet[string]
	foreach ($role in $PreferredRoles) {
		$normalized = ($role.Trim()).ToLowerInvariant()
		if (![string]::IsNullOrWhiteSpace($normalized)) {
			$null = $preferredSet.Add($normalized)
		}
	}
	if ($preferredSet.Count -eq 0) {
		return Find-OfxGgmlFirstModel $Directories
	}

	foreach ($directory in $Directories) {
		if (!(Test-Path -LiteralPath $directory -PathType Container)) {
			continue
		}
		$models = Get-ChildItem -LiteralPath $directory -Filter "*.gguf" -File -ErrorAction SilentlyContinue |
			Sort-Object Name
		foreach ($preferred in $preferredSet) {
			foreach ($model in $models) {
				if (Get-OfxGgmlModelRoleHint $model.Name -eq $preferred) {
					return $model.FullName
				}
			}
		}
	}
	return ""
}

function Get-OfxGgmlModelSearchDirectories {
	param(
		[string]$AddonRoot,
		[string]$ExampleRoot = "",
		[string[]]$ExtraExampleNames = @()
	)

	$directories = New-Object System.Collections.Generic.List[string]
	if (![string]::IsNullOrWhiteSpace($ExampleRoot)) {
		$directories.Add((Join-Path $ExampleRoot "bin\data"))
		$directories.Add((Join-Path $ExampleRoot "bin\data\models"))
		$directories.Add((Join-Path $ExampleRoot "models"))
	}
	foreach ($exampleName in $ExtraExampleNames) {
		$extraRoot = Join-Path $AddonRoot $exampleName
		$directories.Add((Join-Path $extraRoot "bin\data"))
		$directories.Add((Join-Path $extraRoot "bin\data\models"))
		$directories.Add((Join-Path $extraRoot "models"))
	}
	$directories.Add((Join-Path $AddonRoot "models"))
	$directories.Add((Join-Path (Split-Path -Parent $AddonRoot) "models"))
	return @($directories)
}

function Find-OfxGgmlLlamaCli {
	param(
		[string]$AddonRoot,
		[string]$ExampleRoot
	)

	$llamaNames = if ($IsWindows -or $env:OS -eq "Windows_NT") {
		@("llama-cli.exe", "main.exe", "llama.exe")
	} else {
		@("llama-cli", "main", "llama")
	}
	$searchRoots = @(
		$AddonRoot,
		$ExampleRoot,
		(Join-Path $ExampleRoot "bin"),
		(Join-Path $ExampleRoot "bin\data")
	)
	$llamaDirs = @(
		"",
		"bin",
		"data",
		"data\bin",
		"tools",
		"libs\llama\bin",
		"libs\llama.cpp\build\bin",
		"libs\llama.cpp\build\bin\Release",
		"libs\llama.cpp\build\bin\Debug"
	)
	$candidates = foreach ($root in $searchRoots) {
		foreach ($dir in $llamaDirs) {
			foreach ($name in $llamaNames) {
				Join-Path (Join-Path $root $dir) $name
			}
		}
	}
	return Resolve-OfxGgmlFirstFile $candidates
}

function Test-OfxGgmlLocalServerUrl {
	param([string]$Url)
	try {
		$uri = [Uri]$Url
		if ($uri.Host -notin @("127.0.0.1", "localhost", "::1")) {
			return $true
		}
		$healthUrl = $Url.TrimEnd("/") + "/health"
		$response = Invoke-WebRequest -Uri $healthUrl -UseBasicParsing -TimeoutSec 1 -ErrorAction Stop
		return ($response.StatusCode -ge 200 -and $response.StatusCode -lt 500)
	} catch {
		return $false
	}
}

function Get-OfxGgmlServerEndpoint {
	param([string]$ServerUrl)
	$uri = [Uri]$ServerUrl
	$port = if ($uri.IsDefaultPort) {
		if ($uri.Scheme -ieq "https") { 443 } else { 80 }
	} else {
		$uri.Port
	}
	$hostName = if ([string]::IsNullOrWhiteSpace($uri.Host)) { "127.0.0.1" } else { $uri.Host }
	return [pscustomobject]@{
		HostName = $hostName
		Port = $port
	}
}

function Start-OfxGgmlBundledLlamaServerIfNeeded {
	param(
		[string]$ScriptRoot,
		[string]$AddonRoot,
		[string]$ServerUrl,
		[string]$Model,
		[string]$LogDir,
		[string]$MissingModelWarning,
		[string]$StartMessage,
		[switch]$NoAutoServer,
		[switch]$Embeddings
	)

	if ($NoAutoServer -or (Test-OfxGgmlLocalServerUrl $ServerUrl)) {
		return
	}
	if ([string]::IsNullOrWhiteSpace($Model)) {
		Write-Warning $MissingModelWarning
		return
	}

	$endpoint = Get-OfxGgmlServerEndpoint $ServerUrl
	Write-OfxGgmlStep $StartMessage
	$args = @(
		"-ModelPath", $Model,
		"-HostName", $endpoint.HostName,
		"-Port", $endpoint.Port,
		"-Detached",
		"-LogDir", $LogDir
	)
	if ($Embeddings) {
		$args += "-Embeddings"
	}
	& (Join-Path $ScriptRoot "start-llama-server.ps1") @args
}
