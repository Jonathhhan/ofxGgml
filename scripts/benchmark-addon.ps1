param(
    [string]$BuildDir = "build/tests-benchmark",
    [string]$Configuration = "Release",
    [string]$Filter = "[benchmark]~[manual]",
    [switch]$WithOpenFrameworks,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

Write-Host "==> Configuring ofxGgml benchmark suite..."
$cmakeArgs = @(
    "-S", "tests",
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DOFXGGML_ENABLE_BENCHMARK_TESTS=ON",
    "-DOFXGGML_ENABLE_RUNTIME_SOURCES=ON",
    "-DOFXGGML_WITH_OPENFRAMEWORKS=$($WithOpenFrameworks.IsPresent.ToString().ToUpperInvariant())"
)

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed."
}

if (-not $NoBuild) {
    Write-Host "==> Building benchmark executable..."
    & cmake --build $BuildDir --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark build failed."
    }
}

$candidates = @(
    (Join-Path $BuildDir "$Configuration\ofxGgml-tests.exe"),
    (Join-Path $BuildDir "ofxGgml-tests.exe")
)
$testExe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $testExe) {
    throw "Benchmark executable not found under $BuildDir."
}

Write-Host "==> Running benchmarks with filter $Filter"
& $testExe $Filter
if ($LASTEXITCODE -ne 0) {
    throw "Benchmark run failed."
}
