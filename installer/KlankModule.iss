; Inno Setup script (https://jrsoftware.org/isinfo.php)
; Build the app first (Scripts\build_windows.bat), then compile this file with Inno Setup 6.
; Installs per user: no administrator rights needed.

#define AppName "KlankModule"
#define AppVersion "0.1.0"
#define BuildDir "..\build\KlankModule_artefacts\Release"

[Setup]
AppId={{6C5B2E7A-3F1D-4C8B-9A61-2D7E4B9C1F05}
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\build\installer
OutputBaseFilename=KlankModule-{#AppVersion}-setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#BuildDir}\KlankModule.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Scripts\download_sounds.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\KlankModule.exe"
Name: "{group}\Gratis piano downloaden"; Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\download_sounds.ps1"""
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\KlankModule.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Snelkoppeling op het bureaublad"; GroupDescription: "Extra:"

[Run]
Filename: "{app}\KlankModule.exe"; Description: "KlankModule starten"; Flags: nowait postinstall skipifsilent
