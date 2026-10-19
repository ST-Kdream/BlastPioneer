# ============================================================================
#  package_release.ps1 -- build the release package (curated, verified, no junk)
#
#  WHY NOT windeployqt
#    It copies everything it thinks *might* be needed: qmltooling\ (dev tool),
#    generic\, Qt Quick3D, dxcompiler/dxil (D3D12 shader compiler), opengl32sw
#    (19.7 MB software OpenGL), every imageformat plugin, ...  A naive deploy of
#    this game measured 120 MB / 96 files, of which ~57 MB was never loaded.
#    So: we keep an explicit allowlist below, and **prove** the result runs.
#
#  PACKAGE LAYOUT (flat on purpose -- Qt looks next to the exe, no qt.conf needed)
#    BlastPioneer-<ver>\
#      BlastPioneer.exe  Updater.exe  version.txt  NOTICE.txt  LICENSE
#      <Qt DLLs>                      Qt runtime (allowlist below)
#      platforms\ styles\ imageformats\ iconengines\ tls\ networkinformation\
#      qml\{QML,QtQml,QtQuick}        only for the splash effect (QQuickWidget)
#      Assets\ Data\ ui\ mods\        content (Data/ui are also mod-overridable)
#
#  GRAPHICS POLICY
#    D3D11 only (forced in main.cpp). No opengl32sw.dll, no OpenGL/ANGLE fallback.
#    d3dcompiler_47.dll IS required (D3D11 shader compilation at runtime).
#
#  USAGE
#    pwsh -File tools\package_release.ps1
#    pwsh -File tools\package_release.ps1 -SkipBuild -Name MyBuild
#
#  WHAT IT WILL NOT DO
#    Ship your save/settings (Data\playerData.json, settings.ini, launch_state.json),
#    your logs, your bitmap cache, or *.pdb/*.ilk -- those are scrubbed below and the
#    script FAILS if any of them survives.
#
#  Keep this file ASCII-only: PowerShell 5.1 reads .ps1 in the ANSI code page.
# ============================================================================

[CmdletBinding()]
param(
    [string]$Name = "",
    [string]$OutRoot = "dist",
    [string]$Config = "Release",
    [string]$QtBin = "D:\Mygrammer\Qt\6.10.2\msvc2022_64\bin",
    [switch]$SkipBuild,
    [switch]$NoVerify
)

$ErrorActionPreference = "Stop"

$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $toolsDir
$buildDir = Join-Path $repo ("build\" + $Config)
$project = Join-Path $repo "BlastPioneer.vcxproj"
$msbuild = "D:\Mygrammer\aboutcpp\grammer\MSBuild\Current\Bin\MSBuild.exe"

# ---- single source of truth for the version: src\version.h -----------------
$versionHeader = Join-Path $repo "src\version.h"
if (-not (Test-Path $versionHeader)) { throw "src\version.h not found" }
$versionLine = Select-String -Path $versionHeader -Pattern 'BP_VERSION_STRING\s+"([^"]+)"' | Select-Object -First 1
if (-not $versionLine) { throw "BP_VERSION_STRING not found in src\version.h" }
$version = $versionLine.Matches[0].Groups[1].Value
$channelLine = Select-String -Path $versionHeader -Pattern 'BP_UPDATE_CHANNEL\s+"([^"]+)"' | Select-Object -First 1
$channel = if ($channelLine) { $channelLine.Matches[0].Groups[1].Value } else { "stable" }

if ([string]::IsNullOrWhiteSpace($Name)) { $Name = "BlastPioneer-$version" }
$target = Join-Path (Join-Path $repo $OutRoot) $Name

Write-Host "repo    : $repo"
Write-Host "version : $version ($channel)"
Write-Host "target  : $target"

# ---- build ----------------------------------------------------------------
if (-not $SkipBuild) {
    if (-not (Test-Path $msbuild)) { throw "MSBuild not found: $msbuild" }
    Write-Host "== build =="
    & $msbuild $project "/p:Configuration=$Config" /p:Platform=x64 /m /v:m /nologo
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
}

$exe = Join-Path $buildDir "BlastPioneer.exe"
if (-not (Test-Path $exe)) { throw "no BlastPioneer.exe in $buildDir -- build first" }

# ---- 1) reset target ------------------------------------------------------
if (Test-Path $target) { Remove-Item -Recurse -Force $target }
New-Item -ItemType Directory -Path $target | Out-Null

# ---- 2) our own binaries --------------------------------------------------
Write-Host "== binaries =="
Copy-Item $exe -Destination $target
# the updater is optional for now: it lands here once tools\updater is built
$updater = Join-Path $buildDir "Updater.exe"
if (Test-Path $updater) {
    Copy-Item $updater -Destination $target
    Write-Host "  Updater.exe"
} else {
    Write-Host "  (no Updater.exe yet -- skipped)"
}

# ---- 3) Qt runtime: explicit allowlist ------------------------------------
# Naming rule: if it is not in this list it does not ship. Adding a Qt module
# means adding it here *and* proving the package still runs.
$qtDlls = @(
    "Qt6Core.dll",             # everything
    "Qt6Gui.dll",              # widgets
    "Qt6Widgets.dll",
    "Qt6Network.dll",          # multiplayer + updater
    "Qt6OpenGL.dll",           # pulled in by QuickWidgets
    "Qt6Quick.dll",            # splash effect (QQuickWidget)
    "Qt6Qml.dll",
    "Qt6QmlModels.dll",
    "Qt6QmlMeta.dll",
    "Qt6QmlWorkerScript.dll",
    "Qt6QuickWidgets.dll",
    "Qt6Svg.dll",              # svg icons (mods may ship svg)
    "d3dcompiler_47.dll",      # D3D11 shader compilation (required by our policy)
    "icuuc.dll"                # unicode data for Qt6Core
)

$qtPlugins = @(
    @{ Source = "platforms";        Files = @("qwindows.dll") },
    @{ Source = "styles";           Files = @("qmodernwindowsstyle.dll") },
    @{ Source = "imageformats";     Files = @("qgif.dll", "qico.dll", "qjpeg.dll", "qsvg.dll") },
    @{ Source = "iconengines";      Files = @("qsvgicon.dll") },
    @{ Source = "tls";              Files = @("qschannelbackend.dll", "qcertonlybackend.dll") },
    @{ Source = "networkinformation"; Files = @("qnetworklistmanager.dll") }
)

# Only the modules the splash effect actually imports (startupEffect.qml:
# "import QtQuick" + "import QtQuick.Window"). Copying whole QtQuick costs
# 21.8 MB / 1540 files (Controls, Dialogs, Particles, Shapes, ... we never use),
# so this is an explicit file list, not a directory copy.
$qmlFiles = @(
    "QML\qmldir", "QML\plugins.qmltypes",
    "QtQml\qmldir", "QtQml\plugins.qmltypes", "QtQml\qmlplugin.dll",
    "QtQml\Models\qmldir", "QtQml\Models\plugins.qmltypes", "QtQml\Models\modelsplugin.dll",
    "QtQml\WorkerScript\qmldir", "QtQml\WorkerScript\plugins.qmltypes", "QtQml\WorkerScript\workerscriptplugin.dll",
    "QtQuick\qmldir", "QtQuick\plugins.qmltypes", "QtQuick\qtquick2plugin.dll",
    "QtQuick\Window\qmldir", "QtQuick\Window\quickwindow.qmltypes", "QtQuick\Window\quickwindowplugin.dll"
)

Write-Host "== Qt runtime =="
foreach ($dll in $qtDlls) {
    $from = Join-Path $QtBin $dll
    if (-not (Test-Path $from)) {
        # d3dcompiler_47 / icuuc live next to the exe in the Qt deployment, not in bin\
        $from = Join-Path $buildDir $dll
    }
    if (-not (Test-Path $from)) { throw "missing Qt file: $dll (looked in $QtBin and $buildDir)" }
    Copy-Item $from -Destination $target
}
Write-Host ("  {0} dlls" -f $qtDlls.Count)

$qtRoot = Split-Path -Parent $QtBin
foreach ($plugin in $qtPlugins) {
    $destDir = Join-Path $target $plugin.Source
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    foreach ($file in $plugin.Files) {
        $from = Join-Path (Join-Path $qtRoot ("plugins\" + $plugin.Source)) $file
        if (-not (Test-Path $from)) { throw "missing plugin: $($plugin.Source)\$file" }
        Copy-Item $from -Destination $destDir
    }
    Write-Host ("  plugins\{0}: {1}" -f $plugin.Source, ($plugin.Files -join ", "))
}

$qmlRoot = Join-Path $qtRoot "qml"
foreach ($relative in $qmlFiles) {
    $from = Join-Path $qmlRoot $relative
    if (-not (Test-Path $from)) { throw "missing qml file: $relative" }
    $destPath = Join-Path (Join-Path $target "qml") $relative
    $destDir = Split-Path -Parent $destPath
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    Copy-Item $from -Destination $destPath
}
Write-Host ("  qml: {0} files (QtQuick + QtQuick.Window only)" -f $qmlFiles.Count)

# ---- 4) content -----------------------------------------------------------
# content\<x> goes to the package ROOT as <x>\: the runtime expects Assets\ Data\
# ui\ mods\ next to the exe (unchanged since the very first version), while the
# repo keeps them grouped under content\ so the source tree stays readable.
Write-Host "== content =="
$contentDir = Join-Path $repo "content"
foreach ($item in @("Assets", "Data", "ui", "mods")) {
    $from = Join-Path $contentDir $item
    if (-not (Test-Path $from)) { throw "missing content dir: $item" }
    Copy-Item $from -Destination $target -Recurse
    Write-Host ("  {0}: {1} files" -f $item, (Get-ChildItem $from -Recurse -File).Count)
}

# ---- 5) version.txt + license --------------------------------------------
$built = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
@(
    "version=$version",
    "channel=$channel",
    "built=$built",
    "graphics=d3d11"
) | Set-Content -Path (Join-Path $target "version.txt") -Encoding ASCII

Copy-Item (Join-Path $repo "LICENSE") -Destination $target -ErrorAction SilentlyContinue

$notice = @"
BlastPioneer $version
Third-party notices

Qt 6 (QtCore/QtGui/QtWidgets/QtNetwork/QtQuick/QtQml/QtSvg)
  (c) The Qt Company. Licensed under the GNU Lesser General Public License v3
  (LGPLv3). This application links Qt dynamically; the Qt libraries are the
  unmodified upstream binaries shipped alongside this file, so you may replace
  them with your own build of the same version.
  Full license text: https://www.gnu.org/licenses/lgpl-3.0.html

d3dcompiler_47.dll
  Microsoft Direct3D shader compiler, redistributable per the Windows SDK terms.

ICU (icuuc.dll)
  (c) Unicode, Inc. Licensed under the Unicode License.

Images and level data are part of this project (see LICENSE).
"@
Set-Content -Path (Join-Path $target "NOTICE.txt") -Value $notice -Encoding UTF8

# ---- 6) scrub anything personal or useless -------------------------------
Write-Host "== scrub =="
$userFiles = @(
    "Data\playerData.json",       # your save (name / coins / exp / inventory / progress)
    "Data\settings.ini",          # your settings (theme / geometry / mod switches)
    "Data\launch_state.json",     # your crash-protection history
    "Data\mods_disabled.json"
)
foreach ($relative in $userFiles) {
    $full = Join-Path $target $relative
    if (Test-Path $full) { Remove-Item -Force $full; Write-Host "  removed $relative" }
}
foreach ($dir in @("logs", "cache")) {
    $full = Join-Path $target $dir
    if (Test-Path $full) { Remove-Item -Recurse -Force $full; Write-Host "  removed $dir\" }
}
Get-ChildItem -Path $target -Recurse -Include *.pdb, *.ilk, *.exp, *.lib, *.pyc -ErrorAction SilentlyContinue |
    ForEach-Object { Remove-Item -Force $_.FullName }
Get-ChildItem -Path $target -Recurse -Directory -Filter "__pycache__" -ErrorAction SilentlyContinue |
    ForEach-Object { Remove-Item -Recurse -Force $_.FullName }

# ---- 7) verify: the package must actually start --------------------------
$sizeMb = [math]::Round((Get-ChildItem $target -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
$fileCount = (Get-ChildItem $target -Recurse -File).Count
$leaked = @($userFiles | Where-Object { Test-Path (Join-Path $target $_) })

Write-Host "== report =="
Write-Host ("  size        : {0} MB / {1} files" -f $sizeMb, $fileCount)
Write-Host ("  levels      : {0} json" -f (Get-ChildItem (Join-Path $target "Data\levels") -Filter *.json -ErrorAction SilentlyContinue).Count)
Write-Host ("  user data   : {0}" -f $(if ($leaked.Count -eq 0) { "none (good)" } else { "LEAKED: " + ($leaked -join ", ") }))

if ($leaked.Count -ne 0) { throw "user data leaked into the package -- do not ship" }

if (-not $NoVerify) {
    Write-Host "== verify: run the packaged exe =="
    $verifyRoot = Join-Path $env:TEMP ("bp-pkg-verify-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
    New-Item -ItemType Directory -Path $verifyRoot | Out-Null
    Copy-Item (Join-Path $target "*") -Destination $verifyRoot -Recurse
    $proc = Start-Process -FilePath (Join-Path $verifyRoot "BlastPioneer.exe") `
        -ArgumentList ("--root=" + $verifyRoot), "--ui-selftest" -PassThru -Wait
    $logPath = Join-Path $verifyRoot "logs\game.log"
    $selftestOk = $false
    if (Test-Path $logPath) {
        $selftestOk = Select-String -Path $logPath -Pattern '\[selftest\]' -Quiet
        # a pruned qml\ folder fails *softly* (QQuickWidget just stays empty), so check for it
        $qmlBroken = Select-String -Path $logPath -Pattern 'is not installed|qrc:/startupEffect' -Quiet
        if ($qmlBroken) {
            Get-Content $logPath -Encoding UTF8 | Select-String -Pattern 'startupEffect|is not installed' | Write-Host
            throw "packaged qml modules are incomplete (see the log lines above)"
        }
        if (-not $selftestOk) { Get-Content $logPath -Encoding UTF8 | Select-Object -Last 6 | Write-Host }
    }
    Remove-Item -Recurse -Force $verifyRoot -ErrorAction SilentlyContinue
    if ($proc.ExitCode -ne 0 -or -not $selftestOk) {
        throw "packaged exe failed to start (exit $($proc.ExitCode)); the allowlist above is missing something"
    }
    Write-Host "  OK: exe starts, 14 screens construct"
}

Write-Host ""
Write-Host "Done. Zip it:"
Write-Host ("  Compress-Archive -Path '{0}' -DestinationPath '{0}.zip'" -f $target)
