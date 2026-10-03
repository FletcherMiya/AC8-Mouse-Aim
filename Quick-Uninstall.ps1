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
$hasMod = Test-Path -LiteralPath (Join-Path $game $modRelative) -PathType Container
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
$frameworkRelative = 'Game\Binaries\Win64\UE4SS'
$framework = Join-Path $game $frameworkRelative
$targets = @($modRelative, 'EasyAntiCheat\AC8MouseAim_Offline.json',
    'Launch-AC8-Mouse-Aim.cmd', 'Disable-Mod-For-Multiplayer.cmd', 'Uninstall-AC8-Mouse-Aim.ps1')
$mods = Join-Path $game 'Game\Binaries\Win64\UE4SS\Mods'
Assert-SafePath $mods
# Also recognize an older uninstall: its mod entry survives in mods.txt/json.
# A known framework hash alone is NOT proof that it belongs to this mod.
$others = @()
$hasEntry = $false
if (Test-Path -LiteralPath $mods) {
    $others = @(Get-ChildItem -LiteralPath $mods -Force | Where-Object { $_.Name -notin @('AC8MouseAim','mods.txt','mods.json') })
    $txt = Join-Path $mods 'mods.txt'
    $json = Join-Path $mods 'mods.json'
    foreach ($file in @($txt,$json)) { Assert-SafePath $file }
    if (Test-Path -LiteralPath $txt) {
        foreach ($line in Get-Content -LiteralPath $txt) {
            $line = ($line -split ';',2)[0].Trim()
            if (-not $line -or $line.StartsWith('#')) { continue }
            if ($line -match '^AC8MouseAim\s*:\s*[01]\s*$') { $hasEntry = $true }
            else { $others += $line }
        }
    }
    if (Test-Path -LiteralPath $json) {
        $entries = (Get-Content -LiteralPath $json -Raw | ConvertFrom-Json).mods
        foreach ($entry in $entries) {
            if ($entry.mod_name -eq 'AC8MouseAim') { $hasEntry = $true }
            else { $others += $entry }
        }
    }
}
if (-not $hasMod -and -not $hasEntry) { throw 'No identifiable AC8MouseAim installation or residue found. No files were changed.' }
$settings = Join-Path $framework 'UE4SS-settings.ini'
Assert-SafePath $settings
if (Test-Path -LiteralPath $settings) {
    foreach ($line in Get-Content -LiteralPath $settings) {
        if ($line -match '^\s*[+-]?(ModsFolderPath|ModsFolderPaths|ControllingModsTxt)\s*=\s*[^\s;]') {
            $others += 'Custom mod directories configured'
        }
    }
}
if ($others.Count -eq 0) {
    $core = Join-Path $framework 'UE4SS.dll'
    Assert-SafePath $core
    if (-not (Test-Path -LiteralPath $core -PathType Leaf) -or
        (Get-FileHash -LiteralPath $core -Algorithm SHA256).Hash -ne '680A026890ABB4D0DF2211251F8DEFC1681A584275F1521DCC0FE30AF480006F') {
        throw 'Unrecognized UE4SS framework. No files were changed.'
    }
    # Move the entire framework, not its AC8MouseAim child separately.
    $targets = @($frameworkRelative) + @($targets | Where-Object { $_ -ne $modRelative })
    $override = Join-Path $game 'Game\Binaries\Win64\override.txt'
    Assert-SafePath $override
    if (Test-Path -LiteralPath $override) {
        if ((Get-Content -LiteralPath $override -Raw).Trim() -ne 'UE4SS') {
            throw 'Unrecognized loader override.txt. No files were changed.'
        }
        $targets += 'Game\Binaries\Win64\override.txt'
    }
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
function Assert-SafeTree([string]$Path) {
    Assert-SafePath $Path
    if (Test-Path -LiteralPath $Path -PathType Container) {
        foreach ($child in Get-ChildItem -LiteralPath $Path -Force) { Assert-SafeTree $child.FullName }
    }
}
foreach ($relative in $targets) {
    $path = Join-Path $game $relative
    Assert-SafeTree $path
}
$scope = if ($others.Count) { 'Shared framework detected: only AC8MouseAim files will be removed. Shared framework remains; fresh install may still be blocked.' } else { 'AC8MouseAim, its loader, override and entire UE4SS folder will be removed from the live game directory.' }
$answer = [System.Windows.Forms.MessageBox]::Show(
    "Uninstall AC8MouseAim from:`n$game`n`n$scope`n`nFiles will be moved to a backup. Saves are untouched.",
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
else { Write-Host 'The loader, override and entire UE4SS folder were moved to backup. Fresh installation is now possible.' }
Write-Host 'IMPORTANT: Remove the AC8MouseAim offline command from Steam > Properties > Launch Options.' -ForegroundColor Yellow
Write-Host 'To restore, close the game and reinstall the mod; the backup also preserves your old files and logs.'
Read-Host 'Press Enter to close'
