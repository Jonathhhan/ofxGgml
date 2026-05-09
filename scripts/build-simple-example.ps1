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
	return ($normalized -match '(^|\\)libs\\ggml\\(\.source|build)') -or
		($normalized -match '(^|\\)libs\\sam3\.cpp\\(build|ggml|examples|media|scripts|tests)') -or
		($normalized -match '(^|\\)libs\\sam3\.cpp\\sam3\.cpp$')
}

function Repair-VisualStudioProjectFile {
	param([string]$Path)
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

	if ($changed) {
		$doc.Save($Path)
		Write-Step "Removed generated dependency paths from $(Split-Path -Leaf $Path)"
	}
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
	Repair-VisualStudioProjectFile $project
	Repair-VisualStudioProjectFile "$project.filters"
	$msbuild = Get-MsBuild
	if ([string]::IsNullOrWhiteSpace($msbuild)) {
		throw "MSBuild.exe was not found."
	}

	$target = if ($Clean) { "Rebuild" } else { "Build" }
	Write-Step "Building $Example $Configuration $Platform with MSBuild"
	Invoke-CheckedNative "MSBuild $Example" {
		& $msbuild $project /t:$target /p:Configuration=$Configuration /p:Platform=$Platform /m
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
