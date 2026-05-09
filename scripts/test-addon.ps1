param(
	[string]$Configuration = "Release",
	[string]$BuildDir = "",
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

function Convert-ToCmdArgument {
	param([string]$Value)
	return '"' + ($Value -replace '"', '""') + '"'
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

function Invoke-CheckedCmd {
	param(
		[string]$Step,
		[string]$Command
	)
	& cmd.exe /d /s /c $Command
	if ($LASTEXITCODE -ne 0) {
		throw "$Step failed with exit code $LASTEXITCODE"
	}
}

function Get-VisualStudioDevCmd {
	$candidates = New-Object System.Collections.Generic.List[string]
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path -LiteralPath $vswhere) {
		$installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
		if ($installPath) {
			$candidates.Add((Join-Path $installPath "Common7\Tools\VsDevCmd.bat"))
		}
	}

	foreach ($version in @("18", "17", "16")) {
		foreach ($edition in @("Community", "Professional", "Enterprise", "BuildTools")) {
			$candidates.Add("C:\Program Files\Microsoft Visual Studio\$version\$edition\Common7\Tools\VsDevCmd.bat")
			$candidates.Add("C:\Program Files (x86)\Microsoft Visual Studio\$version\$edition\Common7\Tools\VsDevCmd.bat")
		}
	}

	foreach ($candidate in $candidates) {
		if (Test-Path -LiteralPath $candidate) {
			return $candidate
		}
	}
	return ""
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot "..")
$testsDir = Join-Path $addonRoot "tests"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
	$BuildDir = if (Test-WindowsHost) {
		Join-Path $addonRoot "build\tests-nmake"
	} else {
		Join-Path $addonRoot "build/tests"
	}
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
	Write-Step "Cleaning $BuildDir"
	Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

if (Test-WindowsHost) {
	$vsDevCmd = Get-VisualStudioDevCmd
	if ([string]::IsNullOrWhiteSpace($vsDevCmd)) {
		throw "Visual Studio C++ build tools were not found."
	}

	$configure = "cmake -S $(Convert-ToCmdArgument $testsDir) -B $(Convert-ToCmdArgument $BuildDir) -G $(Convert-ToCmdArgument "NMake Makefiles") -DCMAKE_BUILD_TYPE=$Configuration"
	$build = "cmake --build $(Convert-ToCmdArgument $BuildDir)"
	$test = "ctest --test-dir $(Convert-ToCmdArgument $BuildDir) --output-on-failure"
	$command = "call $(Convert-ToCmdArgument $vsDevCmd) -arch=x64 -host_arch=x64 >nul && $configure && $build && $test"

	Write-Step "Configuring and running addon tests with Visual Studio tools"
	Invoke-CheckedCmd "addon tests" $command
} else {
	Write-Step "Configuring addon tests"
	Invoke-CheckedNative "cmake configure addon tests" {
		cmake -S $testsDir -B $BuildDir -DCMAKE_BUILD_TYPE=$Configuration
	}
	Write-Step "Building addon tests"
	Invoke-CheckedNative "cmake build addon tests" {
		cmake --build $BuildDir --config $Configuration
	}
	Write-Step "Running addon tests"
	Invoke-CheckedNative "ctest addon tests" {
		ctest --test-dir $BuildDir --output-on-failure
	}
}
