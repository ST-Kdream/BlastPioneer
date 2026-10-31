# ============================================================================
#  make_manifest.ps1 -- generate the differential-update manifest for a package
#
#  The updater (Updater.exe) compares every file's sha256 against this manifest,
#  so only changed files are downloaded. See docs\Updater design for the format.
#
#  LAYOUT PRODUCED BY THE PUBLISHER (mirror and GitHub raw branch are identical)
#      <base>\manifest.json
#      <base>\files\<relative path>          <- the package tree, copied as-is
#
#  USAGE
#      pwsh -File tools\make_manifest.ps1 -Dir dist\BlastPioneer-0.1.1 -Version 0.1.1
#      # -> dist\BlastPioneer-0.1.1.manifest.json  (+ copy it into the served tree)
#
#  NOTES
#    · version.txt is NOT listed: the updater writes it itself after a successful
#      update. Listing it would make the updater fight itself on every run.
#    · Update\ (staging/backup) and logs\ are excluded: that is runtime state.
#    · Keep this file ASCII-only (PowerShell 5.1 reads .ps1 in the ANSI code page).
# ============================================================================

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Channel = "stable",
    [string]$OutFile = "",
    [string]$MinUpdater = "0.1.0",
    [string[]]$Delete = @()
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Dir)) { throw "package dir not found: $Dir" }
$root = (Resolve-Path $Dir).Path
if ([string]::IsNullOrWhiteSpace($OutFile)) { $OutFile = "$root.manifest.json" }

$excludePrefixes = @("Update\", "logs\", "cache\")
$files = @()

Get-ChildItem -Path $root -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
    foreach ($prefix in $excludePrefixes) {
        if ($relative.StartsWith($prefix)) { return }
    }
    if ($relative -eq "version.txt" -or $relative -eq "manifest.json") { return }

    $hash = (Get-FileHash -Path $_.FullName -Algorithm SHA256).Hash.ToLower()
    $files += [ordered]@{
        path   = $relative
        size   = $_.Length
        sha256 = $hash
    }
}

$manifest = [ordered]@{
    schema      = 1
    version     = $Version
    channel     = $Channel
    publishedAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    minUpdater  = $MinUpdater
    files       = $files
    delete      = $Delete
}

$json = $manifest | ConvertTo-Json -Depth 6
# 必须**无 BOM**：Qt 的 QJsonDocument 不接受 BOM，带 BOM 的清单会让更新器直接判"清单不合法"
[System.IO.File]::WriteAllText($OutFile, $json, (New-Object System.Text.UTF8Encoding($false)))

$totalMb = [math]::Round(($files | ForEach-Object { $_.size } | Measure-Object -Sum).Sum / 1MB, 1)
Write-Host ("manifest  : {0}" -f $OutFile)
Write-Host ("version   : {0} ({1})" -f $Version, $Channel)
Write-Host ("files     : {0} ({1} MB)" -f $files.Count, $totalMb)
Write-Host ("delete    : {0}" -f $(if ($Delete.Count -eq 0) { "none" } else { $Delete -join ", " }))
Write-Host ""
Write-Host "publish:"
Write-Host ("  copy '{0}\*'                        -> <mirrorBase>\files\" -f $root)
Write-Host ("  copy '{0}' -> <mirrorBase>\manifest.json" -f $OutFile)
