param(
	[string]$Configuration = "Release",
	[string]$Platform = "x64",
	[string]$Example = "ofxGgmlSimpleExample",
	[switch]$Clean
)

$ErrorActionPreference = "Stop"

function Write-Step {
	param([string]$Message)
	Write-Host "==> $Message"
}

function Test-WindowsHost {
	return !($IsLinux -or $IsMacOS)
}

function Invoke-CheckedNative {
	param(
		[string]$Step,
		[scriptblock]$Command
	)
	& $Command
	if ($LASTEXITCODE -ne 0) {
		throw "$Step failed with exit code $LASTEXITCODE"
	}
}

function Get-StableNameFragment {
	param([string]$Text)
	$sha1 = [System.Security.Cryptography.SHA1]::Create()
	try {
		$bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
		$hash = $sha1.ComputeHash($bytes)
		return [System.BitConverter]::ToString($hash).Replace("-", "")
	} finally {
		$sha1.Dispose()
	}
}

function Invoke-WithNamedMutex {
	param(
		[string]$Name,
		[scriptblock]$Command
	)
	$mutex = New-Object System.Threading.Mutex($false, $Name)
	$locked = $false
	try {
		$locked = $mutex.WaitOne([TimeSpan]::FromMinutes(30))
		if (!$locked) {
			throw "Timed out waiting for build lock: $Name"
		}
		& $Command
	} finally {
		if ($locked) {
			$mutex.ReleaseMutex()
		}
		$mutex.Dispose()
	}
}

function Get-MsBuild {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path -LiteralPath $vswhere) {
		$installPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
		if ($installPath) {
			$candidate = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
			if (Test-Path -LiteralPath $candidate) {
				return $candidate
			}
		}
	}

	foreach ($version in @("18", "17", "16")) {
		foreach ($edition in @("Community", "Professional", "Enterprise", "BuildTools")) {
			$candidate = "C:\Program Files\Microsoft Visual Studio\$version\$edition\MSBuild\Current\Bin\MSBuild.exe"
			if (Test-Path -LiteralPath $candidate) {
				return $candidate
			}
		}
	}
	return ""
}

function Test-GeneratedAddonPath {
	param([string]$Path)
	if ([string]::IsNullOrWhiteSpace($Path)) {
		return $false
	}

	$normalized = $Path -replace "/", "\"
	return ($normalized -match '(^|\\)libs\\ggml\\\.source(\\|$)') -or
		($normalized -match '(^|\\)libs\\ggml\\build[^\\]*(\\|$)') -or
		($normalized -match '(^|\\)libs\\sam3\.cpp\\build[^\\]*(\\|$)') -or
		($normalized -match '(^|\\)libs\\sam3\.cpp\\(ggml|examples|media|scripts|tests)(\\|$)') -or
		($normalized -match '(^|\\)libs\\sam3\.cpp\\sam3\.cpp$')
}

function Repair-VisualStudioProjectFile {
	param(
		[string]$Path,
		[string[]]$AddonDefines = @()
	)
	if (!(Test-Path -LiteralPath $Path)) {
		return
	}

	[xml]$doc = Get-Content -LiteralPath $Path -Raw
	$namespace = New-Object System.Xml.XmlNamespaceManager($doc.NameTable)
	$namespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
	$changed = $false

	foreach ($tag in @("ClCompile", "ClInclude", "None", "CustomBuild", "CudaCompile", "Filter")) {
		$nodes = @($doc.SelectNodes("//msb:$tag[@Include]", $namespace))
		foreach ($node in $nodes) {
			if (Test-GeneratedAddonPath $node.Include) {
				[void]$node.ParentNode.RemoveChild($node)
				$changed = $true
			}
		}
	}

	$includeNodes = @($doc.SelectNodes("//msb:AdditionalIncludeDirectories", $namespace))
	foreach ($node in $includeNodes) {
		$parts = @($node.InnerText -split ";" | Where-Object { $_ -and !(Test-GeneratedAddonPath $_) })
		$updated = $parts -join ";"
		if ($updated -ne $node.InnerText) {
			$node.InnerText = $updated
			$changed = $true
		}
	}

	if ($AddonDefines.Count -gt 0 -and $Path.EndsWith(".vcxproj", [System.StringComparison]::OrdinalIgnoreCase)) {
		$optionNodes = @($doc.SelectNodes("//msb:ClCompile/msb:AdditionalOptions", $namespace))
		foreach ($node in $optionNodes) {
			$options = @($node.InnerText -split "\s+" | Where-Object { $_ })
			foreach ($define in $AddonDefines) {
				$option = "-D$define"
				if ($options -notcontains $option) {
					$options += $option
					$changed = $true
				}
			}
			$valuedDefines = @{}
			foreach ($option in $options) {
				if ($option -match '^-D([^=\s]+)=') {
					$valuedDefines[$matches[1]] = $true
				}
			}
			$cleanOptions = @($options | Where-Object {
				!($_ -match '^-D([^=\s]+)$' -and $valuedDefines.ContainsKey($matches[1]))
			})
			if ($cleanOptions.Count -ne $options.Count) {
				$changed = $true
			}
			$node.InnerText = ($cleanOptions -join " ")
		}
	}

	if ($changed) {
		$doc.Save($Path)
		Write-Step "Updated generated project metadata in $(Split-Path -Leaf $Path)"
	}
}

function Get-AddonDefines {
	$configPath = Join-Path $addonRoot "addon_config.mk"
	if (!(Test-Path -LiteralPath $configPath)) {
		return @()
	}
	$defines = New-Object System.Collections.Generic.List[string]
	Get-Content -LiteralPath $configPath | ForEach-Object {
		if ($_ -match 'ADDON_CFLAGS\s*\+=\s*-D([A-Za-z0-9_]+(?:=[^\s]+)?)') {
			if (!$defines.Contains($matches[1])) {
				$defines.Add($matches[1])
			}
		}
	}
	return @($defines)
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot "..")
$exampleDir = Join-Path $addonRoot $Example
if (!(Test-Path -LiteralPath $exampleDir)) {
	throw "Example directory not found: $exampleDir"
}

if (Test-WindowsHost) {
	$project = Join-Path $exampleDir "$Example.vcxproj"
	if (!(Test-Path -LiteralPath $project)) {
		throw "Visual Studio project not found: $project. Generate it with the openFrameworks projectGenerator first."
	}
	$addonDefines = Get-AddonDefines
	Repair-VisualStudioProjectFile -Path $project -AddonDefines $addonDefines
	Repair-VisualStudioProjectFile -Path "$project.filters"
	$msbuild = Get-MsBuild
	if ([string]::IsNullOrWhiteSpace($msbuild)) {
		throw "MSBuild.exe was not found."
	}

	$target = if ($Clean) { "Rebuild" } else { "Build" }
	Write-Step "Building $Example $Configuration $Platform with MSBuild"
	$lockName = "Local\ofxGgml-msbuild-" + (Get-StableNameFragment $addonRoot.Path)
	Invoke-WithNamedMutex -Name $lockName -Command {
		$exitCode = 0
		for ($attempt = 1; $attempt -le 2; $attempt++) {
			& $msbuild $project /t:$target /p:Configuration=$Configuration /p:Platform=$Platform /p:TrackFileAccess=false /p:MultiProcessorCompilation=false /m:1 /nr:false
			$exitCode = $LASTEXITCODE
			if ($exitCode -eq 0) {
				return
			}
			if ($attempt -lt 2) {
				Write-Step "MSBuild failed with exit code $exitCode; retrying once"
			}
		}
		throw "MSBuild $Example failed with exit code $exitCode"
	}
	return
}

$makefile = Join-Path $exampleDir "Makefile"
if (Test-Path -LiteralPath $makefile) {
	$target = if ($Clean) { "clean Release" } else { "Release" }
	Write-Step "Building $Example with make"
	Invoke-CheckedNative "make $Example" {
		make -C $exampleDir $target
	}
	return
}

if ($IsMacOS) {
	$xcodeProject = Get-ChildItem -LiteralPath $exampleDir -Filter "*.xcodeproj" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($xcodeProject) {
		Write-Step "Building $Example $Configuration with xcodebuild"
		Invoke-CheckedNative "xcodebuild $Example" {
			xcodebuild -project $xcodeProject.FullName -configuration $Configuration
		}
		return
	}
}

throw "No supported generated project was found for $Example. Generate the example project with openFrameworks projectGenerator first."
