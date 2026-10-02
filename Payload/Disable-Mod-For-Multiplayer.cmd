@echo off
setlocal
cd /d "%~dp0"
tasklist /FI "IMAGENAME eq AceCombat8.exe" 2>nul | find /I "AceCombat8.exe" >nul
if not errorlevel 1 (
  echo Close ACE COMBAT 8 before disabling the loader.
  pause
  exit /b 1
)
if exist "Game\Binaries\Win64\dwmapi.dll" (
  move /y "Game\Binaries\Win64\dwmapi.dll" "Game\Binaries\Win64\dwmapi.dll.disabled" >nul
)
echo The mod loader is disabled. You can now launch the original game from Steam.
pause
