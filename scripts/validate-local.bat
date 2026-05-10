@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "VALIDATE_PS1=%SCRIPT_DIR%validate-local.ps1"

powershell.exe -ExecutionPolicy Bypass -NoProfile -File "%VALIDATE_PS1%" %*
exit /b %ERRORLEVEL%
