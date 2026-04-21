@echo off
setlocal
REM ---------------------------------------------------------------------------
REM build-ggml.bat — Windows wrapper that delegates to build-ggml.sh
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_SH=%SCRIPT_DIR%build-ggml.sh"
set "BASH_CMD="

REM Prefer Git Bash/WSL bash if available
for %%B in (bash.exe bash) do (
    where %%B >nul 2>&1
    if not errorlevel 1 (
        set "BASH_CMD=%%B"
        goto :have_bash
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
"%BASH_CMD%" "%SCRIPT_SH%" %*
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" (
    echo [ofxGgml] ggml build completed successfully.
) else (
    echo [ofxGgml] ggml build failed with exit code %RC%.
)

endlocal
exit /b %RC%
