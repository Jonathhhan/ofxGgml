@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM setup_windows.bat — One-command setup for ofxGgml on Windows.
REM
REM This script builds ggml and llama.cpp CLI tools.
REM Model download is optional and disabled by default.
REM
REM Usage:
REM   scripts\setup_windows.bat [OPTIONS]
REM
REM Options:
REM   --cpu-only         Build CPU backend only
REM   --auto             Auto-detect GPU backends (default)
REM   --gpu, --cuda      Enable CUDA backend
REM   --vulkan           Enable Vulkan backend
REM   --with-debug       Also build ggml Debug libraries (default: Release only)
REM   --skip-ggml        Skip building ggml
REM   --skip-llama       Skip building llama.cpp CLI tools
REM   --skip-model       Skip downloading text model file(s)
REM   --download-model   Download text model file(s)
REM   --model-preset N   Download text preset 1 or 2 (default: both)
REM   --jobs N           Parallel build jobs (default: %NUMBER_OF_PROCESSORS%)
REM   --clean            Remove previous build directories before building
REM   --help             Show this help message
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
set "ADDON_ROOT=%SCRIPT_DIR%.."
set "MODEL_OUTPUT_DIR=%ADDON_ROOT%\ofxGgmlGuiExample\bin\data\models"
set "MODEL_DOWNLOADER=%SCRIPT_DIR%download-model.ps1"
set "LLAMA_BUILDER=%SCRIPT_DIR%build-llama-cli.sh"
set "SKIP_GGML=0"
set "SKIP_LLAMA=0"
set "SKIP_MODEL=1"
set "GPU_FLAG=--auto"
set "LLAMA_GPU_FLAG=--auto"
set "JOBS_FLAG=--jobs %NUMBER_OF_PROCESSORS%"
set "CLEAN_FLAG="
set "DEBUG_FLAG="
set "MODEL_PRESET="
set "GGML_BUILD_FAILED=0"
set "LLAMA_BUILD_FAILED=0"

:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--cpu-only" (
    set "GPU_FLAG=--cpu-only"
    set "LLAMA_GPU_FLAG="
    shift
    goto parse_args
)
if /i "%~1"=="--auto" (
    set "GPU_FLAG=--auto"
    set "LLAMA_GPU_FLAG=--auto"
    shift
    goto parse_args
)
if /i "%~1"=="--cuda" (
    set "GPU_FLAG=--cuda"
    set "LLAMA_GPU_FLAG=--cuda"
    shift
    goto parse_args
)
if /i "%~1"=="--gpu" (
    set "GPU_FLAG=--cuda"
    set "LLAMA_GPU_FLAG=--gpu"
    shift
    goto parse_args
)
if /i "%~1"=="--vulkan" (
    set "GPU_FLAG=--vulkan"
    set "LLAMA_GPU_FLAG=--vulkan"
    shift
    goto parse_args
)
if /i "%~1"=="--with-debug" (
    set "DEBUG_FLAG=--with-debug"
    shift
    goto parse_args
)
if /i "%~1"=="--skip-ggml" (
    set "SKIP_GGML=1"
    shift
    goto parse_args
)
if /i "%~1"=="--skip-llama" (
    set "SKIP_LLAMA=1"
    shift
    goto parse_args
)
if /i "%~1"=="--skip-model" (
    set "SKIP_MODEL=1"
    shift
    goto parse_args
)
if /i "%~1"=="--download-model" (
    set "SKIP_MODEL=0"
    shift
    goto parse_args
)
if /i "%~1"=="--model-preset" (
    if "%~2"=="" (
        echo Error: --model-preset requires a value.
        exit /b 1
    )
    set "MODEL_PRESET=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--jobs" (
    if "%~2"=="" (
        echo Error: --jobs requires a value.
        exit /b 1
    )
    set "JOBS_FLAG=--jobs %~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--clean" (
    set "CLEAN_FLAG=--clean"
    shift
    goto parse_args
)
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
echo Error: Unknown option: %~1
exit /b 1

:usage
echo setup_windows.bat — One-command setup for ofxGgml on Windows.
echo.
echo Usage:
echo   scripts\setup_windows.bat [OPTIONS]
echo.
echo Options:
echo   --cpu-only         Build CPU backend only
echo   --auto             Auto-detect GPU backends ^(default^)
echo   --gpu, --cuda      Enable CUDA backend
echo   --vulkan           Enable Vulkan backend
echo   --with-debug       Also build ggml Debug libraries ^(default: Release only^)
echo   --skip-ggml        Skip building ggml
echo   --skip-llama       Skip building llama.cpp CLI tools
echo   --skip-model       Skip downloading text model files ^(default^)
echo   --download-model   Download text model files
echo   --model-preset N   Download text preset 1 or 2 ^(default: both^)
echo   --jobs N           Parallel build jobs ^(default: %NUMBER_OF_PROCESSORS%^)
echo   --clean            Remove previous build directories before building
echo   --help             Show this help message
exit /b 0

:done_args

echo.
echo   =====================================
echo           ofxGgml Setup (Windows)
echo   =====================================
echo.

if "%SKIP_GGML%"=="0" (
    echo [1/3] Building ggml...
    call "%SCRIPT_DIR%build-ggml.bat" %GPU_FLAG% %DEBUG_FLAG% %JOBS_FLAG% %CLEAN_FLAG%
    if errorlevel 1 (
        echo Warning: ggml build failed. Continuing...
        set "GGML_BUILD_FAILED=1"
    )
) else (
    echo [1/3] Skipping ggml build ^(--skip-ggml^).
)

if "%SKIP_LLAMA%"=="0" (
    echo [2/3] Building llama.cpp CLI tools...
    where bash >nul 2>&1
    if errorlevel 1 (
        echo Warning: bash was not found in PATH. Install Git Bash or WSL, or use --skip-llama.
        set "LLAMA_BUILD_FAILED=1"
    ) else (
        set "LLAMA_ARGS="
        if defined LLAMA_GPU_FLAG set "LLAMA_ARGS=!LLAMA_GPU_FLAG!"
        set "LLAMA_ARGS=!LLAMA_ARGS! %JOBS_FLAG% %CLEAN_FLAG%"
        bash "%LLAMA_BUILDER%" !LLAMA_ARGS!
        if errorlevel 1 (
            echo Warning: llama.cpp CLI build failed.
            set "LLAMA_BUILD_FAILED=1"
        )
    )
) else (
    echo [2/3] Skipping llama.cpp build ^(--skip-llama^).
)

if "%SKIP_MODEL%"=="0" (
    echo [3/3] Downloading model files...
    set "DL_ARGS="
    if defined MODEL_PRESET set "DL_ARGS=-Preset %MODEL_PRESET%"
    powershell -NoProfile -ExecutionPolicy Bypass -File "%MODEL_DOWNLOADER%" %DL_ARGS% -OutputDir "%MODEL_OUTPUT_DIR%"
    if errorlevel 1 (
        echo Error: Model download failed.
        exit /b 1
    )
) else (
    echo [3/3] Skipping model download ^(default^).
)

if "%GGML_BUILD_FAILED%"=="1" (
    echo.
    echo Setup partially completed: ggml build failed.
    echo.
    exit /b 1
)

if "%LLAMA_BUILD_FAILED%"=="1" (
    echo.
    echo Setup partially completed: llama.cpp CLI build failed.
    echo.
    exit /b 1
)

echo.
echo Setup complete.
echo.
echo Next steps:
echo   1. Add ofxGgml to your project addons.make
echo   2. Build and run your project
echo.
exit /b 0
