@echo off
setlocal
REM ---------------------------------------------------------------------------
REM build-ggml.bat — Windows wrapper that delegates to build-ggml.sh
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_SH=%SCRIPT_DIR%build-ggml.sh"
set "BASH_CMD="

REM Prefer Bash distributions that understand Windows drive paths.
for %%B in ("%ProgramFiles%\Git\bin\bash.exe" "%ProgramFiles%\Git\usr\bin\bash.exe" "C:\msys64\usr\bin\bash.exe" "C:\msys64\mingw64\bin\bash.exe") do (
    if exist "%%~B" (
        set "BASH_CMD=%%~B"
        goto :have_bash
    )
)

REM Fall back to PATH, but avoid the WindowsApps WSL launcher because it
REM cannot execute the C:/... path passed below.
for %%B in (bash.exe bash) do (
    for /f "delims=" %%P in ('where %%B 2^>nul') do (
        echo %%P | findstr /I "\\WindowsApps\\bash.exe" >nul
        if errorlevel 1 (
            set "BASH_CMD=%%P"
            goto :have_bash
        )
    )
)

echo [ofxGgml] Error: bash not found in PATH.
echo [ofxGgml] Install Git for Windows (Git Bash) or run build-ggml.sh from WSL.
endlocal
exit /b 1

:have_bash
if not exist "%SCRIPT_SH%" (
    echo [ofxGgml] Error: build-ggml.sh not found next to this script.
    endlocal
    exit /b 1
)

REM Convert the script path for bash
set "SCRIPT_SH=%SCRIPT_SH:\=/%"

echo [ofxGgml] build-ggml.bat forwarding to build-ggml.sh via %BASH_CMD%...
"%BASH_CMD%" -lc "'%SCRIPT_SH%' %*"
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" (
    echo [ofxGgml] ggml build completed successfully.
) else (
    echo [ofxGgml] ggml build failed with exit code %RC%.
)

endlocal
exit /b %RC%
