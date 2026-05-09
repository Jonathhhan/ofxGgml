param(
	[switch]$Auto,
	[switch]$Cuda,
	[switch]$CpuOnly,
	[string]$Configuration = "Release",
	[string]$CudaArchitectures = "",
	[int]$Jobs = 0,
	[switch]$Clean,
	[switch]$SkipExamples
)

$ErrorActionPreference = "Stop"

function Write-Step {
	param([string]$Message)
	Write-Host "==> $Message"
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

function Get-VisualStudioGenerator {
	$help = & cmake --help
	foreach ($candidate in @("Visual Studio 18 2026", "Visual Studio 17 2022", "Visual Studio 16 2019")) {
		if (($help -join "`n") -match [regex]::Escape($candidate)) {
			return $candidate
		}
	}
	return ""
}

function Get-CudaRoot {
	foreach ($candidate in @($env:CUDA_PATH, $env:CUDAToolkit_ROOT)) {
		if (![string]::IsNullOrWhiteSpace($candidate) -and
			(Test-Path (Join-Path $candidate "bin\nvcc.exe"))) {
			return $candidate
		}
	}
	$nvcc = Get-Command nvcc.exe -ErrorAction SilentlyContinue
	if ($nvcc) {
		return (Resolve-Path (Join-Path (Split-Path -Parent $nvcc.Source) "..")).Path
	}
	return ""
}

function Test-CudaVsIntegration {
	param([string]$CudaRoot)
	$msbuildExt = Join-Path $CudaRoot "extras\visual_studio_integration\MSBuildExtensions"
	return (Get-ChildItem -LiteralPath $msbuildExt -Filter "CUDA *.props" -ErrorAction SilentlyContinue | Select-Object -First 1) -and
		(Get-ChildItem -LiteralPath $msbuildExt -Filter "CUDA *.targets" -ErrorAction SilentlyContinue | Select-Object -First 1)
}

function Get-PlatformSection {
	if ($IsMacOS) {
		return @{ Section = "osx"; Extension = ".a"; Prefix = "lib" }
	}
	if ($IsLinux) {
		return @{ Section = "linux64"; Extension = ".a"; Prefix = "lib" }
	}
	return @{ Section = "vs"; Extension = ".lib"; Prefix = "" }
}

function Convert-ToAddonPath {
	param([string]$Path)
	$relative = Resolve-Path -LiteralPath $Path -Relative
	if ($relative.StartsWith(".\")) {
		$relative = $relative.Substring(2)
	}
	return ($relative -replace "\\", "/")
}

function Update-AddonConfig {
	param([string]$Sam3Library)

	$configPath = Join-Path $addonRoot "addon_config.mk"
	if (!(Test-Path -LiteralPath $configPath)) {
		return
	}

	$platform = Get-PlatformSection
	$startMarker = "# @OFXGGML_SAM3_LIBS_START $($platform.Section)"
	$endMarker = "# @OFXGGML_SAM3_LIBS_END $($platform.Section)"
	$replacement = @(
		"`t$startMarker",
		"`tADDON_CFLAGS += -DOFXGGML_ENABLE_SAM3_ADAPTER",
		"`tADDON_LIBS += $(Convert-ToAddonPath $Sam3Library)",
		"`t$endMarker"
	)

	$content = Get-Content -LiteralPath $configPath
	$output = New-Object System.Collections.Generic.List[string]
	$inside = $false
	$replaced = $false
	foreach ($line in $content) {
		if ($line.Contains($startMarker)) {
			foreach ($newLine in $replacement) {
				$output.Add($newLine)
			}
			$inside = $true
			$replaced = $true
			continue
		}
		if ($inside -and $line.Contains($endMarker)) {
			$inside = $false
			continue
		}
		if (!$inside) {
			$output.Add($line)
		}
	}

	if ($replaced) {
		Set-Content -LiteralPath $configPath -Value $output
		Write-Step "Updated addon_config.mk [$($platform.Section)] with sam3.cpp"
	}
}

if ($Cuda -and $CpuOnly) {
	throw "Use either -Cuda or -CpuOnly, not both."
}
if ($Jobs -le 0) {
	$Jobs = [Math]::Max(1, [Environment]::ProcessorCount)
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot "..")
$sourceDir = Join-Path $addonRoot "libs\sam3.cpp"
$installLibDir = Join-Path $addonRoot "libs\sam3\lib"

if (!(Test-Path (Join-Path $sourceDir "sam3.cpp"))) {
	& (Join-Path $scriptRoot "install-sam3-cpp.ps1")
	if ($LASTEXITCODE -ne 0) {
		throw "install-sam3-cpp.ps1 failed with exit code $LASTEXITCODE."
	}
}

$enableCuda = $Cuda.IsPresent
if (!$Cuda -and !$CpuOnly) {
	$enableCuda = ![string]::IsNullOrWhiteSpace((Get-CudaRoot))
}

$buildDirName = if ($enableCuda) { "build-cuda" } else { "build-cpu" }
$buildDir = Join-Path $sourceDir $buildDirName
if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
	Remove-Item -LiteralPath $buildDir -Recurse -Force
}

$cmakeArgs = @(
	"-S", $sourceDir,
	"-B", $buildDir,
	"-DBUILD_SHARED_LIBS=OFF",
	"-DSAM3_BUILD_EXAMPLES=$(if ($SkipExamples) { 'OFF' } else { 'ON' })",
	"-DSAM3_BUILD_TESTS=OFF",
	"-DSAM3_CUDA=$(if ($enableCuda) { 'ON' } else { 'OFF' })"
)

if (!$IsLinux -and !$IsMacOS) {
	$generator = Get-VisualStudioGenerator
	if ([string]::IsNullOrWhiteSpace($generator)) {
		throw "No supported Visual Studio CMake generator was found."
	}
	$cmakeArgs = @("-G", $generator, "-A", "x64") + $cmakeArgs
}

if ($enableCuda) {
	$cudaRoot = Get-CudaRoot
	if ([string]::IsNullOrWhiteSpace($cudaRoot)) {
		throw "CUDA was requested but nvcc.exe was not found."
	}
	if (!$IsLinux -and !$IsMacOS) {
		if (!(Test-CudaVsIntegration -CudaRoot $cudaRoot)) {
			throw "CUDA was requested but Visual Studio CUDA integration files were not found under $cudaRoot."
		}
		$cmakeArgs = @("-T", "host=x64,cuda=$cudaRoot") + $cmakeArgs
	}
	if (![string]::IsNullOrWhiteSpace($CudaArchitectures)) {
		$cmakeArgs += "-DCMAKE_CUDA_ARCHITECTURES=$CudaArchitectures"
	}
}

Write-Step "Configuring sam3.cpp ($buildDirName)"
Invoke-CheckedNative "cmake configure sam3.cpp" { cmake @cmakeArgs }
Write-Step "Building sam3.cpp $Configuration with $Jobs jobs"
Invoke-CheckedNative "cmake build sam3.cpp" {
	cmake --build $buildDir --config $Configuration --parallel $Jobs
}

$platform = Get-PlatformSection
$libraryName = "$($platform.Prefix)sam3$($platform.Extension)"
$builtLibrary = Get-ChildItem -LiteralPath $buildDir -Recurse -File -Filter $libraryName -ErrorAction SilentlyContinue |
	Where-Object { $_.FullName -notmatch "\\Debug\\" } |
	Select-Object -First 1
if (!$builtLibrary) {
	throw "sam3.cpp build finished but $libraryName was not found under $buildDir"
}

New-Item -ItemType Directory -Path $installLibDir -Force | Out-Null
$installedLibrary = Join-Path $installLibDir $builtLibrary.Name
Copy-Item -LiteralPath $builtLibrary.FullName -Destination $installedLibrary -Force
if (!(Test-Path (Join-Path $installLibDir ".gitkeep"))) {
	Set-Content -LiteralPath (Join-Path $installLibDir ".gitkeep") -Value ""
}
Update-AddonConfig -Sam3Library $installedLibrary

Write-Step "sam3.cpp build complete"
Write-Host "Build:   $buildDir"
Write-Host "Library: $installedLibrary"
Write-Host "CUDA:    $enableCuda"
