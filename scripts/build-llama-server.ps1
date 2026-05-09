param(
	[string]$Revision = "master",
	[string]$Repo = "https://github.com/ggml-org/llama.cpp.git",
	[string]$SourceDir = "",
	[string]$BuildDir = "",
	[string]$InstallDir = "",
	[int]$Jobs = 0,
	[switch]$Auto,
	[switch]$CpuOnly,
	[switch]$Cuda,
	[switch]$Vulkan,
	[switch]$Metal,
	[switch]$OpenCL,
	[switch]$Clean,
	[switch]$Refetch,
	[switch]$DryRun
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot "..")
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
	$SourceDir = Join-Path $addonRoot "libs\llama.cpp\.source"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
	$BuildDir = Join-Path $addonRoot "libs\llama.cpp\build"
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
	$InstallDir = Join-Path $addonRoot "libs\llama\bin"
}
if ($Jobs -le 0) {
	$Jobs = [Math]::Max(1, [Environment]::ProcessorCount)
}

function Write-Step {
	param([string]$Message)
	Write-Host "==> $Message"
}

function Test-Command {
	param([string]$Name)
	return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-CheckedNative {
	param(
		[string]$Step,
		[string]$FilePath,
		[string[]]$Arguments
	)
	if ($DryRun) {
		Write-Host "$FilePath $($Arguments -join ' ')"
		return
	}
	& $FilePath @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "$Step failed with exit code $LASTEXITCODE"
	}
}

function Test-SourceRevisionMatches {
	param(
		[string]$Path,
		[string]$Revision
	)
	if (!(Test-Path -LiteralPath $Path)) {
		return $false
	}
	$tag = git -C $Path describe --tags --exact-match HEAD 2>$null
	if ($LASTEXITCODE -eq 0 -and $tag -eq $Revision) {
		return $true
	}
	$branch = git -C $Path rev-parse --abbrev-ref HEAD 2>$null
	if ($LASTEXITCODE -eq 0 -and $branch -eq $Revision) {
		return $true
	}
	$commit = git -C $Path rev-parse HEAD 2>$null
	return ($LASTEXITCODE -eq 0 -and $commit.StartsWith($Revision, [System.StringComparison]::OrdinalIgnoreCase))
}

function Test-CudaAvailable {
	if ($env:CUDA_PATH -and (Test-Path (Join-Path $env:CUDA_PATH "bin\nvcc.exe"))) {
		return $true
	}
	return (Test-Command "nvcc") -or (Test-Command "nvidia-smi")
}

function Test-VulkanAvailable {
	if ($env:VULKAN_SDK -and (Test-Path $env:VULKAN_SDK)) {
		return $true
	}
	return (Test-Command "glslc") -or (Test-Command "vulkaninfo")
}

function Test-OpenCLAvailable {
	foreach ($root in @($env:OPENCL_ROOT, $env:OpenCL_ROOT, $env:OCL_ROOT, $env:CUDA_PATH)) {
		if (!$root) {
			continue
		}
		if ((Test-Path (Join-Path $root "include\CL\cl.h")) -or
			(Test-Path (Join-Path $root "Include\CL\cl.h"))) {
			return $true
		}
	}
	return (Test-Command "pkg-config")
}

function Get-VisualStudioGenerator {
	$help = cmake --help 2>$null
	foreach ($generator in @("Visual Studio 18 2026", "Visual Studio 17 2022", "Visual Studio 16 2019")) {
		if (($help -join "`n") -match [regex]::Escape($generator)) {
			return $generator
		}
	}
	return ""
}

function Get-CMakeConfigureArgs {
	$args = @(
		"-S", $SourceDir,
		"-B", $BuildDir,
		"-DLLAMA_BUILD_SERVER=ON",
		"-DLLAMA_BUILD_TESTS=OFF",
		"-DLLAMA_BUILD_EXAMPLES=OFF",
		"-DLLAMA_CURL=OFF",
		"-DGGML_CUDA=$(if ($Cuda) { 'ON' } else { 'OFF' })",
		"-DGGML_VULKAN=$(if ($Vulkan) { 'ON' } else { 'OFF' })",
		"-DGGML_METAL=$(if ($Metal) { 'ON' } else { 'OFF' })",
		"-DGGML_OPENCL=$(if ($OpenCL) { 'ON' } else { 'OFF' })"
	)
	if ($IsLinux -or $IsMacOS) {
		$args += "-DCMAKE_BUILD_TYPE=Release"
	} else {
		$generator = Get-VisualStudioGenerator
		if ([string]::IsNullOrWhiteSpace($generator)) {
			throw "No supported Visual Studio CMake generator was found."
		}
		$args = @("-G", $generator, "-A", "x64") + $args
		if ($Cuda) {
			if (!$env:CUDA_PATH) {
				throw "CUDA was requested, but CUDA_PATH is not set."
			}
			$args += "-T"
			$args += "host=x64,cuda=$env:CUDA_PATH"
			$args += "-DCUDAToolkit_ROOT=$env:CUDA_PATH"
		}
	}
	return $args
}

function Find-BuiltExecutable {
	param([string]$Name)
	$names = if ($IsLinux -or $IsMacOS) { @($Name) } else { @("$Name.exe", $Name) }
	foreach ($fileName in $names) {
		foreach ($candidate in @(
			(Join-Path $BuildDir "bin\Release\$fileName"),
			(Join-Path $BuildDir "bin\$fileName"),
			(Join-Path $BuildDir $fileName)
		)) {
			if (Test-Path -LiteralPath $candidate -PathType Leaf) {
				return (Resolve-Path -LiteralPath $candidate).Path
			}
		}
		$match = Get-ChildItem -LiteralPath $BuildDir -Recurse -Filter $fileName -File -ErrorAction SilentlyContinue |
			Select-Object -First 1
		if ($match) {
			return $match.FullName
		}
	}
	return ""
}

if (!(Test-Command "git")) {
	throw "git was not found in PATH."
}
if (!(Test-Command "cmake")) {
	throw "cmake was not found in PATH."
}
if ($CpuOnly -and ($Cuda -or $Vulkan -or $Metal -or $OpenCL)) {
	throw "-CpuOnly cannot be combined with backend switches."
}

$explicitBackendRequested = $CpuOnly -or $Cuda -or $Vulkan -or $Metal -or $OpenCL
$autoRequested = $Auto -or !$explicitBackendRequested
if ($autoRequested -and !$CpuOnly) {
	if (Test-CudaAvailable) {
		$Cuda = $true
	}
	if (($IsLinux -or $IsMacOS) -and (Test-VulkanAvailable)) {
		$Vulkan = $true
	}
	if ($IsMacOS) {
		$Metal = $true
	}
	if (($IsLinux -or $IsMacOS) -and (Test-OpenCLAvailable)) {
		$OpenCL = $true
	}
}
if ($Cuda -and !(Test-CudaAvailable)) {
	throw "CUDA was requested, but CUDA was not found. Install CUDA or use -CpuOnly."
}
if ($Vulkan -and !(Test-VulkanAvailable)) {
	throw "Vulkan was requested, but Vulkan SDK/tools were not found."
}
if ($Metal -and !$IsMacOS) {
	throw "Metal builds are only supported on macOS."
}
if ($OpenCL -and !(Test-OpenCLAvailable)) {
	throw "OpenCL was requested, but OpenCL headers/tools were not found."
}

if ($Clean) {
	Write-Step "Cleaning llama.cpp source/build/install outputs"
	Remove-Item -LiteralPath $SourceDir,$BuildDir -Recurse -Force -ErrorAction SilentlyContinue
	Get-ChildItem -LiteralPath $InstallDir -Force -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -ne ".gitkeep" } |
		Remove-Item -Recurse -Force
}
if ($Refetch) {
	Remove-Item -LiteralPath $SourceDir -Recurse -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Path (Split-Path -Parent $SourceDir),$InstallDir -Force | Out-Null
if (!(Test-Path -LiteralPath $SourceDir)) {
	Write-Step "Cloning llama.cpp $Revision"
	Invoke-CheckedNative "git clone llama.cpp" "git" @("clone", "--depth", "1", "--branch", $Revision, $Repo, $SourceDir)
} elseif (Test-SourceRevisionMatches $SourceDir $Revision) {
	Write-Step "llama.cpp source already at $Revision; skipping fetch"
} else {
	Write-Step "Fetching llama.cpp $Revision"
	Invoke-CheckedNative "git fetch llama.cpp" "git" @("-C", $SourceDir, "fetch", "--depth", "1", "origin", $Revision)
	Invoke-CheckedNative "git checkout llama.cpp" "git" @("-C", $SourceDir, "checkout", "--detach", "FETCH_HEAD")
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
	Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

Write-Step "Configuring llama.cpp: CUDA=$(if ($Cuda) { 'ON' } else { 'OFF' }) Vulkan=$(if ($Vulkan) { 'ON' } else { 'OFF' }) Metal=$(if ($Metal) { 'ON' } else { 'OFF' }) OpenCL=$(if ($OpenCL) { 'ON' } else { 'OFF' })"
Invoke-CheckedNative "cmake configure llama.cpp" "cmake" (Get-CMakeConfigureArgs)

foreach ($target in @("llama-server", "llama-cli")) {
	Write-Step "Building required target $target"
	Invoke-CheckedNative "cmake build $target" "cmake" @("--build", $BuildDir, "--config", "Release", "--target", $target, "--parallel", $Jobs.ToString())
}
foreach ($target in @("llama-completion", "llama-embedding")) {
	Write-Step "Building optional target $target"
	try {
		Invoke-CheckedNative "cmake build $target" "cmake" @("--build", $BuildDir, "--config", "Release", "--target", $target, "--parallel", $Jobs.ToString())
	} catch {
		Write-Warning "$target is not available in this llama.cpp checkout; continuing."
	}
}

if (!$DryRun) {
	$server = Find-BuiltExecutable "llama-server"
	$cli = Find-BuiltExecutable "llama-cli"
	if ([string]::IsNullOrWhiteSpace($server)) {
		throw "Build finished, but llama-server was not found under $BuildDir."
	}
	if ([string]::IsNullOrWhiteSpace($cli)) {
		throw "Build finished, but llama-cli was not found under $BuildDir."
	}

	Write-Step "Installing llama.cpp tools into $InstallDir"
	foreach ($tool in @("llama-server", "llama-cli", "llama-completion", "llama-embedding")) {
		$exe = Find-BuiltExecutable $tool
		if (![string]::IsNullOrWhiteSpace($exe)) {
			Copy-Item -LiteralPath $exe -Destination $InstallDir -Force
		}
	}
	Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Include "*.dll","*.so","*.dylib" -ErrorAction SilentlyContinue |
		ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Force }

	Write-Step "Source and build cache kept under libs\llama.cpp"
}

Write-Step "Done. llama-server runtime installed under $InstallDir"
