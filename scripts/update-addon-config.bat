@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM update-addon-config.bat — Update addon_config.mk with built ggml libraries
REM
REM This script scans the built ggml libraries and updates addon_config.mk [vs]
REM section with the correct library list. Run this after building ggml if you
REM encounter linker errors about missing ggml libraries.
REM
REM When CUDA backend is detected, this script also adds the required CUDA
REM Toolkit libraries (cublas.lib, cudart.lib) to fix CUBLAS linker errors.
REM
REM Usage:
REM   scripts\update-addon-config.bat
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
set "ADDON_ROOT=%SCRIPT_DIR%.."
set "BUILD_DIR=%ADDON_ROOT%\libs\ggml\build"
set "LIB_DIR=%BUILD_DIR%\src"
set "CONFIG_FILE=%ADDON_ROOT%\addon_config.mk"

if not exist "%LIB_DIR%\Release" (
    echo Error: No Release libraries found in %LIB_DIR%
    echo Please build ggml first using scripts\build-ggml.bat
    exit /b 1
)

echo ==^> Scanning built libraries in %LIB_DIR%\Release...

REM Collect libraries in priority order
set "LIBS="
set "COUNT=0"

REM Core libraries
if exist "%LIB_DIR%\Release\ggml.lib" (
    set "LIBS=!LIBS! libs/ggml/build/src/$^(Configuration^)/ggml.lib"
    set /a COUNT+=1
)
if exist "%LIB_DIR%\Release\ggml-base.lib" (
    set "LIBS=!LIBS! libs/ggml/build/src/$^(Configuration^)/ggml-base.lib"
    set /a COUNT+=1
)
if exist "%LIB_DIR%\Release\ggml-cpu.lib" (
    set "LIBS=!LIBS! libs/ggml/build/src/$^(Configuration^)/ggml-cpu.lib"
    set /a COUNT+=1
)

REM GPU backend libraries (in subdirectories)
if exist "%LIB_DIR%\ggml-cuda\Release\ggml-cuda.lib" (
    set "LIBS=!LIBS! libs/ggml/build/src/ggml-cuda/$^(Configuration^)/ggml-cuda.lib"
    set /a COUNT+=1
)
if exist "%LIB_DIR%\ggml-vulkan\Release\ggml-vulkan.lib" (
    set "LIBS=!LIBS! libs/ggml/build/src/ggml-vulkan/$^(Configuration^)/ggml-vulkan.lib"
    set /a COUNT+=1
)
if exist "%LIB_DIR%\ggml-metal\Release\ggml-metal.lib" (
    set "LIBS=!LIBS! libs/ggml/build/src/ggml-metal/$^(Configuration^)/ggml-metal.lib"
    set /a COUNT+=1
)
if exist "%LIB_DIR%\ggml-opencl\Release\ggml-opencl.lib" (
    set "LIBS=!LIBS! libs/ggml/build/src/ggml-opencl/$^(Configuration^)/ggml-opencl.lib"
    set /a COUNT+=1
)
if exist "%LIB_DIR%\ggml-sycl\Release\ggml-sycl.lib" (
    set "LIBS=!LIBS! libs/ggml/build/src/ggml-sycl/$^(Configuration^)/ggml-sycl.lib"
    set /a COUNT+=1
)

if %COUNT% EQU 0 (
    echo Error: No ggml libraries found in %LIB_DIR%\Release
    exit /b 1
)

echo ==^> Found %COUNT% libraries

REM Check if CUDA backend is present and add CUDA Toolkit libraries
set "CUDA_PRESENT=0"
if exist "%LIB_DIR%\ggml-cuda\Release\ggml-cuda.lib" (
    set "CUDA_PRESENT=1"
    echo ==^> CUDA backend detected - adding CUDA Toolkit libraries
)

REM Add CUDA Toolkit libraries if CUDA is present
REM These libraries are required by ggml-cuda.lib and must be linked by the final application
set "CUDA_LIBS="
set "CUDA_LIB_DIR="
if "%CUDA_PRESENT%"=="1" (
    REM cublas.lib and cudart.lib are the minimum required libraries
    REM The CUDA Toolkit provides these through CMake's FindCUDAToolkit
    if defined CUDA_PATH if exist "%CUDA_PATH%\lib\x64\cublas.lib" (
        set "CUDA_LIB_DIR=%CUDA_PATH%\lib\x64"
    )
    if not defined CUDA_LIB_DIR if defined CUDAToolkit_ROOT if exist "%CUDAToolkit_ROOT%\lib\x64\cublas.lib" (
        set "CUDA_LIB_DIR=%CUDAToolkit_ROOT%\lib\x64"
    )
    if not defined CUDA_LIB_DIR (
        for /f "tokens=1,* delims==" %%A in ('set CUDA_PATH_V 2^>nul') do (
            if not defined CUDA_LIB_DIR (
                if exist "%%B\lib\x64\cublas.lib" (
                    set "CUDA_LIB_DIR=%%B\lib\x64"
                )
            )
        )
    )
    if not defined CUDA_LIB_DIR (
        for /f "delims=" %%D in ('dir /b /ad "%ProgramFiles%\NVIDIA GPU Computing Toolkit\CUDA" 2^>nul ^| sort /R') do (
            if not defined CUDA_LIB_DIR (
                if exist "%ProgramFiles%\NVIDIA GPU Computing Toolkit\CUDA\%%D\lib\x64\cublas.lib" (
                    set "CUDA_LIB_DIR=%ProgramFiles%\NVIDIA GPU Computing Toolkit\CUDA\%%D\lib\x64"
                )
            )
        )
    )

    if defined CUDA_LIB_DIR (
        echo ==^> Using CUDA Toolkit libs from "!CUDA_LIB_DIR!"
        set "CUDA_LIBS=\"!CUDA_LIB_DIR!\cublas.lib\" \"!CUDA_LIB_DIR!\cudart.lib\""
    ) else (
        echo Warning: Could not locate CUDA Toolkit lib directory - falling back to library names
        echo Warning: Ensure your project links against the CUDA Toolkit lib path (e.g. %CUDA_PATH%\lib\x64)
        set "CUDA_LIBS=cublas.lib cudart.lib"
    )
)

REM Create temporary file with new content
set "TEMP_FILE=%TEMP%\addon_config_temp.mk"
set "IN_VS_SECTION=0"
set "IN_MARKER_BLOCK=0"

(
    for /f "usebackq delims=" %%a in ("%CONFIG_FILE%") do (
        set "LINE=%%a"

        REM Check if we're entering VS section
        if "!LINE!"=="vs:" (
            set "IN_VS_SECTION=1"
            echo !LINE!
        ) else if "!IN_VS_SECTION!"=="1" (
            REM Check for start marker
            if not "!LINE:@DIFFUSION_LIBS_START vs=!"=="!LINE!" (
                set "IN_MARKER_BLOCK=1"
                echo 	# @DIFFUSION_LIBS_START vs
                REM Output library list
                for %%L in (!LIBS!) do (
                    echo 	ADDON_LIBS += %%L
                )
                REM Output CUDA Toolkit libraries if present
                if defined CUDA_LIBS (
                    for %%C in (!CUDA_LIBS!) do (
                        echo 	ADDON_LIBS += %%C
                    )
                )
                echo 	# @DIFFUSION_LIBS_END vs
                REM Skip until end marker
            ) else if not "!LINE:@DIFFUSION_LIBS_END vs=!"=="!LINE!" (
                set "IN_MARKER_BLOCK=0"
                REM Already output end marker, skip this line
            ) else if "!IN_MARKER_BLOCK!"=="0" (
                REM Check if we left VS section (new section starts)
                if not "!LINE:~0,1!"=="	" if not "!LINE!"=="" (
                    set "IN_VS_SECTION=0"
                )
                echo !LINE!
            )
        ) else (
            echo !LINE!
        )
    )
) > "%TEMP_FILE%"

REM Replace original file
move /y "%TEMP_FILE%" "%CONFIG_FILE%" >nul

echo ==^> Updated addon_config.mk [vs] section with %COUNT% libraries
if "%CUDA_PRESENT%"=="1" (
    echo ==^> Added CUDA Toolkit libraries: cublas.lib, cudart.lib
)
echo ==^> Rebuild your Visual Studio project to apply changes

endlocal
exit /b 0
