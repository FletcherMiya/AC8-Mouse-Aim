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
  echo WARNING: This disables the shared dwmapi loader and ALL mods using it.
  echo Custom loaders and other injection methods are not handled.
  choice /C YN /N /M "Disable this shared loader? [Y/N] "
  if errorlevel 2 exit /b 0
  if exist "Game\Binaries\Win64\dwmapi.dll.disabled" (
    echo Both active and disabled loader files exist. Resolve this manually.
    pause
    exit /b 1
  )
  move /y "Game\Binaries\Win64\dwmapi.dll" "Game\Binaries\Win64\dwmapi.dll.disabled" >nul
)
echo Remove the offline Steam launch option before returning to normal play.
echo Verify ALL other mod loaders are disabled before multiplayer. This script cannot guarantee that.
pause
