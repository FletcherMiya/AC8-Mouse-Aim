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
  if exist "Game\Binaries\Win64\dwmapi.dll" (
    echo Both loader files exist. Resolve this manually; nothing was replaced.
    pause
    exit /b 1
  )
  echo This enables shared UE4SS and all other enabled mods using this loader.
  choice /C YN /N /M "Enable the shared loader? [Y/N] "
  if errorlevel 2 exit /b 0
  move /y "Game\Binaries\Win64\dwmapi.dll.disabled" "Game\Binaries\Win64\dwmapi.dll" >nul
)
if not exist "Game\Binaries\Win64\dwmapi.dll" (
  echo No supported dwmapi loader found. If you use a custom loader, use its existing Steam startup workflow.
  pause
  exit /b 1
)
set "EOS_USE_ANTICHEATCLIENTNULL=1"
start "" "%~dp0start_protected_game.exe" -anticheat_settings=AC8MouseAim_Offline.json
