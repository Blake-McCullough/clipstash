; Windows installer. Built by the release workflow:
;   iscc /DAppVersion=0.6.0 /DSourceDir=dist packaging\clipstash.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif

[Setup]
AppId={{6F1C2B7E-3D5A-4E8B-9C21-5A7D0E4F8B13}
AppName=Clipstash
AppVersion={#AppVersion}
AppPublisher=Blake McCullough
DefaultDirName={localappdata}\Programs\Clipstash
DefaultGroupName=Clipstash
DisableProgramGroupPage=yes
; Installs for the current user only: no admin prompt, and updates can
; replace the files without asking either.
PrivilegesRequired=lowest
OutputBaseFilename=Clipstash-Setup-x64
OutputDir=..
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupIconFile=..\data\clipstash.ico
UninstallDisplayIcon={app}\clipstash.exe
CloseApplications=force
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Left-over libraries from older versions.
Type: filesandordirs; Name: "{app}\*.dll"
Type: filesandordirs; Name: "{app}\plugins"

[Icons]
Name: "{autoprograms}\Clipstash"; Filename: "{app}\clipstash.exe"
Name: "{autodesktop}\Clipstash"; Filename: "{app}\clipstash.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\clipstash.exe"; Description: "Start Clipstash"; Flags: nowait postinstall skipifsilent
; After an update from inside the app (a silent install), start it again.
Filename: "{app}\clipstash.exe"; Flags: nowait skipifnotsilent

[UninstallRun]
Filename: "{app}\clipstash.exe"; Parameters: "--quit"; Flags: runhidden skipifdoesntexist; RunOnceId: "QuitClipstash"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
