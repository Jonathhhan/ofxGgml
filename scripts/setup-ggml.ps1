param(
	[string]$Revision = "v0.11.0",
	[string]$Repo = "https://github.com/ggml-org/ggml.git",
	[int]$Jobs = 0,
	# Default behavior when no backend switch is supplied.
	[switch]$Auto,
	[switch]$CpuOnly,
	[switch]$Cuda,
	[switch]$Vulkan,
	[switch]$Metal,
	[switch]$OpenCL,
	[switch]$AllBackends,
	[switch]$Clean,
	[switch]$WithDebug
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$GgmlRoot = Join-Path $Root "libs\ggml"
$Source = Join-Path $GgmlRoot ".source"
$Build = Join-Path $GgmlRoot "build"
$Include = Join-Path $GgmlRoot "include"
$Lib = Join-Path $GgmlRoot "lib"

function Write-Step {
	param([string]$Message)
	Write-Host "==> $Message"
}

function Test-Command {
	param([string]$Name)
	$null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
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

function Clear-DirectoryContents {
	param([string]$Path)
	if (!(Test-Path -LiteralPath $Path)) {
		return
	}
	Get-ChildItem -LiteralPath $Path -Force |
		Where-Object { $_.Name -ne ".gitkeep" } |
		Remove-Item -Recurse -Force
}

function Test-CMakeGenerator {
	param([string]$Generator)
	$help = & cmake --help 2>$null
	return ($help -join "`n") -match [regex]::Escape($Generator)
}

function Get-VisualStudioGenerator {
	$generators = @(
		"Visual Studio 18 2026",
		"Visual Studio 17 2022",
		"Visual Studio 16 2019"
	)
	foreach ($generator in $generators) {
		if (Test-CMakeGenerator $generator) {
			return $generator
		}
	}
	return $null
}

function Test-CudaAvailable {
	if ($env:CUDA_PATH -and (Test-Path (Join-Path $env:CUDA_PATH "bin\nvcc.exe"))) {
		return $true
	}
	return Test-Command "nvcc"
}

function Test-VulkanAvailable {
	if ($env:VULKAN_SDK -and (Test-Path (Join-Path $env:VULKAN_SDK "Lib\vulkan-1.lib"))) {
		return $true
	}
	return (Test-Command "glslc")
}

function Get-PlatformSection {
	if ($IsMacOS) {
		return @{ Section = "osx"; Extension = ".a" }
	}
	if ($IsLinux) {
		return @{ Section = "linux64"; Extension = ".a" }
	}
	return @{ Section = "vs"; Extension = ".lib" }
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
	$configPath = Join-Path $Root "addon_config.mk"
	if (!(Test-Path -LiteralPath $configPath)) {
		Write-Step "addon_config.mk not found; skipping library update"
		return
	}

	$platform = Get-PlatformSection
	$section = $platform.Section
	$extension = $platform.Extension
	$startMarker = "# @OFXGGML_LIBS_START $section"
	$endMarker = "# @OFXGGML_LIBS_END $section"

	$order = @(
		"ggml$extension",
		"libggml$extension",
		"ggml-base$extension",
		"libggml-base$extension",
		"ggml-cpu$extension",
		"libggml-cpu$extension",
		"ggml-cuda$extension",
		"libggml-cuda$extension",
		"ggml-vulkan$extension",
		"libggml-vulkan$extension",
		"ggml-metal$extension",
		"libggml-metal$extension",
		"ggml-opencl$extension",
		"libggml-opencl$extension",
		"ggml-sycl$extension",
		"libggml-sycl$extension"
	)

	$found = @{}
	Get-ChildItem -LiteralPath $Lib -Recurse -File -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -like "ggml*$extension" -or $_.Name -like "libggml*$extension" } |
		ForEach-Object {
			if (!$found.ContainsKey($_.Name)) {
				$found[$_.Name] = $_.FullName
			}
		}

	$orderedPaths = New-Object System.Collections.Generic.List[string]
	foreach ($name in $order) {
		if ($found.ContainsKey($name)) {
			$orderedPaths.Add($found[$name])
			$found.Remove($name)
		}
	}
	foreach ($name in ($found.Keys | Sort-Object)) {
		$orderedPaths.Add($found[$name])
	}

	$lines = New-Object System.Collections.Generic.List[string]
	$lines.Add("`t$startMarker")
	foreach ($path in $orderedPaths) {
		$lines.Add("`tADDON_LIBS += $(Convert-ToAddonPath $path)")
	}

	if ($section -eq "vs") {
		if (($orderedPaths | Where-Object { (Split-Path $_ -Leaf) -eq "ggml-cuda.lib" }) -and $env:CUDA_PATH) {
			$lines.Add("`tADDON_LIBS += `$`(CUDA_PATH`)/lib/x64/cublas.lib")
			$lines.Add("`tADDON_LIBS += `$`(CUDA_PATH`)/lib/x64/cudart.lib")
			$lines.Add("`tADDON_LIBS += `$`(CUDA_PATH`)/lib/x64/cuda.lib")
		}
		if (($orderedPaths | Where-Object { (Split-Path $_ -Leaf) -eq "ggml-vulkan.lib" }) -and $env:VULKAN_SDK) {
			$lines.Add("`tADDON_LIBS += `$`(VULKAN_SDK`)/Lib/vulkan-1.lib")
		}
	}

	$lines.Add("`t$endMarker")

	$content = Get-Content -LiteralPath $configPath
	$output = New-Object System.Collections.Generic.List[string]
	$inside = $false
	$replaced = $false
	foreach ($line in $content) {
		if ($line.Contains($startMarker)) {
			foreach ($replacement in $lines) {
				$output.Add($replacement)
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

	if (!$replaced) {
		Write-Step "Markers for $section were not found in addon_config.mk"
		return
	}

	Set-Content -LiteralPath $configPath -Value $output
	Write-Step "Updated addon_config.mk [$section] with $($orderedPaths.Count) ggml libraries"
}

if ($Jobs -le 0) {
	$Jobs = [Math]::Max(1, [Environment]::ProcessorCount)
}

if ($CpuOnly -and ($Cuda -or $Vulkan -or $Metal -or $OpenCL -or $AllBackends)) {
	throw "-CpuOnly cannot be combined with backend switches"
}

$explicitBackendRequested = $CpuOnly -or $Cuda -or $Vulkan -or $Metal -or $OpenCL -or $AllBackends
$autoRequested = $Auto -or !$explicitBackendRequested

if ($AllBackends) {
	$Cuda = $true
	$Vulkan = $true
	$OpenCL = $true
	if ($IsMacOS) {
		$Metal = $true
	}
}

if ($autoRequested -and !$CpuOnly) {
	if (Test-CudaAvailable) {
		$Cuda = $true
	}
	if (Test-VulkanAvailable) {
		$Vulkan = $true
	}
	if ($IsMacOS) {
		$Metal = $true
	}
}

New-Item -ItemType Directory -Path $GgmlRoot,$Include,$Lib -Force | Out-Null

if ($Clean) {
	Write-Step "Cleaning source, build, include, and lib outputs"
	Remove-Item -LiteralPath $Source,$Build -Recurse -Force -ErrorAction SilentlyContinue
	Clear-DirectoryContents $Include
	Clear-DirectoryContents $Lib
}

if (!(Test-Path -LiteralPath $Source)) {
	Write-Step "Cloning ggml $Revision"
	Invoke-CheckedNative "git clone ggml" { git clone --depth 1 --branch $Revision $Repo $Source }
} else {
	Write-Step "Fetching ggml $Revision"
	Invoke-CheckedNative "git fetch ggml" { git -C $Source fetch --depth 1 origin $Revision }
	Invoke-CheckedNative "git checkout ggml" { git -C $Source checkout --detach FETCH_HEAD }
}

$commit = git -C $Source rev-parse --short HEAD
if ($LASTEXITCODE -ne 0) {
	throw "git rev-parse ggml failed with exit code $LASTEXITCODE"
}
Write-Step "Using ggml commit $commit"

if (Test-Path -LiteralPath $Build) {
	Remove-Item -LiteralPath $Build -Recurse -Force
}

$configureArgs = @(
	"-S", $Source,
	"-B", $Build,
	"-DBUILD_SHARED_LIBS=OFF",
	"-DGGML_BUILD_TESTS=OFF",
	"-DGGML_BUILD_EXAMPLES=OFF",
	"-DGGML_NATIVE=ON",
	"-DGGML_STATIC=ON",
	"-DGGML_BACKEND_DL=OFF",
	"-DCMAKE_C_FLAGS=-DGGML_MAX_NAME=128",
	"-DCMAKE_CXX_FLAGS=-DGGML_MAX_NAME=128",
	"-DCMAKE_CUDA_FLAGS=-DGGML_MAX_NAME=128",
	"-DGGML_CUDA=$(if ($Cuda) { 'ON' } else { 'OFF' })",
	"-DGGML_VULKAN=$(if ($Vulkan) { 'ON' } else { 'OFF' })",
	"-DGGML_METAL=$(if ($Metal) { 'ON' } else { 'OFF' })",
	"-DGGML_OPENCL=$(if ($OpenCL) { 'ON' } else { 'OFF' })"
)

if (!$IsLinux -and !$IsMacOS) {
	$generator = Get-VisualStudioGenerator
	if ($generator) {
		$configureArgs = @("-G", $generator) + $configureArgs
		if ($Cuda -and $env:CUDA_PATH) {
			$configureArgs = @("-T", "host=x64,cuda=$env:CUDA_PATH") + $configureArgs
		}
		Write-Step "Using CMake generator: $generator"
	}
}

Write-Step "Configuring ggml backends: CPU=ON CUDA=$(if ($Cuda) { 'ON' } else { 'OFF' }) Vulkan=$(if ($Vulkan) { 'ON' } else { 'OFF' }) Metal=$(if ($Metal) { 'ON' } else { 'OFF' }) OpenCL=$(if ($OpenCL) { 'ON' } else { 'OFF' })"
Invoke-CheckedNative "cmake configure ggml" { cmake @configureArgs }

Write-Step "Building ggml Release with $Jobs jobs"
Invoke-CheckedNative "cmake build ggml Release" { cmake --build $Build --config Release -j $Jobs }

if ($WithDebug) {
	Write-Step "Building ggml Debug with $Jobs jobs"
	Invoke-CheckedNative "cmake build ggml Debug" { cmake --build $Build --config Debug -j $Jobs }
}

Write-Step "Exporting headers and libraries"
Clear-DirectoryContents $Include
Clear-DirectoryContents $Lib

Copy-Item -Path (Join-Path $Source "include\*") -Destination $Include -Recurse -Force
New-Item -ItemType File -Path (Join-Path $Include ".gitkeep") -Force | Out-Null

$libraryPatterns = @("ggml*.lib", "libggml*.a", "libggml*.dylib")
foreach ($pattern in $libraryPatterns) {
	Get-ChildItem -LiteralPath $Build -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -notmatch "\\Debug\\" } |
		Sort-Object Name |
		ForEach-Object {
			Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Lib $_.Name) -Force
		}
}

if ($WithDebug) {
	$releaseDir = Join-Path $Lib "Release"
	$debugDir = Join-Path $Lib "Debug"
	New-Item -ItemType Directory -Path $releaseDir,$debugDir -Force | Out-Null
	Get-ChildItem -LiteralPath $Lib -File |
		Where-Object { $_.Name -like "ggml*.lib" -or $_.Name -like "libggml*.a" } |
		ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $releaseDir $_.Name) -Force }
	foreach ($pattern in $libraryPatterns) {
		Get-ChildItem -LiteralPath $Build -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue |
			Where-Object { $_.FullName -match "\\Debug\\" } |
			Sort-Object Name |
			ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $debugDir $_.Name) -Force }
	}
}

New-Item -ItemType File -Path (Join-Path $Lib ".gitkeep") -Force | Out-Null

Update-AddonConfig

Write-Step "Done. ggml $Revision installed under $GgmlRoot"
