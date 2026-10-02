@echo off
setlocal
cd /d "%~dp0"
tasklist /FI "IMAGENAME eq AceCombat8.exe" 2>nul | find /I "AceCombat8.exe" >nul
if not errorlevel 1 (
  echo ACE COMBAT 8 is already running.
  pause
  exit /b 1
)
if exist "Game\Binaries\Win64\dwmapi.dll.disabled" (
  move /y "Game\Binaries\Win64\dwmapi.dll.disabled" "Game\Binaries\Win64\dwmapi.dll" >nul
)
if not exist "Game\Binaries\Win64\dwmapi.dll" (
  echo The UE4SS loader is missing. Re-run Install.cmd.
  pause
  exit /b 1
)
set "EOS_USE_ANTICHEATCLIENTNULL=1"
start "" "%~dp0start_protected_game.exe" -anticheat_settings=AC8MouseAim_Offline.json
