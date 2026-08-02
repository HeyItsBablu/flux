#pragma once

// Default Inno Setup script. Overridden if the project has its own file at
// installer/windows/app.iss (see windows_release()) — same "convention
// with override" pattern as everything else in flux.
inline const char *kDefaultInnoTemplate = R"ISS(
#define MyAppName "@@APP_NAME@@"
#define MyAppVersion "@@APP_VERSION@@"
#define MyAppPublisher "@@APP_PUBLISHER@@"
#define MyAppExeName "@@APP_EXE_NAME@@"

[Setup]
AppId={{@@APP_GUID@@}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputBaseFilename={#MyAppName}-Setup-{#MyAppVersion}
OutputDir=@@OUTPUT_DIR@@
Compression=lzma2
SolidCompression=yes
@@ICON_FILE_LINE@@
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "@@APP_EXE_PATH@@"; DestDir: "{app}"; Flags: ignoreversion
@@ASSETS_FILES_LINE@@

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch @@APP_NAME@@"; Flags: nowait postinstall skipifsilent
)ISS";