; Inno Setup script: installs the OL-4 VST3 (and the Standalone app) on Windows.
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SrcDir
  #define SrcDir "..\build\OL4_artefacts\Release"
#endif

[Setup]
AppId={{6C2E1A54-0B7D-4C5E-9E4A-0A4F4C2D0A41}
AppName=OL-4 Optical Leveler
AppVersion={#AppVersion}
AppPublisher=OL Audio
DefaultDirName={autopf}\OL Audio\OL-4 Optical Leveler
DefaultGroupName=OL Audio
DisableProgramGroupPage=yes
OutputBaseFilename=OL-4-Windows-Installer-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=OL-4 Optical Leveler

[Types]
Name: "full"; Description: "VST3 plugin and Standalone app"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (FL Studio, Ableton Live, Cubase, Studio One, Reaper, ...)"; Types: full custom; Flags: fixed
Name: "app"; Description: "Standalone app (use with a mic or interface, no DAW needed)"; Types: full

[Files]
Source: "{#SrcDir}\VST3\OL-4 Optical Leveler.vst3\*"; DestDir: "{commoncf64}\VST3\OL-4 Optical Leveler.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#SrcDir}\Standalone\OL-4 Optical Leveler.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app

[Icons]
Name: "{autoprograms}\OL-4 Optical Leveler"; Filename: "{app}\OL-4 Optical Leveler.exe"; Components: app

[Run]
Filename: "{app}\OL-4 Optical Leveler.exe"; Description: "Open the OL-4 Standalone app now"; Flags: postinstall nowait skipifsilent unchecked; Components: app
