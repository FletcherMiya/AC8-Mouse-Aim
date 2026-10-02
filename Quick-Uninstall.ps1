param([string]$GamePath)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
if ([string]::IsNullOrWhiteSpace($GamePath)) {
    $picker = New-Object System.Windows.Forms.FolderBrowserDialog
    $picker.Description = 'Select ACE COMBAT 8 root folder (contains Game and EasyAntiCheat).'
    $picker.ShowNewFolderButton = $false
    try {
        if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
        $GamePath = $picker.SelectedPath
    } finally { $picker.Dispose() }
}
$game = (Get-Item -LiteralPath $GamePath).FullName.TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $game 'Game\Binaries\Win64\AceCombat8.exe') -PathType Leaf)) {
    throw 'Not the game root. Select ACE COMBAT 8, not Game or Win64.'
}
if (Get-Process -Name 'AceCombat8' -ErrorAction SilentlyContinue) { throw 'Close ACE COMBAT 8 first.' }
$modRelative = 'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim'
if (-not (Test-Path -LiteralPath (Join-Path $game $modRelative) -PathType Container)) {
    throw 'AC8MouseAim was not found. No files were changed.'
}
# Validate every ancestor and descendant before moving anything. Refuse links
# or junctions so an unexpected reparse point cannot redirect the operation.
function Assert-SafePath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path outside selected game: $full"
    }
    $cursor = $full
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Link/junction refused: $cursor" }
        }
        $cursor = Split-Path -Parent $cursor
    }
}
$targets = @($modRelative, 'EasyAntiCheat\AC8MouseAim_Offline.json',
    'Launch-AC8-Mouse-Aim.cmd', 'Disable-Mod-For-Multiplayer.cmd', 'Uninstall-AC8-Mouse-Aim.ps1')
$mods = Join-Path $game 'Game\Binaries\Win64\UE4SS\Mods'
Assert-SafePath $mods
# Conservatively preserve a shared loader whenever another mod folder exists.
$others = @(Get-ChildItem -LiteralPath $mods -Directory -Force | Where-Object Name -ne 'AC8MouseAim')
if ($others.Count -eq 0) {
    $known = @('C5D2AB9F9B89BD94460B0A283EEFB113085105014011CAC961F36787376DB744',
        'CF440B9EB8643BB7C434ACFDA696AEE57FD981D185DCA5E57FB8DBB18F8FC1CD')
    foreach ($relative in @('Game\Binaries\Win64\dwmapi.dll', 'Game\Binaries\Win64\dwmapi.dll.disabled')) {
        $path = Join-Path $game $relative
        Assert-SafePath $path
        if (Test-Path -LiteralPath $path) {
            if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -notin $known) {
                throw 'Unrecognized loader found. No files were changed.'
            }
            $targets += $relative
        }
    }
}
foreach ($relative in $targets) {
    $path = Join-Path $game $relative
    Assert-SafePath $path
    if (Test-Path -LiteralPath $path -PathType Container) {
        foreach ($child in Get-ChildItem -LiteralPath $path -Recurse -Force) { Assert-SafePath $child.FullName }
    }
}
$answer = [System.Windows.Forms.MessageBox]::Show(
    "Uninstall AC8MouseAim from:`n$game`n`nFiles will be moved to a backup. Saves are untouched.",
    'Uninstall AC8MouseAim', 'YesNo', 'Question')
if ($answer -ne [System.Windows.Forms.DialogResult]::Yes) { exit 0 }
$backup = Join-Path $game ('AC8MouseAim-Uninstall-Backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
Assert-SafePath $backup
New-Item -ItemType Directory -Path $backup | Out-Null
Write-Host "Backup: $backup"
foreach ($relative in $targets) {
    $source = Join-Path $game $relative
    if (-not (Test-Path -LiteralPath $source)) { continue }
    $destination = Join-Path $backup $relative
    Assert-SafePath $source
    Assert-SafePath $destination
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Move-Item -LiteralPath $source -Destination $destination
}
Write-Host 'AC8MouseAim uninstalled. Saves and game assets were not changed.' -ForegroundColor Green
if ($others.Count) { Write-Host 'Other mod folders detected: shared UE4SS loader was preserved.' }
else { Write-Host 'The loader was removed. The unused UE4SS framework remains on disk but cannot load through it.' }
Write-Host 'IMPORTANT: Remove the AC8MouseAim offline command from Steam > Properties > Launch Options.' -ForegroundColor Yellow
Write-Host 'To restore, close the game and reinstall the mod; the backup also preserves your old files and logs.'
Read-Host 'Press Enter to close'
