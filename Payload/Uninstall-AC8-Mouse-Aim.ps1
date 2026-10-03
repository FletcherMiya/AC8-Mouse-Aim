param([switch]$NoPause)
$ErrorActionPreference = 'Stop'
$game = $PSScriptRoot
. (Join-Path $game 'AC8MouseAim-ModList.ps1')
if (-not (Test-Path -LiteralPath (Join-Path $game 'Game\Binaries\Win64\AceCombat8.exe'))) { throw 'Run from the game root.' }
if (Get-Process -Name AceCombat8 -ErrorAction SilentlyContinue) { throw 'Close ACE COMBAT 8 first.' }
$mods = Join-Path $game 'Game\Binaries\Win64\UE4SS\Mods'
$mod = Join-Path $mods 'AC8MouseAim'
$updates = Get-ModListUpdates $mods $false
$backup = Join-Path $game ('AC8MouseAim-Backups\uninstall-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$ownFiles = @('EasyAntiCheat\AC8MouseAim_Offline.json','Launch-AC8-Mouse-Aim.cmd','Disable-Mod-For-Multiplayer.cmd','Uninstall-AC8-Mouse-Aim.ps1','AC8MouseAim-ModList.ps1')
$targets = @($mod) + @($ownFiles | ForEach-Object { Join-Path $game $_ })
foreach ($path in $targets + @($updates.Keys) + @($backup)) {
    $resolved = [IO.Path]::GetFullPath($path)
    if (-not $resolved.StartsWith([IO.Path]::GetFullPath($game).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Uninstall path escapes the game directory.' }
    Assert-NoReparsePath $resolved
}
New-Item -ItemType Directory -Path $backup -Force | Out-Null
foreach ($path in $updates.Keys) {
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $backup (Split-Path -Leaf $path)) }
}
Write-ModListUpdates $updates
foreach ($path in $targets) {
    if (Test-Path -LiteralPath $path) { Move-Item -LiteralPath $path -Destination (Join-Path $backup (Split-Path -Leaf $path)) }
}
Write-Host 'AC8MouseAim uninstalled. UE4SS, the shared loader, framework settings and other mods were preserved.'
Write-Host "Removed files can be recovered from: $backup"
Write-Host 'Remove the AC8MouseAim offline launch option in Steam. Other mods may still be loaded; this is NOT a clean multiplayer setup.'
if (-not $NoPause) { Read-Host 'Press Enter to close' }
