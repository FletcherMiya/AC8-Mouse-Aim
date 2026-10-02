@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Quick-Uninstall.ps1"
if errorlevel 1 pause
