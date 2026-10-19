; KFW — Inno Setup installer script (per-user)
; Build: ISCC D:\KFW\packaging\kfw-installer.iss

#define AppName "Kishan Forensic Workbench"
#define AppNameShort "KFW"
#define AppVersion "0.1.0-alpha"

[Setup]
AppId={{7E4C2A98-1B3D-4E6F-9A0C-5D2F8B41C7E9}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Kishan G A
VersionInfoVersion=0.1.0.0
VersionInfoDescription={#AppName} installer
DefaultDirName={localappdata}\Programs\{#AppNameShort}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=D:\KFW\dist
OutputBaseFilename=KFW-Setup-{#AppVersion}
SetupIconFile=D:\KFW\kfw-app\ui\kfw.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\KishanForensicWorkbench.exe

[Files]
Source: "D:\KFW\dist\KFW\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion createallsubdirs; Excludes: "portable.ini,data,data\*,*.log"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "Additional icons:"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\KishanForensicWorkbench.exe"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\KishanForensicWorkbench.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\KishanForensicWorkbench.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data"
