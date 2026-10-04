param([string]$GamePath, [switch]$NoPause)
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'Payload'
. (Join-Path $PSScriptRoot 'ModList.ps1')
function Test-GameDirectory([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    return ((Test-Path -LiteralPath (Join-Path $Path 'Game\Binaries\Win64\AceCombat8.exe') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $Path 'EasyAntiCheat') -PathType Container))
}
if ([string]::IsNullOrWhiteSpace($GamePath)) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.FolderBrowserDialog
    $picker.Description = 'Select the ACE COMBAT 8 root folder (contains Game and EasyAntiCheat).'
    $picker.ShowNewFolderButton = $false
    try {
        while ($true) {
            if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
            if (Test-GameDirectory $picker.SelectedPath) { $GamePath = $picker.SelectedPath; break }
            [void][System.Windows.Forms.MessageBox]::Show('Select the game root, not Game or Win64.')
        }
    } finally { $picker.Dispose() }
}
if (-not (Test-GameDirectory $GamePath)) { throw 'Invalid game directory.' }
if (Get-Process -Name AceCombat8 -ErrorAction SilentlyContinue) { throw 'Close ACE COMBAT 8 first.' }
$GamePath = (Get-Item -LiteralPath $GamePath).FullName
$bin = Join-Path $GamePath 'Game\Binaries\Win64'
$ue4ss = Join-Path $bin 'UE4SS'
$mods = Join-Path $ue4ss 'Mods'
$mod = Join-Path $mods 'AC8MouseAim'
$core = Join-Path $ue4ss 'UE4SS.dll'
$coreHash = '680A026890ABB4D0DF2211251F8DEFC1681A584275F1521DCC0FE30AF480006F'
$sourceCore = Join-Path $source 'Game\Binaries\Win64\UE4SS\UE4SS.dll'
if ((Get-FileHash -LiteralPath $sourceCore).Hash -ne $coreHash) { throw 'Package UE4SS runtime does not match the mod ABI.' }
# Complete preflight before any writes. Custom layouts remain manual, not overwritten.
foreach ($path in @($bin,$ue4ss,$mods,$mod,(Join-Path $GamePath 'EasyAntiCheat'),(Join-Path $GamePath 'AC8MouseAim-Backups'))) {
    Assert-NoReparsePath $path
}
if (Test-Path -LiteralPath $mod) {
    if (Get-ChildItem -LiteralPath $mod -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
        throw 'The existing mod contains linked files. Use manual installation.'
    }
}
$reuse = Test-Path -LiteralPath $core -PathType Leaf
if ($reuse) {
    if ((Get-FileHash -LiteralPath $core).Hash -ne $coreHash) {
        throw 'Existing UE4SS is not the validated runtime for this mod. Nothing was replaced. Use a compatible mod build or arrange a framework upgrade separately.'
    }
    if (Test-Path -LiteralPath (Join-Path $bin 'UE4SS.dll')) {
        throw 'Multiple UE4SS runtime layouts found. Select the active framework manually; nothing was replaced.'
    }
    $overridePath = Join-Path $bin 'override.txt'
    if ((Test-Path -LiteralPath $overridePath) -and [IO.File]::ReadAllText($overridePath).Trim() -ne 'UE4SS') {
        throw 'Custom loader redirection found in override.txt. Use manual integration; nothing was replaced.'
    }
    $settingsPath = Join-Path $ue4ss 'UE4SS-settings.ini'
    if (Test-Path -LiteralPath $settingsPath) {
        $settings = [IO.File]::ReadAllText($settingsPath)
        if ($settings -match '(?im)^\s*[+\-]?(ModsFolderPath|ModsFolderPaths|ControllingModsTxt)\s*=[\t ]*[^\s;\r\n]') {
            throw 'Custom mod search paths detected. Use manual installation; existing settings were preserved.'
        }
        if ($settings -match '(?im)^\s*HookEngineTick\s*=[\t ]*(0|false)[\t ]*(?:;.*)?\r?$') {
            throw 'This mod requires HookEngineTick. Existing settings were not changed.'
        }
    }
} else {
    foreach ($relative in @('UE4SS','UE4SS.dll','dwmapi.dll','dwmapi.dll.disabled','override.txt','winhttp.dll','version.dll','dinput8.dll','xinput1_3.dll','dxgi.dll')) {
        if (Test-Path -LiteralPath (Join-Path $bin $relative)) {
            throw "An unrecognized framework/loader layout exists ($relative). It was not overwritten. Manual integration is required."
        }
    }
}
$updates = Get-ModListUpdates $mods $true
$modSource = Join-Path $source 'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim'
$keepConfig = Test-Path -LiteralPath (Join-Path $mod 'config.ini')
$rootFiles = @('Launch-AC8-Mouse-Aim.cmd','Disable-Mod-For-Multiplayer.cmd','Uninstall-AC8-Mouse-Aim.ps1','AC8MouseAim-ModList.ps1')
foreach ($name in $rootFiles) {
    $from = if ($name -eq 'AC8MouseAim-ModList.ps1') { Join-Path $PSScriptRoot 'ModList.ps1' } else { Join-Path $source $name }
    if (-not (Test-Path -LiteralPath $from -PathType Leaf)) { throw "Incomplete package: $name" }
}
foreach ($path in @($updates.Keys) + @((Join-Path $GamePath 'EasyAntiCheat\AC8MouseAim_Offline.json')) + @($rootFiles | ForEach-Object { Join-Path $GamePath $_ })) {
    Assert-NoReparsePath $path
}
$backup = Join-Path $GamePath ('AC8MouseAim-Backups\install-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
if (Test-Path -LiteralPath $mod) { Copy-Item -LiteralPath $mod -Destination (Join-Path $backup 'AC8MouseAim') -Recurse }
foreach ($path in $updates.Keys) {
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $backup (Split-Path -Leaf $path)) }
}
foreach ($name in $rootFiles) {
    $path = Join-Path $GamePath $name
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $backup }
}
$offline = Join-Path $GamePath 'EasyAntiCheat\AC8MouseAim_Offline.json'
if (Test-Path -LiteralPath $offline) { Copy-Item -LiteralPath $offline -Destination $backup }
if (-not $reuse) {
    Copy-Item -Path (Join-Path $source 'Game\*') -Destination (Join-Path $GamePath 'Game') -Recurse -Force
} else {
    New-Item -ItemType Directory -Path $mod -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $modSource -Force) {
        if ($keepConfig -and $item.Name -eq 'config.ini') { continue }
        Copy-Item -LiteralPath $item.FullName -Destination $mod -Recurse -Force
    }
}
Write-ModListUpdates $updates
Copy-Item -LiteralPath (Join-Path $source 'EasyAntiCheat\AC8MouseAim_Offline.json') -Destination $offline -Force
foreach ($name in $rootFiles) {
    $from = if ($name -eq 'AC8MouseAim-ModList.ps1') { Join-Path $PSScriptRoot 'ModList.ps1' } else { Join-Path $source $name }
    Copy-Item -LiteralPath $from -Destination (Join-Path $GamePath $name) -Force
}
foreach ($file in Get-ChildItem -LiteralPath $modSource -Recurse -File) {
    $relative = $file.FullName.Substring($modSource.Length).TrimStart('\')
    if ($keepConfig -and $relative -eq 'config.ini') { continue }
    if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $mod $relative)).Hash) { throw "Mod verification failed: $relative" }
}
if ((Get-FileHash -LiteralPath $core).Hash -ne $coreHash) { throw 'Runtime verification failed.' }
Write-Host 'VERIFIED: AC8MouseAim 0.2.30-pw.5 (contribution build), coexist installer r1.' -ForegroundColor Green
if ($reuse) {
    Write-Host 'Reused compatible UE4SS. Loader, framework settings, backends and other mod folders were not overwritten.'
    Write-Host 'Your existing loader must already work; disabled or custom loaders were NOT enabled or repaired.'
} else { Write-Host 'Installed the bundled UE4SS framework.' }
if ($keepConfig) { Write-Host 'Preserved existing config.ini.' }
Write-Host "Backup: $backup"
Write-Host 'Uninstall now removes only this mod and preserves the shared framework.'
Write-Host 'Installation coexistence does not guarantee camera/input mod compatibility. Offline single-player only.'
if (-not $NoPause) { Read-Host 'Press Enter to close' }
