@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0install-mojo.ps1" %*
