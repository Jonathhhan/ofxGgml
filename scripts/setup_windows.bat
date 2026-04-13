@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM setup_windows.bat — One-command setup for ofxGgml on Windows.
REM
REM This script builds ggml and can optionally download recommended GGUF models.
REM
REM Usage:
REM   scripts\setup_windows.bat [OPTIONS]
REM
REM Options:
REM   --cpu-only         Build CPU backend only
REM   --auto             Auto-detect GPU backends (default)
REM   --gpu, --cuda      Enable CUDA backend
REM   --vulkan           Enable Vulkan backend
REM   --skip-ggml        Skip building ggml
REM   --skip-model       Skip downloading model file(s)
REM   --model-preset N   Download preset 1 or 2 (default: both)
REM   --jobs N           Parallel build jobs (default: %NUMBER_OF_PROCESSORS%)
REM   --clean            Remove previous build directories before building
REM   --help             Show this help message
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
set "ADDON_ROOT=%SCRIPT_DIR%.."
set "MODEL_OUTPUT_DIR=%ADDON_ROOT%\ofxGgmlGuiExample\bin\data\models"
set "SKIP_GGML=0"
set "SKIP_MODEL=0"
set "GPU_FLAG=--auto"
set "JOBS_FLAG="
set "CLEAN_FLAG="
set "MODEL_PRESET="

:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--cpu-only" (
    set "GPU_FLAG=--cpu-only"
    shift
    goto parse_args
)
if /i "%~1"=="--auto" (
    set "GPU_FLAG=--auto"
    shift
    goto parse_args
)
if /i "%~1"=="--cuda" (
    set "GPU_FLAG=--cuda"
    shift
    goto parse_args
)
if /i "%~1"=="--gpu" (
    set "GPU_FLAG=--gpu"
    shift
    goto parse_args
)
if /i "%~1"=="--vulkan" (
    set "GPU_FLAG=--vulkan"
    shift
    goto parse_args
)
if /i "%~1"=="--skip-ggml" (
    set "SKIP_GGML=1"
    shift
    goto parse_args
)
if /i "%~1"=="--skip-model" (
    set "SKIP_MODEL=1"
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
echo   --skip-ggml        Skip building ggml
echo   --skip-model       Skip downloading model files
echo   --model-preset N   Download preset 1 or 2 ^(default: both^)
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
    echo [1/2] Building ggml...
    call "%SCRIPT_DIR%build-ggml.bat" %GPU_FLAG% %JOBS_FLAG% %CLEAN_FLAG%
    if errorlevel 1 (
        echo Error: ggml build failed.
        exit /b 1
    )
) else (
    echo [1/2] Skipping ggml build ^(--skip-ggml^).
)

if "%SKIP_MODEL%"=="0" (
    echo [2/2] Downloading model files...
    call :download_models
    if errorlevel 1 exit /b 1
) else (
    echo [2/2] Skipping model download ^(--skip-model^).
)

echo.
echo Setup complete.
echo.
echo Next steps:
echo   1. Add ofxGgml to your project addons.make
echo   2. Build and run your project
echo.
exit /b 0

:download_models
if not exist "%MODEL_OUTPUT_DIR%" mkdir "%MODEL_OUTPUT_DIR%"
if not exist "%MODEL_OUTPUT_DIR%" (
    echo Error: Could not create model output directory: %MODEL_OUTPUT_DIR%
    exit /b 1
)

set "URL1=https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf"
set "NAME1=qwen2.5-1.5b-instruct-q4_k_m.gguf"
set "URL2=https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf"
set "NAME2=qwen2.5-coder-1.5b-instruct-q4_k_m.gguf"

if defined MODEL_PRESET (
    if "%MODEL_PRESET%"=="1" (
        call :download_one "%URL1%" "%NAME1%"
        exit /b %errorlevel%
    )
    if "%MODEL_PRESET%"=="2" (
        call :download_one "%URL2%" "%NAME2%"
        exit /b %errorlevel%
    )
    echo Error: Invalid --model-preset value "%MODEL_PRESET%" ^(valid: 1 or 2^).
    exit /b 1
)

call :download_one "%URL1%" "%NAME1%"
if errorlevel 1 exit /b 1
call :download_one "%URL2%" "%NAME2%"
if errorlevel 1 exit /b 1
exit /b 0

:download_one
set "MODEL_URL=%~1"
set "MODEL_NAME=%~2"
set "MODEL_PATH=%MODEL_OUTPUT_DIR%\%MODEL_NAME%"

if exist "%MODEL_PATH%" (
    echo   - %MODEL_NAME% already exists, skipping.
    exit /b 0
)

echo   - Downloading %MODEL_NAME%
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "try { Invoke-WebRequest -Uri '%MODEL_URL%' -OutFile '%MODEL_PATH%' -UseBasicParsing } catch { exit 1 }"

if errorlevel 1 (
    echo Error: Failed to download %MODEL_NAME%
    if exist "%MODEL_PATH%" del /q "%MODEL_PATH%" >nul 2>&1
    exit /b 1
)

for %%A in ("%MODEL_PATH%") do set "MODEL_SIZE=%%~zA"
if "!MODEL_SIZE!"=="0" (
    echo Error: Downloaded file is empty: %MODEL_NAME%
    del /q "%MODEL_PATH%" >nul 2>&1
    exit /b 1
)

echo     Saved to %MODEL_PATH%
exit /b 0
