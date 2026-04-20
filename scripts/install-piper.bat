@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-piper.ps1" %*
exit /b %ERRORLEVEL%
