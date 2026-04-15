@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM build-ggml.bat — Build the bundled ggml tensor library on Windows.
REM
REM The ggml source is bundled inside libs\ggml\.  This script runs CMake
REM to configure and build it, producing static libraries that the addon
REM links against.  GPU backends (CUDA, Vulkan) can be auto-detected or
REM explicitly enabled via command-line flags.  By default auto-detection is on.
REM
REM Usage:
REM   scripts\build-ggml.bat [OPTIONS]
REM
REM Options:
REM   --gpu, --cuda  Enable CUDA backend (requires CUDA toolkit)
REM   --vulkan       Enable Vulkan backend (requires Vulkan SDK)
REM   --auto         Auto-detect available GPU backends (default)
REM   --cpu-only     Disable GPU autodetection, build CPU backend only
REM   --with-debug   Also build Debug libraries (default: Release only)
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
set "BUILD_DEBUG=0"

:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--cuda" (
    set "ENABLE_CUDA=ON"
    set "AUTO_DETECT=0"
    shift
    goto parse_args
)
if /i "%~1"=="--gpu" (
    set "ENABLE_CUDA=ON"
    set "AUTO_DETECT=0"
    shift
    goto parse_args
)
if /i "%~1"=="--vulkan" (
    set "ENABLE_VULKAN=ON"
    set "AUTO_DETECT=0"
    shift
    goto parse_args
)
if /i "%~1"=="--cpu-only" (
    set "AUTO_DETECT=0"
    set "ENABLE_CUDA="
    set "ENABLE_VULKAN="
    shift
    goto parse_args
)
if /i "%~1"=="--auto" (
    set "AUTO_DETECT=1"
    shift
    goto parse_args
)
if /i "%~1"=="--clean" (
    set "CLEAN=1"
    shift
    goto parse_args
)
if /i "%~1"=="--with-debug" (
    set "BUILD_DEBUG=1"
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
echo   --gpu, --cuda  Enable CUDA backend (requires CUDA installed)
echo   --vulkan       Enable Vulkan backend (requires Vulkan SDK)
echo   --auto         Auto-detect available GPU backends (default)
echo   --cpu-only     Disable GPU autodetection, build CPU backend only
echo   --with-debug   Also build Debug libraries ^(default: Release only^)
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
set "PLATFORM_ARG="

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

if defined CMAKE_GENERATOR (
    echo(%CMAKE_GENERATOR% | findstr /i /c:"Visual Studio" >nul
    if not errorlevel 1 set "PLATFORM_ARG=-A x64"
) else (
    set "PLATFORM_ARG=-A x64"
)

if defined PLATFORM_ARG (
    cmake -B "%BUILD_DIR%" "%GGML_DIR%" %PLATFORM_ARG% %CMAKE_ARGS%
    if errorlevel 1 (
        echo ==^> Configure with x64 platform failed, retrying with default platform settings...
        cmake -B "%BUILD_DIR%" "%GGML_DIR%" %CMAKE_ARGS%
    )
) else (
    cmake -B "%BUILD_DIR%" "%GGML_DIR%" %CMAKE_ARGS%
)

if errorlevel 1 (
    echo Error: CMake configuration failed.
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM Build
REM ---------------------------------------------------------------------------

call :build_config Release
if errorlevel 1 exit /b 1
if "%BUILD_DEBUG%"=="1" (
    call :build_config Debug
    if errorlevel 1 exit /b 1
)

REM ---------------------------------------------------------------------------
REM Verify
REM ---------------------------------------------------------------------------

echo ==^> Verifying build output...

set "LIB_DIR_REL=%BUILD_DIR%\src\Release"
set "LIB_DIR_DBG=%BUILD_DIR%\src\Debug"

call :verify_config Release
if errorlevel 1 exit /b 1
if "%BUILD_DEBUG%"=="1" (
    call :verify_config Debug
    if errorlevel 2 (
        echo ==^> Warning: No ggml Debug libraries found in %LIB_DIR_DBG%
        echo ==^> Debug mode may not work. Only Release configuration will be available.
    )
)

REM ---------------------------------------------------------------------------
REM addon_config.mk — Update VS library list (ggml + CUDA deps)
REM ---------------------------------------------------------------------------

echo ==^> Updating addon_config.mk for Visual Studio...
call "%SCRIPT_DIR%dev\update-addon-config.bat"
if errorlevel 1 (
    echo Error: addon_config.mk update failed.
    exit /b 1
)

echo ==^> Done! ggml has been built in %BUILD_DIR%
echo ==^>   Release libs: %LIB_DIR_REL%
if "%BUILD_DEBUG%"=="1" (
    echo ==^>   Debug libs:   %LIB_DIR_DBG%
) else (
    echo ==^>   Debug libs:   skipped ^(use --with-debug to build them^)
)
echo ==^>   addon_config.mk [vs] updated with ggml libraries
echo ==^>   CUDA builds also add CUDA libs: cublas.lib, cudart.lib
echo ==^>

endlocal
exit /b 0

:build_config
set "CFG=%~1"
echo ==^> Building ggml (%CFG%) with %JOBS% parallel jobs...
cmake --build "%BUILD_DIR%" --config %CFG% -j %JOBS%
if errorlevel 1 (
    if not "%JOBS%"=="1" (
        echo ==^> Build failed with parallel jobs for ggml ^(%CFG%^).
        echo ==^> Retry triggered: running ggml ^(%CFG%^) again with 1 job to avoid transient CUDA/MSBuild object races...
        cmake --build "%BUILD_DIR%" --config %CFG% -j 1
        if errorlevel 1 (
            echo ==^> Retry result: FAILED for ggml ^(%CFG%^) with 1 job.
        ) else (
            echo ==^> Retry result: SUCCESS for ggml ^(%CFG%^) with 1 job.
            exit /b 0
        )
    )
    echo Error: CMake %CFG% build failed.
    exit /b 1
)
exit /b 0

:verify_config
set "CFG=%~1"
set "CFG_LIB_DIR=%BUILD_DIR%\src\%CFG%"
set "FOUND=0"
if exist "%CFG_LIB_DIR%\ggml.lib" (
    echo ==^>   Found ^(%CFG%^): %CFG_LIB_DIR%\ggml.lib
    set "FOUND=1"
)
if exist "%CFG_LIB_DIR%\ggml-base.lib" (
    echo ==^>   Found ^(%CFG%^): %CFG_LIB_DIR%\ggml-base.lib
    set "FOUND=1"
)
if exist "%CFG_LIB_DIR%\ggml-cpu.lib" (
    echo ==^>   Found ^(%CFG%^): %CFG_LIB_DIR%\ggml-cpu.lib
    set "FOUND=1"
)

if "%FOUND%"=="0" (
    if /i "%CFG%"=="Release" (
        echo Error: No ggml Release libraries found in %CFG_LIB_DIR%
        echo Make sure CMake built successfully with the Visual Studio generator.
        exit /b 1
    ) else (
        exit /b 2
    )
)
exit /b 0
