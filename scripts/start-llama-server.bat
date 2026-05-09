@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "START_PS1=%SCRIPT_DIR%start-llama-server.ps1"

where pwsh.exe >nul 2>nul
if errorlevel 1 goto try_windows_powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%START_PS1%" %*
exit /b %ERRORLEVEL%

:try_windows_powershell
where powershell.exe >nul 2>nul
if errorlevel 1 goto powershell_missing
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%START_PS1%" %*
exit /b %ERRORLEVEL%

:powershell_missing
echo Could not find pwsh.exe or powershell.exe.
echo Install PowerShell, then rerun this script.
exit /b 1
