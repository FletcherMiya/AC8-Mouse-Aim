# Shared installer/uninstaller helpers. Preserve other mods and their ordering.
function Get-ModListUpdates([string]$ModsPath, [bool]$Enable) {
    $updates = @{}
    $textPath = Join-Path $ModsPath 'mods.txt'
    $text = if (Test-Path -LiteralPath $textPath) { [IO.File]::ReadAllText($textPath) } else { '' }
    $text = [regex]::Replace($text, '(?im)^[\t ]*AC8MouseAim[\t ]*:[^\r\n]*(?:\r?\n|$)', '')
    if ($Enable) {
        if ($text.Length -and -not $text.EndsWith("`n")) { $text += "`r`n" }
        $text += "AC8MouseAim : 1`r`n"
    }
    if ($Enable -or (Test-Path -LiteralPath $textPath)) { $updates[$textPath] = $text }
    $jsonPath = Join-Path $ModsPath 'mods.json'
    if (Test-Path -LiteralPath $jsonPath) {
        $json = [IO.File]::ReadAllText($jsonPath) | ConvertFrom-Json -ErrorAction Stop
        if ($null -eq $json -or $null -eq $json.PSObject.Properties['mods'] -or $json.mods -isnot [array]) {
            throw 'Unrecognized mods.json format. No mod list was changed.'
        }
        $own = @($json.mods | Where-Object { $_.mod_name -eq 'AC8MouseAim' })
        $others = @($json.mods | Where-Object { $_.mod_name -ne 'AC8MouseAim' })
        if ($Enable) {
            $entry = if ($own.Count) { $own[0] } else { [pscustomobject]@{mod_name='AC8MouseAim'} }
            $entry | Add-Member -NotePropertyName mod_enabled -NotePropertyValue $true -Force
            $others += $entry
        }
        $json.mods = @($others)
        $updates[$jsonPath] = ($json | ConvertTo-Json -Depth 100)
    }
    return $updates
}
function Assert-NoReparsePath([string]$Path) {
    $part = [IO.Path]::GetFullPath($Path)
    while ($part) {
        if (Test-Path -LiteralPath $part) {
            if ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Linked paths require manual installation: $part"
            }
        }
        $parent = Split-Path -Parent $part
        if ($parent -eq $part) { break }
        $part = $parent
    }
}
function Write-ModListUpdates($Updates) {
    foreach ($path in $Updates.Keys) { [IO.File]::WriteAllText($path, $Updates[$path], (New-Object Text.UTF8Encoding($false))) }
}
