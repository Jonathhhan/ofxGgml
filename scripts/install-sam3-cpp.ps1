param(
	[string]$Repo = "https://github.com/PABannier/sam3.cpp.git",
	[string]$Ref = "main",
	[switch]$Force
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

function Patch-Sam3CudaSupport {
	param([string]$SourceDir)

	$cmakePath = Join-Path $SourceDir "CMakeLists.txt"
	$cppPath = Join-Path $SourceDir "sam3.cpp"

	if (!(Test-Path -LiteralPath $cmakePath) -or !(Test-Path -LiteralPath $cppPath)) {
		throw "sam3.cpp checkout is missing CMakeLists.txt or sam3.cpp"
	}

	$cmake = Get-Content -Raw -LiteralPath $cmakePath
	if ($cmake -notmatch "SAM3_CUDA") {
		$cmake = [regex]::Replace(
			$cmake,
			"option\(SAM3_METAL `"Enable Metal backend`" ON\)",
			"option(SAM3_METAL `"Enable Metal backend`" ON)`noption(SAM3_CUDA `"Enable CUDA backend`" OFF)",
			1)
		$cmake = [regex]::Replace(
			$cmake,
			"if\(APPLE AND SAM3_METAL\)([\s\S]*?)endif\(\)",
			"if(APPLE AND SAM3_METAL)`$1endif()`nif(SAM3_CUDA)`n    set(GGML_CUDA ON CACHE BOOL `"`" FORCE)`nendif()",
			1)
		$cmake = [regex]::Replace(
			$cmake,
			"target_compile_features\(sam3 PUBLIC cxx_std_14\)",
			"target_compile_features(sam3 PUBLIC cxx_std_14)`nif(SAM3_CUDA)`n    target_compile_definitions(sam3 PUBLIC GGML_USE_CUDA)`nendif()",
			1)
		Set-Content -LiteralPath $cmakePath -Value $cmake -Encoding UTF8
		Write-Step "Patched sam3.cpp CMake CUDA option"
	}

	$cpp = Get-Content -Raw -LiteralPath $cppPath
	if ($cpp -notmatch "ggml-cuda\.h") {
		$cpp = [regex]::Replace(
			$cpp,
			"#include `"ggml\.h`"",
			"#include `"ggml.h`"`n#ifdef GGML_USE_CUDA`n#include `"ggml-cuda.h`"`n#endif",
			1)
	}
	if ($cpp -notmatch "ggml_backend_cuda_init") {
		$cudaBlock =
			"#ifdef GGML_USE_CUDA`n" +
			"    if (params.use_gpu) {`n" +
			"        SAM3_LOG(1, `"%s: using CUDA backend\n`", __func__);`n" +
			"        model->backend = ggml_backend_cuda_init(0);`n" +
			"        if (!model->backend) {`n" +
			"            SAM3_LOG(1, `"%s: failed to init CUDA backend; falling back\n`", __func__);`n" +
			"        }`n" +
			"    }`n" +
			"#endif`n"
		$metalBackendPattern =
			"#ifdef GGML_USE_METAL\r?\n\s*if \(params\.use_gpu\) \{[\s\S]*?ggml_backend_metal_init\(\);[\s\S]*?\}\r?\n#endif"
		if ($cpp -match $metalBackendPattern) {
			$cpp = [regex]::Replace(
				$cpp,
				$metalBackendPattern,
				$cudaBlock + '$0',
				1)
		} else {
			$backendFallbackPattern = "if \(!model->backend\)"
			if ($cpp -notmatch $backendFallbackPattern) {
				throw "Could not locate sam3.cpp backend initialization block to patch CUDA support."
			}
			$cpp = [regex]::Replace($cpp, $backendFallbackPattern, $cudaBlock + '$0', 1)
		}
		Set-Content -LiteralPath $cppPath -Value $cpp -Encoding UTF8
		Write-Step "Patched sam3.cpp CUDA backend initialization"
	}
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonRoot = Resolve-Path (Join-Path $scriptRoot "..")
$destDir = Join-Path $addonRoot "libs\sam3.cpp"

if (!(Get-Command git -ErrorAction SilentlyContinue)) {
	throw "git is required to install sam3.cpp."
}

if (Test-Path (Join-Path $destDir ".git")) {
	Write-Step "Updating existing sam3.cpp checkout"
	Invoke-CheckedNative "git fetch sam3.cpp" { git -C $destDir fetch --tags origin }
} else {
	if (Test-Path -LiteralPath $destDir) {
		$children = Get-ChildItem -LiteralPath $destDir -Force
		if ($children.Count -gt 0 -and !$Force) {
			throw "Refusing to overwrite non-empty directory: $destDir. Re-run with -Force to replace it."
		}
		Remove-Item -LiteralPath $destDir -Recurse -Force
	}
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destDir) | Out-Null
	Write-Step "Cloning sam3.cpp into $destDir"
	Invoke-CheckedNative "git clone sam3.cpp" { git clone --recursive $Repo $destDir }
}

Write-Step "Checking out sam3.cpp ref $Ref"
Invoke-CheckedNative "git checkout sam3.cpp" { git -C $destDir checkout $Ref }
Write-Step "Updating sam3.cpp submodules"
Invoke-CheckedNative "git submodule update sam3.cpp" { git -C $destDir submodule update --init --recursive }

Patch-Sam3CudaSupport -SourceDir $destDir

Write-Step "sam3.cpp is installed"
Write-Host "Source: $destDir"
Write-Host "Ref:    $Ref"
