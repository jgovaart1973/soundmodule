; KlankModule installer (Inno Setup 6, https://jrsoftware.org/isinfo.php)
;
; Built automatically by GitHub Actions (.github/workflows/windows.yml). By hand:
;   1. Build the app: Scripts\build_windows.bat   (Release, folder "build")
;   2. "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\KlankModule.iss
; The result is build\installer\KlankModule-<version>-setup-x64.exe
;
; Installs per user by default (no administrator rights needed); the user can choose
; "install for all users" in the first page.

#ifndef AppVersion
  #define AppVersion "0.5.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#define AppName "KlankModule"
#define AppExe "KlankModule.exe"

[Setup]
AppId={{6C5B2E7A-3F1D-4C8B-9A61-2D7E4B9C1F05}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=KlankModule
AppPublisherURL=https://github.com/jgovaart1973/soundmodule
AppSupportURL=https://github.com/jgovaart1973/soundmodule/issues
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} installatie
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#BuildDir}\installer
OutputBaseFilename={#AppName}-{#AppVersion}-setup-x64
SetupIconFile=..\Resources\icon.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
dutch.Desktop=Snelkoppeling op het bureaublad
dutch.Extra=Extra:
dutch.Associate=.kmsong- en .kmperf-bestanden openen met KlankModule
dutch.Piano=Gratis concertvleugel (Salamander Grand Piano, ca. 700 MB) downloaden na de installatie
dutch.PianoShortcut=Gratis piano downloaden
dutch.Readme=Handleiding
dutch.Start=KlankModule starten
dutch.SongFile=KlankModule-nummer
dutch.PerfFile=KlankModule-performance
english.Desktop=Desktop shortcut
english.Extra=Extra:
english.Associate=Open .kmsong and .kmperf files with KlankModule
english.Piano=Download the free concert grand (Salamander Grand Piano, about 700 MB) after installing
english.PianoShortcut=Download free piano
english.Readme=Manual
english.Start=Start KlankModule
english.SongFile=KlankModule song
english.PerfFile=KlankModule performance

[Tasks]
Name: "desktopicon"; Description: "{cm:Desktop}"; GroupDescription: "{cm:Extra}"
Name: "associate"; Description: "{cm:Associate}"; GroupDescription: "{cm:Extra}"
Name: "piano"; Description: "{cm:Piano}"; GroupDescription: "{cm:Extra}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\KlankModule_artefacts\Release\KlankModule.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\KlankModuleRender_artefacts\Release\KlankModuleRender.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Scripts\download_sounds.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; DestName: "Handleiding.md"; Flags: ignoreversion
Source: "..\Docs\ARCHITECTUUR.md"; DestDir: "{app}\Docs"; Flags: ignoreversion
Source: "..\Resources\icon.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autoprograms}\{#AppName}\{cm:Readme}"; Filename: "{app}\Handleiding.md"
Name: "{autoprograms}\{#AppName}\{cm:PianoShortcut}"; Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\download_sounds.ps1"""; IconFilename: "{app}\icon.ico"
Name: "{autoprograms}\{#AppName}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; File associations (per user or for all users, follows the install mode)
Root: HKA; Subkey: "Software\Classes\.kmsong"; ValueType: string; ValueName: ""; ValueData: "KlankModule.Song"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\KlankModule.Song"; ValueType: string; ValueName: ""; ValueData: "{cm:SongFile}"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\KlankModule.Song\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\KlankModule.Song\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.kmperf"; ValueType: string; ValueName: ""; ValueData: "KlankModule.Performance"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\KlankModule.Performance"; ValueType: string; ValueName: ""; ValueData: "{cm:PerfFile}"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\KlankModule.Performance\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\KlankModule.Performance\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: associate

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\download_sounds.ps1"""; Flags: postinstall waituntilterminated; Tasks: piano; Description: "{cm:Piano}"
Filename: "{app}\{#AppExe}"; Description: "{cm:Start}"; Flags: nowait postinstall skipifsilent

; Settings, performances and songs are the user's own data (AppData / Documenten\KlankModule):
; the uninstaller leaves them alone on purpose.
