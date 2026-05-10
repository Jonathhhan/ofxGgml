param(
	[string]$Revision = "master",
	[string]$Repo = "https://github.com/ggml-org/llama.cpp.git",
	[string]$SourceDir = "",
	[string]$BuildDir = "",
	[string]$InstallDir = "",
	[int]$Jobs = 0,
	[string]$CudaArchitectures = "",
	[switch]$Auto,
	[switch]$CpuOnly,
	[switch]$Cuda,
	[switch]$Vulkan,
	[switch]$Metal,
	[switch]$OpenCL,
	[switch]$WithCompletionTool,
	[switch]$Clean,
	[switch]$Refetch,
	[switch]$StopRunningRuntime,
	[switch]$DryRun
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot "..")
$jobsSpecified = $PSBoundParameters.ContainsKey("Jobs")
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

function Normalize-WindowsPathEnvironment {
	if ($IsLinux -or $IsMacOS) {
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

function Invoke-GitProbe {
	param([string[]]$Arguments)
	$previousErrorActionPreference = $ErrorActionPreference
	try {
		$ErrorActionPreference = "Continue"
		$output = & git @Arguments 2>$null
		return @{
			ExitCode = $LASTEXITCODE
			Output = (@($output) -join "`n").Trim()
		}
	} finally {
		$ErrorActionPreference = $previousErrorActionPreference
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
	$tag = Invoke-GitProbe @("-C", $Path, "describe", "--tags", "--exact-match", "HEAD")
	if ($tag.ExitCode -eq 0 -and $tag.Output -eq $Revision) {
		return $true
	}
	$branch = Invoke-GitProbe @("-C", $Path, "rev-parse", "--abbrev-ref", "HEAD")
	if ($branch.ExitCode -eq 0 -and $branch.Output -eq $Revision) {
		return $true
	}
	$commit = Invoke-GitProbe @("-C", $Path, "rev-parse", "HEAD")
	return ($commit.ExitCode -eq 0 -and $commit.Output.StartsWith($Revision, [System.StringComparison]::OrdinalIgnoreCase))
}

function Test-CudaAvailable {
	if ($env:CUDA_PATH -and (Test-Path (Join-Path $env:CUDA_PATH "bin\nvcc.exe"))) {
		return $true
	}
	return (Test-Command "nvcc") -or (Test-Command "nvidia-smi")
}

function Get-DetectedCudaArchitectures {
	if (!(Test-Command "nvidia-smi")) {
		return ""
	}
	$previousErrorActionPreference = $ErrorActionPreference
	try {
		$ErrorActionPreference = "Continue"
		$computeCaps = & nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>$null
		if ($LASTEXITCODE -ne 0) {
			return ""
		}
		$architectures = New-Object System.Collections.Generic.List[string]
		foreach ($computeCap in @($computeCaps)) {
			$architecture = ($computeCap.Trim() -replace '[^0-9]', '')
			if (![string]::IsNullOrWhiteSpace($architecture) -and !$architectures.Contains($architecture)) {
				$architectures.Add($architecture)
			}
		}
		return ($architectures -join ";")
	} finally {
		$ErrorActionPreference = $previousErrorActionPreference
	}
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
		"-DLLAMA_BUILD_EXAMPLES=ON",
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
	if ($Cuda) {
		$architectures = $CudaArchitectures
		if ([string]::IsNullOrWhiteSpace($architectures)) {
			$architectures = Get-DetectedCudaArchitectures
		}
		if (![string]::IsNullOrWhiteSpace($architectures)) {
			$args += "-DCMAKE_CUDA_ARCHITECTURES=$architectures"
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

function Clear-InstallDirectory {
	if (!(Test-Path -LiteralPath $InstallDir)) {
		return
	}
	foreach ($entry in Get-ChildItem -LiteralPath $InstallDir -Force -ErrorAction SilentlyContinue) {
		if ($entry.Name -eq ".gitkeep") {
			continue
		}
		try {
			Remove-Item -LiteralPath $entry.FullName -Recurse -Force -ErrorAction Stop
		} catch {
			$hint = Get-RuntimeProcessHint
			throw "Could not update llama.cpp runtime install because '$($entry.FullName)' is locked or access was denied. $hint"
		}
	}
}

function Get-RuntimeProcesses {
	$processNames = @(
		"llama-server",
		"llama-cli",
		"llama-embedding",
		"ofxGgmlTextExample",
		"ofxGgmlChatExample",
		"ofxGgmlEmbeddingExample"
	)
	$addonPath = (Resolve-Path -LiteralPath $addonRoot).Path
	return @(Get-Process -Name $processNames -ErrorAction SilentlyContinue |
		Where-Object {
			$processPath = ""
			try {
				$processPath = $_.Path
			} catch {
				$processPath = ""
			}
			[string]::IsNullOrWhiteSpace($processPath) -or
				$processPath.StartsWith($addonPath, [System.StringComparison]::OrdinalIgnoreCase)
		} |
		Sort-Object ProcessName,Id)
}

function Get-RuntimeProcessHint {
	$running = @(Get-RuntimeProcesses |
		ForEach-Object { "$($_.ProcessName)($($_.Id))" })
	if ($running.Count -gt 0) {
		return "Stop these running processes and rerun the build, or pass -StopRunningRuntime: $($running -join ', ')."
	}
	return "Stop any running llama-server/example process that may be using files under libs\llama\bin, then rerun the build."
}

function Stop-RuntimeProcesses {
	$running = @(Get-RuntimeProcesses)
	if ($running.Count -eq 0) {
		return
	}
	$labels = @($running | ForEach-Object { "$($_.ProcessName)($($_.Id))" })
	Write-Step "Stopping running llama.cpp runtime processes: $($labels -join ', ')"
	if ($DryRun) {
		return
	}
	foreach ($process in $running) {
		Stop-Process -Id $process.Id -Force -ErrorAction Stop
	}
	Start-Sleep -Milliseconds 500
}

function Get-BuiltRuntimeFiles {
	$libraryExtensions = if ($IsLinux) { @(".so") } elseif ($IsMacOS) { @(".dylib") } else { @(".dll") }
	$executableNames = @("llama-server", "llama-cli", "llama-embedding")
	if ($WithCompletionTool) {
		$executableNames += "llama-completion"
	}
	if (!$IsLinux -and !$IsMacOS) {
		$executableNames = $executableNames | ForEach-Object { "$_.exe" }
	}
	$runtimeDirs = @(
		(Join-Path $BuildDir "bin\Release"),
		(Join-Path $BuildDir "bin")
	)
	$files = New-Object System.Collections.Generic.List[System.IO.FileInfo]
	$seen = @{}
	foreach ($runtimeDir in $runtimeDirs) {
		if (!(Test-Path -LiteralPath $runtimeDir -PathType Container)) {
			continue
		}
		Get-ChildItem -LiteralPath $runtimeDir -File -ErrorAction SilentlyContinue |
			Where-Object { ($libraryExtensions -contains $_.Extension) -or ($executableNames -contains $_.Name) } |
			ForEach-Object {
				if (!$seen.ContainsKey($_.FullName)) {
					$seen[$_.FullName] = $true
					$files.Add($_)
				}
			}
	}
	return @($files)
}

Normalize-WindowsPathEnvironment

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
if ($Cuda -and !$jobsSpecified -and !$IsLinux -and !$IsMacOS) {
	$Jobs = 1
}

if ($Clean) {
	Write-Step "Cleaning llama.cpp source/build/install outputs"
	if ($StopRunningRuntime) {
		Stop-RuntimeProcesses
	}
	Remove-Item -LiteralPath $SourceDir,$BuildDir -Recurse -Force -ErrorAction SilentlyContinue
	Clear-InstallDirectory
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

foreach ($target in @("llama-server", "llama-cli", "llama-embedding")) {
	Write-Step "Building required target $target"
	Invoke-CheckedNative "cmake build $target" "cmake" @("--build", $BuildDir, "--config", "Release", "--target", $target, "--parallel", $Jobs.ToString())
}
if ($WithCompletionTool) {
	Write-Step "Building optional target llama-completion"
	Invoke-CheckedNative "cmake build llama-completion" "cmake" @("--build", $BuildDir, "--config", "Release", "--target", "llama-completion", "--parallel", $Jobs.ToString())
}

if (!$DryRun) {
	$server = Find-BuiltExecutable "llama-server"
	$cli = Find-BuiltExecutable "llama-cli"
	$embedding = Find-BuiltExecutable "llama-embedding"
	if ([string]::IsNullOrWhiteSpace($server)) {
		throw "Build finished, but llama-server was not found under $BuildDir."
	}
	if ([string]::IsNullOrWhiteSpace($cli)) {
		throw "Build finished, but llama-cli was not found under $BuildDir."
	}
	if ([string]::IsNullOrWhiteSpace($embedding)) {
		throw "Build finished, but llama-embedding was not found under $BuildDir."
	}

	Write-Step "Installing llama.cpp tools into $InstallDir"
	if ($StopRunningRuntime) {
		Stop-RuntimeProcesses
	}
	Clear-InstallDirectory
	foreach ($file in Get-BuiltRuntimeFiles) {
		try {
			Copy-Item -LiteralPath $file.FullName -Destination $InstallDir -Force -ErrorAction Stop
		} catch {
			$hint = Get-RuntimeProcessHint
			throw "Could not copy '$($file.FullName)' into '$InstallDir'. $hint"
		}
	}

	Write-Step "Source and build cache kept under libs\llama.cpp"
}

Write-Step "Done. llama-server runtime installed under $InstallDir"
