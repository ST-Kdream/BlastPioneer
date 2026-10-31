; ============================================================================
;  BlastPioneer installer (Inno Setup 6)
;
;  DESIGN: the installer and the portable (green) build have the SAME layout.
;  It simply copies dist\BlastPioneer-<ver>\* into %LOCALAPPDATA%\Programs\BlastPioneer.
;
;  WHY PER-USER (%LOCALAPPDATA%) INSTEAD OF Program Files:
;    the built-in updater replaces files in place. Under Program Files that needs
;    elevation (UAC) on every update, and a non-elevated Updater.exe would fail with
;    "access denied". Installing per-user keeps the updater working exactly like the
;    portable build, with no admin prompt at install OR update time.
;
;  WHY THE SAVES SURVIVE:
;    the shipped package contains NO Data\playerData.json / settings.ini
;    (tools\package_release.ps1 strips them), and Inno only ever writes the files it
;    was given -- so an upgrade never overwrites a player's save, and uninstalling
;    leaves Data\ behind (delete the folder by hand if you really want a clean wipe).
;
;  BUILD:
;    "C:\...\Inno Setup 6\ISCC.exe" installer\BlastPioneer.iss
;  (paths below are relative to this .iss file)
; ============================================================================

#define AppName "爆破先锋"
#define AppNameEn "BlastPioneer"
#define AppVersion "0.1.0-rc.1"
#define AppPublisher "ST-Kdream"
#define AppUrl "https://github.com/ST-Kdream/BlastPioneer"
#define AppExe "BlastPioneer.exe"
#define SourceDir "..\dist\BlastPioneer-0.1.0-rc.1"

[Setup]
; 固定 AppId：升级/卸载认的是它，改了这个字符串就等于变成另一个软件
AppId={{9C3B1E52-5A47-4F0B-9E6D-2C8A7B41D3F5}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}
AppUpdatesURL={#AppUrl}

DefaultDirName={localappdata}\Programs\BlastPioneer
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}
AllowNoIcons=yes

; 安装包图标与卸载项图标
SetupIconFile=..\content\Assets\picture\setup.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}

; 绿色版不带安装器，安装版只在"用户级"写入，不需要管理员权限
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

OutputDir=..\dist
OutputBaseFilename=BlastPioneer-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
; Inno Setup 6 自带简体中文
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："; Flags: unchecked

[Files]
; 整包原样复制：与绿色版**完全一致**的目录结构（Assets / Data / ui / mods / qml / platforms ...）
; 这样 Updater.exe 面对的仍然是"exe 旁边就是内容目录"的布局，差分更新照常工作。
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "立即启动 {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 只清运行期缓存与日志；存档（Data\playerData.json）与设置保留，重装后自动接上
Type: filesandordirs; Name: "{app}\cache"
Type: filesandordirs; Name: "{app}\logs"
Type: filesandordirs; Name: "{app}\Update"
