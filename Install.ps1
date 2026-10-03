param(
    [string]$GamePath
)
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'Payload'
function Test-GameDirectory([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    return ((Test-Path -LiteralPath (Join-Path $Path 'Game\Binaries\Win64\AceCombat8.exe') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $Path 'EasyAntiCheat') -PathType Container))
}
if ([string]::IsNullOrWhiteSpace($GamePath)) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.FolderBrowserDialog
    $picker.Description = 'Select the ACE COMBAT 8 game root folder (contains Game and EasyAntiCheat).'
    $picker.ShowNewFolderButton = $false
    try {
        while ($true) {
            if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
                Write-Host 'Installation cancelled. No game files were changed.'
                exit 0
            }
            if (Test-GameDirectory $picker.SelectedPath) {
                $GamePath = $picker.SelectedPath
                break
            }
            [void][System.Windows.Forms.MessageBox]::Show(
                'This is not the game root folder. Select ACE COMBAT 8, not its Game or Win64 subfolder.',
                'Game directory not found', 'OK', 'Warning')
        }
    } finally { $picker.Dispose() }
}
if (-not (Test-GameDirectory $GamePath)) { throw "Invalid ACE COMBAT 8 game directory: $GamePath" }
$GamePath = (Get-Item -LiteralPath $GamePath).FullName
Write-Host "Installing to: $GamePath"
$gameExe = Join-Path $GamePath 'Game\Binaries\Win64\AceCombat8.exe'
if (-not (Test-Path -LiteralPath $gameExe)) { throw "ACE COMBAT 8 was not found at: $GamePath" }
if (Get-Process -Name 'AceCombat8' -ErrorAction SilentlyContinue) { throw 'Close ACE COMBAT 8 first.' }

$loader = Join-Path $GamePath 'Game\Binaries\Win64\dwmapi.dll'
$disabledLoader = "$loader.disabled"
$ue4ss = Join-Path $GamePath 'Game\Binaries\Win64\UE4SS'
$mouseAim = Join-Path $ue4ss 'Mods\AC8MouseAim'
$isUpgrade = Test-Path -LiteralPath $mouseAim
$hasLoader = (Test-Path -LiteralPath $loader) -or (Test-Path -LiteralPath $disabledLoader) -or
    (Test-Path -LiteralPath $ue4ss)
if ($hasLoader -and -not $isUpgrade) {
    throw 'An unrelated UE4SS/dwmapi installation was found. It was not overwritten.'
}

if ($isUpgrade) {
    $knownLoaderHashes = @(
        'C5D2AB9F9B89BD94460B0A283EEFB113085105014011CAC961F36787376DB744',
        'CF440B9EB8643BB7C434ACFDA696AEE57FD981D185DCA5E57FB8DBB18F8FC1CD'
    )
    $installedLoader = if (Test-Path -LiteralPath $loader) { $loader } else { $disabledLoader }
    if (-not (Test-Path -LiteralPath $installedLoader)) {
        throw 'The existing AC8MouseAim installation has no recognized loader to repair.'
    }
    $installedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $installedLoader).Hash
    if ($installedHash -notin $knownLoaderHashes) {
        throw 'The existing loader was modified or belongs to another package. It was not overwritten.'
    }

    $backup = Join-Path $GamePath 'AC8MouseAim-Loader-Backup-0.1.0'
    if (-not (Test-Path -LiteralPath $backup)) {
        New-Item -ItemType Directory -Path $backup | Out-Null
        Copy-Item -LiteralPath $installedLoader -Destination (Join-Path $backup 'dwmapi.dll')
        $oldCore = Join-Path $ue4ss 'UE4SS.dll'
        if (Test-Path -LiteralPath $oldCore) {
            Copy-Item -LiteralPath $oldCore -Destination (Join-Path $backup 'UE4SS.dll')
        }
    }
    if (Test-Path -LiteralPath $disabledLoader) {
        Remove-Item -LiteralPath $disabledLoader -Force
    }
}

$gameSource = Join-Path $source 'Game\*'
$gameDestination = Join-Path $GamePath 'Game'
Copy-Item -Path $gameSource -Destination $gameDestination -Recurse -Force
Copy-Item -LiteralPath (Join-Path $source 'EasyAntiCheat\AC8MouseAim_Offline.json') `
    -Destination (Join-Path $GamePath 'EasyAntiCheat\AC8MouseAim_Offline.json')
foreach ($name in @('Launch-AC8-Mouse-Aim.cmd','Disable-Mod-For-Multiplayer.cmd','Uninstall-AC8-Mouse-Aim.ps1')) {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $GamePath $name)
}
# Verify the actual files, not just whether Copy-Item returned successfully.
foreach ($relative in @(
    'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim\Scripts\ac8_mouse_aim_010.dll',
    'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim\Scripts\main.lua',
    'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim\Scripts\camera.lua',
    'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim\Scripts\gaze_probe.lua',
    'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim\Scripts\gaze.lua',
    'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim\Scripts\rig_math.lua',
    'Game\Binaries\Win64\UE4SS\Mods\AC8MouseAim\config.ini'
)) {
    $expected = (Get-FileHash -LiteralPath (Join-Path $source $relative) -Algorithm SHA256).Hash
    $actual = (Get-FileHash -LiteralPath (Join-Path $GamePath $relative) -Algorithm SHA256).Hash
    if ($actual -ne $expected) { throw "Installation verification failed: $relative" }
}
Write-Host 'VERIFIED: game files match this 0.2.29 release.' -ForegroundColor Green
Write-Host 'Hold F for free look; release F to return to the flight target.'
if ($isUpgrade) { Write-Host 'The startup loader was repaired and the old files were backed up.' }
Write-Host 'Start ACE COMBAT 8 through Steam with the existing offline launch option.'
Write-Host 'Before multiplayer, run Disable-Mod-For-Multiplayer.cmd and then launch from Steam.'
Read-Host 'Press Enter to close'
