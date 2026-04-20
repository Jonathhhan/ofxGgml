@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM setup_windows.bat — One-command setup for ofxGgml on Windows.
REM
REM This script builds ggml and keeps the local llama.cpp runtime optional.
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
REM   --with-llama-cli   Build optional local llama.cpp runtime (server + CLI fallback)
REM   --with-piper       Install optional local Piper TTS runtime
REM   --download-piper-voice  Download a Piper voice into models\piper
REM   --piper-voice NAME Piper voice name (default: en_US-lessac-medium)
REM   --skip-llama       Skip building llama.cpp runtime tools (legacy no-op)
REM   --skip-model       Skip downloading text model file(s)
REM   --download-model   Download text model file(s)
REM   --model-preset N   Download text preset 1 or 2 (default: both)
REM   --jobs N           Parallel build jobs (default: %NUMBER_OF_PROCESSORS%)
REM   --clean            Remove previous build directories before building
REM   --help             Show this help message
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
set "ADDON_ROOT=%SCRIPT_DIR%.."
set "MODEL_OUTPUT_DIR=%ADDON_ROOT%\models"
set "MODEL_DOWNLOADER=%SCRIPT_DIR%download-model.ps1"
set "LLAMA_RUNTIME_BUILDER=%SCRIPT_DIR%build-llama-server.ps1"
set "PIPER_INSTALLER=%SCRIPT_DIR%install-piper.ps1"
set "SKIP_GGML=0"
set "SKIP_LLAMA=1"
set "SKIP_PIPER=1"
set "PIPER_DOWNLOAD_VOICE=0"
set "PIPER_VOICE_NAME=en_US-lessac-medium"
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
if /i "%~1"=="--with-llama-cli" (
    set "SKIP_LLAMA=0"
    shift
    goto parse_args
)
if /i "%~1"=="--with-piper" (
    set "SKIP_PIPER=0"
    shift
    goto parse_args
)
if /i "%~1"=="--download-piper-voice" (
    set "SKIP_PIPER=0"
    set "PIPER_DOWNLOAD_VOICE=1"
    shift
    goto parse_args
)
if /i "%~1"=="--piper-voice" (
    if "%~2"=="" (
        echo Error: --piper-voice requires a value.
        exit /b 1
    )
    set "SKIP_PIPER=0"
    set "PIPER_DOWNLOAD_VOICE=1"
    set "PIPER_VOICE_NAME=%~2"
    shift
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
echo   --with-llama-cli   Build optional local llama.cpp runtime ^(server + CLI fallback^)
echo   --with-piper       Install optional local Piper TTS runtime
echo   --download-piper-voice  Download a Piper voice into models\piper
echo   --piper-voice NAME Piper voice name ^(default: en_US-lessac-medium^)
echo   --skip-llama       Skip building llama.cpp runtime tools ^(legacy no-op^)
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
    echo [1/4] Building ggml...
    call "%SCRIPT_DIR%build-ggml.bat" %GPU_FLAG% %DEBUG_FLAG% %JOBS_FLAG% %CLEAN_FLAG%
    if errorlevel 1 (
        echo Warning: ggml build failed. Continuing...
        set "GGML_BUILD_FAILED=1"
    )
) else (
    echo [1/4] Skipping ggml build ^(--skip-ggml^).
)

if "%SKIP_LLAMA%"=="0" (
    echo [2/4] Building optional local llama.cpp runtime...
    set "LLAMA_ARGS="
    if /i "%LLAMA_GPU_FLAG%"=="--cuda" set "LLAMA_ARGS=!LLAMA_ARGS! -Cuda"
    if /i "%LLAMA_GPU_FLAG%"=="--gpu" set "LLAMA_ARGS=!LLAMA_ARGS! -Cuda"
    if /i "%GPU_FLAG%"=="--cpu-only" set "LLAMA_ARGS=!LLAMA_ARGS! -CpuOnly"
    for /f "tokens=2" %%A in ("%JOBS_FLAG%") do set "LLAMA_JOBS=%%A"
    if defined LLAMA_JOBS set "LLAMA_ARGS=!LLAMA_ARGS! -Jobs !LLAMA_JOBS!"
    if defined CLEAN_FLAG set "LLAMA_ARGS=!LLAMA_ARGS! -Clean"
    powershell -NoProfile -ExecutionPolicy Bypass -File "%LLAMA_RUNTIME_BUILDER%" !LLAMA_ARGS!
    if errorlevel 1 (
        echo Warning: llama.cpp runtime build failed.
        set "LLAMA_BUILD_FAILED=1"
    )
) else (
    echo [2/4] Skipping optional local llama.cpp runtime ^(server-first default^).
)

if "%SKIP_PIPER%"=="0" (
    echo [3/4] Installing optional local Piper runtime...
    set "PIPER_ARGS="
    if "%PIPER_DOWNLOAD_VOICE%"=="1" (
        set "PIPER_ARGS=!PIPER_ARGS! -VoiceName %PIPER_VOICE_NAME%"
    ) else (
        set "PIPER_ARGS=!PIPER_ARGS! -SkipVoiceDownload"
    )
    powershell -NoProfile -ExecutionPolicy Bypass -File "%PIPER_INSTALLER%" !PIPER_ARGS!
    if errorlevel 1 (
        echo Warning: Piper runtime install failed.
        exit /b 1
    )
) else (
    echo [3/4] Skipping optional Piper runtime.
)

if "%SKIP_MODEL%"=="0" (
    echo [4/4] Downloading model files...
    set "DL_ARGS="
    if defined MODEL_PRESET set "DL_ARGS=-Preset %MODEL_PRESET%"
    powershell -NoProfile -ExecutionPolicy Bypass -File "%MODEL_DOWNLOADER%" %DL_ARGS% -OutputDir "%MODEL_OUTPUT_DIR%"
    if errorlevel 1 (
        echo Error: Model download failed.
        exit /b 1
    )
) else (
    echo [4/4] Skipping model download ^(default^).
)

if "%GGML_BUILD_FAILED%"=="1" (
    echo.
    echo Setup partially completed: ggml build failed.
    echo.
    exit /b 1
)

if "%LLAMA_BUILD_FAILED%"=="1" (
    echo.
    echo Setup partially completed: optional llama.cpp runtime build failed.
    echo.
    exit /b 1
)

echo.
echo Setup complete.
echo.
echo Next steps:
echo   1. Add ofxGgml to your project addons.make
echo   2. Build and run your project
echo   3. If you want the local llama.cpp runtime, rerun with --with-llama-cli
echo   4. If you want bundled Piper TTS, rerun with --with-piper --download-piper-voice
echo   5. Use scripts\start-llama-server.ps1 to launch the local server manually
echo.
exit /b 0
