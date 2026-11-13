; AsiaCAD — Windows o'rnatuvchisi (Inno Setup 6)
; GitHub Actions: iscc /DSrcDir=..\..\AsiaCAD /DAppVer=0.33 apps\win\asiacad.iss
#ifndef AppVer
  #define AppVer "0.33"
#endif
#ifndef SrcDir
  #define SrcDir "..\..\AsiaCAD"
#endif

[Setup]
AppId={{6A1F3C2E-7B54-4E1A-9C8D-A51ACAD00001}
AppName=AsiaCAD
AppVersion={#AppVer}
AppPublisher=AsiaCAD
DefaultDirName={autopf}\AsiaCAD
DefaultGroupName=AsiaCAD
UninstallDisplayIcon={app}\AsiaCAD.exe
SetupIconFile=asiacad.ico
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=AsiaCAD-Setup-{#AppVer}
ChangesAssociations=yes
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "tr"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SrcDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "vc_redist.x64.exe"
Source: "{#SrcDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall skipifsourcedoesntexist

[Icons]
Name: "{group}\AsiaCAD"; Filename: "{app}\AsiaCAD.exe"; IconFilename: "{app}\AsiaCAD.exe"
Name: "{group}\{cm:UninstallProgram,AsiaCAD}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\AsiaCAD"; Filename: "{app}\AsiaCAD.exe"; IconFilename: "{app}\AsiaCAD.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.acad"; ValueType: string; ValueName: ""; ValueData: "AsiaCAD.Drawing"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\AsiaCAD.Drawing"; ValueType: string; ValueName: ""; ValueData: "AsiaCAD"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\AsiaCAD.Drawing\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\AsiaCAD.exe,0"
Root: HKA; Subkey: "Software\Classes\AsiaCAD.Drawing\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\AsiaCAD.exe"" ""%1"""

[Run]
; Visual C++ ish vaqti kutubxonalari (yo'q bo'lsa dastur ochilmaydi)
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Microsoft Visual C++ Runtime..."; Check: VCRedistNeeded; Flags: waituntilterminated skipifdoesntexist
Filename: "{app}\AsiaCAD.exe"; Description: "{cm:LaunchProgram,AsiaCAD}"; Flags: nowait postinstall skipifsilent

[Code]
function VCRedistNeeded: Boolean;
var v: Cardinal;
begin
  Result := not (RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', v) and (v = 1));
end;
