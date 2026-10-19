# ============================================================================
#  test_updater.ps1 -- end-to-end test for Updater.exe (no internet needed)
#
#  WHAT IT PROVES
#    1. up-to-date      -> "already latest", exit 0, nothing touched
#    2. differential    -> ONLY changed files are fetched (the local HTTP server
#                          logs every request, so this is measured, not assumed)
#    3. backup          -> overwritten/deleted files land in Update\backup\<oldver>\
#    4. apply           -> new file appears, deleted file disappears, version.txt updated
#    5. rollback        -> if the new BlastPioneer.exe cannot run, the update is
#                          reverted and the old exe works again
#    6. path safety     -> a manifest with "..\..\evil" entries is rejected outright
#
#  HOW
#    A PowerShell HttpListener serves a temp "mirror" tree on 127.0.0.1:<port>.
#    Two payloads are generated from dist\ (v0.1.0 as "installed", v0.1.1 as "server").
#
#  USAGE
#      pwsh -File tools\test_updater.ps1
#      pwsh -File tools\test_updater.ps1 -KeepTemp     (leave the sandbox for inspection)
#
#  Keep this file ASCII-only (PowerShell 5.1 reads .ps1 in the ANSI code page).
# ============================================================================

[CmdletBinding()]
param(
    [string]$Port = "8099",
    [switch]$KeepTemp
)

$ErrorActionPreference = "Stop"

$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $toolsDir
$distRoot = Join-Path $repo "dist"
$work = Join-Path $env:TEMP ("bp-updater-test-" + [guid]::NewGuid().ToString("N").Substring(0, 8))

$failures = 0
function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Host ("  [OK]   " + $what) }
    else { Write-Host ("  [FAIL] " + $what); $script:failures++ }
}

function New-VersionedPackage([string]$from, [string]$to) {
    if (Test-Path $to) { Remove-Item -Recurse -Force $to }
    Copy-Item $from $to -Recurse
    foreach ($relative in @("Data/playerData.json", "Data/settings.ini", "Data/launch_state.json")) {
        $full = Join-Path $to $relative.Replace('/', '\')
        if (Test-Path $full) { Remove-Item -Force $full }
    }
    foreach ($dir in @("logs", "cache", "Update")) {
        $full = Join-Path $to $dir
        if (Test-Path $full) { Remove-Item -Recurse -Force $full }
    }
    return $to
}

# ---- 0) prerequisites -----------------------------------------------------
Write-Host "== 0) prerequisites =="
$basePackage = Get-ChildItem $distRoot -Directory -Filter "BlastPioneer-*" -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $basePackage) { throw "no package in dist\ -- run tools\package_release.ps1 first" }
$updaterExe = Join-Path (Join-Path $repo "build\Release") "Updater.exe"
if (-not (Test-Path $updaterExe)) { throw "Updater.exe not built -- build tools\updater\Updater.vcxproj first" }
Write-Host ("  base package : {0}" -f $basePackage.Name)
Check $true "found package and Updater.exe"

New-Item -ItemType Directory -Path $work | Out-Null
$server = Join-Path $work "server"
$servedFiles = Join-Path $server "files"
New-Item -ItemType Directory -Path $servedFiles | Out-Null

# ---- 1) installed v0.1.0 --------------------------------------------------
Write-Host "== 1) prepare installed 0.1.0 + server payload 0.1.1 =="
$installed = New-VersionedPackage $basePackage.FullName (Join-Path $work "installed")
Copy-Item (Join-Path (Join-Path $repo "build\Release") "Updater.exe") $installed -Force
$payload = New-VersionedPackage $basePackage.FullName (Join-Path $work "payload-0.1.1")

# what changes in 0.1.1: one modified file, one added file, one deleted file
Add-Content -Path (Join-Path $payload "ui\light.qss") -Value "/* test: 0.1.1 */"
Set-Content -Path (Join-Path $payload "Data\notes.txt") -Value "added in 0.1.1" -Encoding ASCII
Remove-Item (Join-Path $payload "ui\dark.qss") -Force

# build the manifest while the payload still contains all "files" entries
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $toolsDir "make_manifest.ps1") `
    -Dir $payload -Version "0.1.1" -OutFile (Join-Path $server "manifest.json") -Delete @("ui/dark.qss") | Out-Host
Copy-Item (Join-Path $payload "*") -Destination $servedFiles -Recurse -Force
Check (Test-Path (Join-Path $server "manifest.json")) "manifest.json generated"

# ---- 2) local HTTP mirror -------------------------------------------------
Write-Host "== 2) start local mirror on 127.0.0.1:$Port =="
$requestLog = Join-Path $work "requests.log"
Set-Content -Path $requestLog -Value "" -Encoding ASCII

$listenerJob = Start-Job -ScriptBlock {
    param($Root, $Port, $LogPath)
    $listener = New-Object System.Net.HttpListener
    $listener.Prefixes.Add("http://127.0.0.1:$Port/")
    $listener.Start()
    $deadline = (Get-Date).AddMinutes(6)
    while ((Get-Date) -lt $deadline) {
        $context = $listener.GetContext()
        $relative = $context.Request.Url.AbsolutePath.TrimStart('/').Replace('/', '\')
        Add-Content -Path $LogPath -Value $relative
        $file = Join-Path $Root $relative
        if (Test-Path $file -PathType Leaf) {
            $bytes = [System.IO.File]::ReadAllBytes($file)
            $context.Response.StatusCode = 200
            $context.Response.ContentLength64 = $bytes.Length
            $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        } else {
            $context.Response.StatusCode = 404
        }
        $context.Response.OutputStream.Close()
    }
    $listener.Stop()
} -ArgumentList $server, $Port, $requestLog

Start-Sleep -Seconds 2
Check ($listenerJob.State -eq 'Running') "mirror is listening"

$mirror = "http://127.0.0.1:$Port"

# ---- 3) up-to-date check --------------------------------------------------
Write-Host "== 3) already up to date (server still offers 0.1.0) =="
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $toolsDir "make_manifest.ps1") `
    -Dir $installed -Version "0.1.0" -OutFile (Join-Path $server "manifest.json") | Out-Null
Remove-Item (Join-Path $servedFiles "*") -Recurse -Force
Set-Content -Path $requestLog -Value "" -Encoding ASCII
$proc = Start-Process -FilePath (Join-Path $installed "Updater.exe") `
    -ArgumentList ("--root=" + $installed), ("--mirror=" + $mirror), "--check" `
    -PassThru -Wait -NoNewWindow
Check ($proc.ExitCode -eq 0) ("up-to-date check exits 0 (actual " + $proc.ExitCode + ")")
$requests = @(Get-Content $requestLog | Where-Object { $_ -ne "" })
Check (($requests | Where-Object { $_ -like "files\*" }).Count -eq 0) "no file downloads when already up to date"

# ---- 4) differential update ----------------------------------------------
Write-Host "== 4) differential update 0.1.0 -> 0.1.1 =="
# (step 3 replaced the served manifest with a 0.1.0 one; put the 0.1.1 payload back)
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $toolsDir "make_manifest.ps1") `
    -Dir $payload -Version "0.1.1" -OutFile (Join-Path $server "manifest.json") -Delete @("ui/dark.qss") | Out-Null
Remove-Item (Join-Path $servedFiles "*") -Recurse -Force
Copy-Item (Join-Path $payload "*") -Destination $servedFiles -Recurse -Force
Set-Content -Path $requestLog -Value "" -Encoding ASCII
$proc = Start-Process -FilePath (Join-Path $installed "Updater.exe") `
    -ArgumentList ("--root=" + $installed), ("--mirror=" + $mirror), "--yes" `
    -PassThru -Wait -NoNewWindow
$log = Join-Path $installed "logs\updater.log"
Check ($proc.ExitCode -eq 0) ("updater exit code 0 (actual " + $proc.ExitCode + ")")

$requests = @(Get-Content $requestLog | Where-Object { $_ -ne "" })
Write-Host ("  requests: " + ($requests -join ", "))
Check ($requests -contains "manifest.json") "fetched manifest.json"

$expectedChanged = @("ui\light.qss", "Data\notes.txt")
foreach ($relative in $expectedChanged) {
    $hit = $requests | Where-Object { $_ -eq ("files\" + $relative) }
    Check ($null -ne $hit) ("fetched changed file: " + $relative)
}
$untouched = $requests | Where-Object { $_ -like "files\BlastPioneer.exe" }
Check ($null -eq $untouched) "did NOT re-download the (unchanged) 10 MB exe"

# applied?
Check ((Test-Path (Join-Path $installed "Data\notes.txt"))) "new file applied"
Check (-not (Test-Path (Join-Path $installed "ui\dark.qss"))) "deleted file removed"
$versionText = Get-Content (Join-Path $installed "version.txt") -Raw
Check ($versionText -match "version=0.1.1") "version.txt now says 0.1.1"

# backup?
$backup = Join-Path $installed "Update\backup"
$backupDirs = @(Get-ChildItem $backup -Directory -ErrorAction SilentlyContinue)
Check ($backupDirs.Count -ge 1) "backup directory created"
if ($backupDirs.Count -ge 1) {
    $backupRoot = $backupDirs[0].FullName
    Check ((Test-Path (Join-Path $backupRoot "ui\light.qss"))) "old light.qss backed up"
    Check ((Test-Path (Join-Path $backupRoot "ui\dark.qss"))) "deleted dark.qss backed up"
}

# ---- 5) rollback on a broken payload -------------------------------------
Write-Host "== 5) rollback: ship a broken BlastPioneer.exe =="
$broken = New-VersionedPackage $basePackage.FullName (Join-Path $work "payload-0.1.2")
Set-Content -Path (Join-Path $broken "BlastPioneer.exe") -Value "this is not an executable" -Encoding ASCII
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $toolsDir "make_manifest.ps1") `
    -Dir $broken -Version "0.1.2" -OutFile (Join-Path $server "manifest.json") | Out-Host
Remove-Item (Join-Path $servedFiles "*") -Recurse -Force
Copy-Item (Join-Path $broken "*") -Destination $servedFiles -Recurse -Force

$proc = Start-Process -FilePath (Join-Path $installed "Updater.exe") `
    -ArgumentList ("--root=" + $installed), ("--mirror=" + $mirror), "--yes" `
    -PassThru -Wait -NoNewWindow
Check ($proc.ExitCode -ne 0) ("updater reports failure (exit " + $proc.ExitCode + ")")

# the old exe must be back and runnable
$selftest = Start-Process -FilePath (Join-Path $installed "BlastPioneer.exe") `
    -ArgumentList ("--root=" + $installed), "--ui-selftest" -PassThru -Wait -NoNewWindow
Check ($selftest.ExitCode -eq 0) ("rolled-back exe still runs (selftest exit " + $selftest.ExitCode + ")")
$versionText = Get-Content (Join-Path $installed "version.txt") -Raw
Check ($versionText -match "version=0.1.1") "version.txt stayed at 0.1.1 after rollback"

# ---- 6) path safety -------------------------------------------------------
Write-Host "== 6) path safety: manifest trying to escape the package =="
$evilManifest = [ordered]@{
    schema     = 1
    version    = "9.9.9"
    channel    = "stable"
    minUpdater = "0.1.0"
    files      = @(
        [ordered]@{ path = "../evil.txt"; size = 3; sha256 = ("a" * 64) }
    )
    delete     = @()
}
$evilManifest | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $server "manifest.json") -Encoding UTF8

$proc = Start-Process -FilePath (Join-Path $installed "Updater.exe") `
    -ArgumentList ("--root=" + $installed), ("--mirror=" + $mirror), "--yes" `
    -PassThru -Wait -NoNewWindow
Check ($proc.ExitCode -ne 0) ("evil manifest rejected (exit " + $proc.ExitCode + ")")
Check (-not (Test-Path (Join-Path (Split-Path $installed -Parent) "evil.txt"))) "nothing written outside the package"

# ---- 7) cleanup -----------------------------------------------------------
Stop-Job $listenerJob -ErrorAction SilentlyContinue
Remove-Job $listenerJob -Force -ErrorAction SilentlyContinue

Write-Host ""
if ($KeepTemp) { Write-Host ("sandbox kept: " + $work) }
else { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }

Write-Host ("updater tests: {0} check(s) failed" -f $failures)
if ($failures -ne 0) { exit 1 }
exit 0
