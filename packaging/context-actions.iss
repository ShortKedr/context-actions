#ifndef StageDir
 #error StageDir is required
#endif
#ifndef OutputDir
 #error OutputDir is required
#endif
#ifndef AppVersion
 #define AppVersion "0.1.0"
#endif
[Setup]
AppId={{A836FB4F-6548-4FC3-8D67-50267B25A418}
AppName=Context Actions
AppVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\Context Actions
DefaultGroupName=Context Actions
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=ContextActions-{#AppVersion}-win-x64-setup
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\ContextActions.exe
WizardStyle=modern
DisableProgramGroupPage=yes
CloseApplications=no
RestartApplications=no
LicenseFile=..\docs\preview-license.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "shell"; Description: "File Explorer context menus (preview; unsigned package)"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Context Actions"; Filename: "{app}\ContextActions.exe"

[Code]
function ShellRegistration(const Mode: String): Boolean;
var Code: Integer;
begin
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\packaging\register-shell.ps1') + '" -Mode ' + Mode,
    ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('shell') then begin
    if not ShellRegistration('Install') then
      MsgBox('Context Actions was installed, but Explorer registration failed. See docs\build.md for the manual registration command. The preview package is unsigned and may require elevation or a signed package under your Windows policy.', mbError, MB_OK);
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  if RegKeyExists(HKCU, 'Software\Classes\CLSID\{A836FB4F-6548-4FC3-8D67-50267B25A418}') then
    if not ShellRegistration('Uninstall') then begin
      MsgBox('Explorer unregistration failed. Run packaging\register-shell.ps1 -Mode Uninstall before removing the application.', mbError, MB_OK);
      Result := False;
    end;
end;
