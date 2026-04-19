@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0install-acestep.ps1" %*
