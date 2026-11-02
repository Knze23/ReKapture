; Instalador de ReKapture (Inno Setup 6). Lo invoca scripts\package.ps1.
; Uso manual:  ISCC.exe /DAppVersion=0.6.0 /DSourceDir=D:\Dev\ReKapture\dist\ReKapture-0.6.0-portable installer\ReKapture.iss

#ifndef AppVersion
  #define AppVersion "0.6.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\ReKapture-0.6.0-portable"
#endif

[Setup]
AppId={{6C1B3D5E-2A74-4F5B-9B1D-0E8A5C7D4F21}
AppName=ReKapture
AppVersion={#AppVersion}
AppPublisher=ReKapture
DefaultDirName={localappdata}\Programs\ReKapture
DefaultGroupName=ReKapture
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=ReKapture-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\64bit\ReKapture.exe
SetupIconFile=..\resources\icons\rekapture.ico
WizardImageFile=wizard.bmp
WizardSmallImageFile=wizard-small.bmp
DisableProgramGroupPage=yes

#define SpanishIsl AddBackslash(CompilerPath) + "Languages\Spanish.isl"
#if FileExists(SpanishIsl)
[Languages]
Name: "spanish"; MessagesFile: "{#SpanishIsl}"
#endif

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; Flags: unchecked

[Files]
; El instalador no incluye el modo portable: los ajustes van a %APPDATA% y %LOCALAPPDATA%
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "portable.flag,ReKapture.cmd,LEEME-portable.txt"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\ReKapture"; Filename: "{app}\bin\64bit\ReKapture.exe"; WorkingDir: "{app}\bin\64bit"
Name: "{autodesktop}\ReKapture"; Filename: "{app}\bin\64bit\ReKapture.exe"; WorkingDir: "{app}\bin\64bit"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\64bit\ReKapture.exe"; WorkingDir: "{app}\bin\64bit"; Description: "Abrir ReKapture"; Flags: nowait postinstall skipifsilent
