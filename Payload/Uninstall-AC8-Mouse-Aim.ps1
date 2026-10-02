$ErrorActionPreference = 'Stop'
$game = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $game 'Game\Binaries\Win64\AceCombat8.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'This script is not in the ACE COMBAT 8 installation root.' }
if (Get-Process -Name 'AceCombat8' -ErrorAction SilentlyContinue) { throw 'Close ACE COMBAT 8 first.' }

$targets = @(
    'Game\Binaries\Win64\dwmapi.dll',
    'Game\Binaries\Win64\dwmapi.dll.disabled',
    'Game\Binaries\Win64\override.txt',
    'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim',
    'EasyAntiCheat\AC8MouseAim_Offline.json',
    'Launch-AC8-Mouse-Aim.cmd',
    'Disable-Mod-For-Multiplayer.cmd',
    'Uninstall-AC8-Mouse-Aim.ps1'
)
foreach ($relative in $targets) {
    $target = Join-Path $game $relative
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
}
Write-Host 'AC8 Mouse Aim and its loader entry have been removed.'
Write-Host 'The inert UE4SS framework folder was preserved in case another offline mod uses it.'
Read-Host 'Press Enter to close'
