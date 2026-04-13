@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM build-ggml.bat — Build the bundled ggml tensor library on Windows.
REM
REM The ggml source is bundled inside libs\ggml\.  This script runs CMake
REM to configure and build it, producing static libraries that the addon
REM links against.  GPU backends (CUDA, Vulkan) are auto-detected by default.
REM
REM Usage:
REM   scripts\build-ggml.bat [OPTIONS]
REM
REM Options:
REM   --gpu, --cuda  Enable CUDA backend (requires CUDA toolkit)
REM   --vulkan       Enable Vulkan backend (requires Vulkan SDK)
REM   --cpu-only     Disable GPU autodetection, build CPU backend only
REM   --clean        Remove build directory before building
REM   --jobs N       Parallel build jobs (default: %NUMBER_OF_PROCESSORS%)
REM   --help         Show this help message
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
set "ADDON_ROOT=%SCRIPT_DIR%.."
set "GGML_DIR=%ADDON_ROOT%\libs\ggml"
set "BUILD_DIR=%GGML_DIR%\build"
set "JOBS=%NUMBER_OF_PROCESSORS%"
set "ENABLE_CUDA="
set "ENABLE_VULKAN="
set "AUTO_DETECT=1"
set "CLEAN=0"

:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--cuda" (
    set "ENABLE_CUDA=ON"
    shift
    goto parse_args
)
if /i "%~1"=="--gpu" (
    set "ENABLE_CUDA=ON"
    shift
    goto parse_args
)
if /i "%~1"=="--vulkan" (
    set "ENABLE_VULKAN=ON"
    shift
    goto parse_args
)
if /i "%~1"=="--cpu-only" (
    set "AUTO_DETECT=0"
    shift
    goto parse_args
)
if /i "%~1"=="--clean" (
    set "CLEAN=1"
    shift
    goto parse_args
)
if /i "%~1"=="--jobs" (
    set "JOBS=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
echo Error: Unknown option: %~1
exit /b 1

:usage
echo build-ggml.bat — Build the bundled ggml tensor library on Windows.
echo.
echo Usage:
echo   scripts\build-ggml.bat [OPTIONS]
echo.
echo Options:
echo   --gpu, --cuda  Enable CUDA backend (requires CUDA toolkit)
echo   --vulkan       Enable Vulkan backend (requires Vulkan SDK)
echo   --cpu-only     Disable GPU autodetection, build CPU backend only
echo   --clean        Remove build directory before building
echo   --jobs N       Parallel build jobs (default: %NUMBER_OF_PROCESSORS%)
echo   --help         Show this help message
exit /b 0

:done_args

REM ---------------------------------------------------------------------------
REM Prerequisites
REM ---------------------------------------------------------------------------

where cmake >nul 2>&1
if errorlevel 1 (
    echo Error: Required command not found: cmake
    echo Please install CMake and make sure it is in your PATH.
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM Clean
REM ---------------------------------------------------------------------------

if "%CLEAN%"=="1" (
    echo ==^> Cleaning previous build...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

REM ---------------------------------------------------------------------------
REM Configure
REM ---------------------------------------------------------------------------

echo ==^> Configuring ggml build...

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

set "CMAKE_ARGS=-DCMAKE_BUILD_TYPE=Release"

if "%AUTO_DETECT%"=="1" (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DOFXGGML_GPU_AUTODETECT=ON"
) else (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DOFXGGML_GPU_AUTODETECT=OFF"
)

if defined ENABLE_CUDA (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DOFXGGML_CUDA=%ENABLE_CUDA%"
)
if defined ENABLE_VULKAN (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DOFXGGML_VULKAN=%ENABLE_VULKAN%"
)

cmake -B "%BUILD_DIR%" "%GGML_DIR%" %CMAKE_ARGS%
if errorlevel 1 (
    echo Error: CMake configuration failed.
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM Build
REM ---------------------------------------------------------------------------

echo ==^> Building ggml with %JOBS% parallel jobs...

cmake --build "%BUILD_DIR%" --config Release -j %JOBS%
if errorlevel 1 (
    echo Error: CMake build failed.
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM Verify
REM ---------------------------------------------------------------------------

echo ==^> Verifying build output...

set "LIB_DIR=%BUILD_DIR%\src\Release"
set "FOUND=0"

if exist "%LIB_DIR%\ggml.lib" (
    echo ==^>   Found: %LIB_DIR%\ggml.lib
    set "FOUND=1"
)
if exist "%LIB_DIR%\ggml-base.lib" (
    echo ==^>   Found: %LIB_DIR%\ggml-base.lib
    set "FOUND=1"
)
if exist "%LIB_DIR%\ggml-cpu.lib" (
    echo ==^>   Found: %LIB_DIR%\ggml-cpu.lib
    set "FOUND=1"
)

if "%FOUND%"=="0" (
    echo Error: No ggml libraries found in %LIB_DIR%
    echo Make sure CMake built successfully with the Visual Studio generator.
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM Update addon_config.mk with detected libraries
REM ---------------------------------------------------------------------------

echo ==^> Updating addon_config.mk with built libraries...

set "CONFIG_FILE=%ADDON_ROOT%\addon_config.mk"
if not exist "%CONFIG_FILE%" (
    echo ==^> Warning: addon_config.mk not found, skipping config update.
    goto skip_config_update
)

REM Collect count of built ggml .lib files
set "LIB_COUNT=0"
for %%F in ("%LIB_DIR%\ggml*.lib") do (
    set /a LIB_COUNT+=1
)

if %LIB_COUNT%==0 (
    echo ==^> Warning: No libraries found to update in addon_config.mk.
    goto skip_config_update
)

REM Write a temporary awk-like replacement using PowerShell
set "START_MARKER=# @DIFFUSION_LIBS_START vs"
set "END_MARKER=# @DIFFUSION_LIBS_END vs"

REM Build replacement content to a temp file
set "TMPFILE=%TEMP%\ofxggml_config_update.tmp"
(
    echo 	%START_MARKER%
    for %%F in ("%LIB_DIR%\ggml*.lib") do (
        echo 	ADDON_LIBS += libs/ggml/build/src/Release/%%~nxF
    )
    echo 	%END_MARKER%
) > "%TMPFILE%"

REM Use PowerShell to do the replacement between markers
powershell -NoProfile -Command ^
    "$content = Get-Content '%CONFIG_FILE%' -Raw;" ^
    "$replacement = Get-Content '%TMPFILE%' -Raw;" ^
    "$pattern = '(?ms)\t# @DIFFUSION_LIBS_START vs\r?\n.*?\t# @DIFFUSION_LIBS_END vs';" ^
    "$newContent = $content -replace $pattern, $replacement.TrimEnd();" ^
    "Set-Content '%CONFIG_FILE%' -Value $newContent -NoNewline"

if errorlevel 1 (
    echo ==^> Warning: Could not update addon_config.mk automatically.
) else (
    echo ==^> Updated addon_config.mk [vs] with %LIB_COUNT% libraries.
)

del /q "%TMPFILE%" 2>nul

:skip_config_update

echo ==^> Done! ggml has been built in %BUILD_DIR%
echo ==^>
echo ==^> The addon will automatically find the libraries in this location.
echo ==^> Build your OF project with ofxGgml.

endlocal
