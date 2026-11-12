#ifndef StageDir
 #error StageDir is required
#endif
#ifndef OutputDir
 #error OutputDir is required
#endif
#ifndef AppVersion
 #define AppVersion "0.2.0"
#endif
#define AppGuid "A836FB4F-6548-4FC3-8D67-50267B25A418"
[Setup]
AppId={{{#AppGuid}}
SetupMutex=ContextActionsSetup-{#AppGuid}
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
SetupIconFile=..\..\src\windows\resources\app.ico
UninstallDisplayIcon={app}\ContextActions.exe
WizardStyle=modern
DisableProgramGroupPage=yes
CloseApplications=no
RestartApplications=no
LicenseFile=..\..\docs\preview-license.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
english.ReplacementTitle=Replace the installed version
english.ReplacementDescription=Version %1 is installed. This installer contains version %2.
english.AllowReplacement=Allow reinstall or downgrade
english.VersionUnknown=Setup could not determine the installed version of Context Actions.
russian.ReplacementTitle=Замена установленной версии
russian.ReplacementDescription=Установлена версия %1. Этот установщик содержит версию %2.
russian.AllowReplacement=Разрешить переустановку или откат
russian.VersionUnknown=Не удалось определить установленную версию Context Actions.
german.ReplacementTitle=Installierte Version ersetzen
german.ReplacementDescription=Version %1 ist installiert. Dieses Setup enthält Version %2.
german.AllowReplacement=Neuinstallation oder Downgrade erlauben
german.VersionUnknown=Die installierte Version von Context Actions konnte nicht ermittelt werden.
french.ReplacementTitle=Remplacer la version installée
french.ReplacementDescription=La version %1 est installée. Ce programme installe la version %2.
french.AllowReplacement=Autoriser la réinstallation ou le retour à une version antérieure
french.VersionUnknown=Impossible de déterminer la version installée de Context Actions.

[Tasks]
Name: "shell"; Description: "File Explorer context menus (local preview; requires Developer Mode already enabled)"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef DownloadEngines
Source: "{tmp}\{#FFmpegRoot}\bin\ffmpeg.exe"; DestDir: "{app}\engines"; ExternalSize: {#FFmpegBytes}; Flags: external ignoreversion
Source: "{tmp}\{#FFmpegRoot}\bin\ffprobe.exe"; DestDir: "{app}\engines"; ExternalSize: {#FFprobeBytes}; Flags: external ignoreversion
Source: "{tmp}\{#FFmpegRoot}\*"; DestDir: "{app}\licenses\ffmpeg"; ExternalSize: {#FFmpegNoticeBytes}; Flags: external ignoreversion
Source: "{tmp}\{#QpdfRoot}\bin\qpdf.exe"; DestDir: "{app}\engines"; ExternalSize: {#QpdfBytes}; Flags: external ignoreversion
Source: "{tmp}\{#QpdfRoot}\bin\*.dll"; DestDir: "{app}\engines"; ExternalSize: {#QpdfLibraryBytes}; Flags: external ignoreversion
Source: "{tmp}\{#QpdfRoot}\share\*"; DestDir: "{app}\licenses\qpdf\share"; ExternalSize: {#QpdfNoticeBytes}; Flags: external ignoreversion recursesubdirs createallsubdirs
#endif

[Icons]
Name: "{autoprograms}\Context Actions"; Filename: "{app}\ContextActions.exe"

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#AppGuid}}_is1';

var
  ReplacementPage: TInputOptionWizardPage;
  ConfirmedFromVersion: String;
#ifdef DownloadEngines
  MediaToolsPage: TDownloadWizardPage;
#endif

function CheckInstalledVersion(var InstalledVersion: String; var Comparison: Integer): Boolean;
var CurrentVersion, SetupVersion: Int64;
begin
  InstalledVersion := '';
  Comparison := -1;
  Result := True;
  if not RegKeyExists(HKCU64, UninstallKey) then Exit;
  Result := RegQueryStringValue(HKCU64, UninstallKey, 'DisplayVersion', InstalledVersion) and
    StrToVersion(InstalledVersion, CurrentVersion) and StrToVersion('{#AppVersion}', SetupVersion);
  if Result then Comparison := ComparePackedVersion(CurrentVersion, SetupVersion);
end;

procedure InitializeWizard();
var Comparison: Integer;
begin
  if CheckInstalledVersion(ConfirmedFromVersion, Comparison) and (Comparison >= 0) and not WizardSilent then begin
    ReplacementPage := CreateInputOptionPage(wpSelectTasks, CustomMessage('ReplacementTitle'),
      FmtMessage(CustomMessage('ReplacementDescription'), [ConfirmedFromVersion, '{#AppVersion}']),
      '', False, False);
    ReplacementPage.Add(CustomMessage('AllowReplacement'));
    ReplacementPage.Values[0] := False;
  end;
#ifdef DownloadEngines
  MediaToolsPage := CreateDownloadPage('Downloading media tools',
    'Setup downloads FFmpeg and qpdf from their publishers and checks their SHA-256 hashes.', nil);
#endif
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if ReplacementPage <> nil then
    if (CurPageID = ReplacementPage.ID) and not ReplacementPage.Values[0] then begin
      SuppressibleMsgBox(CustomMessage('AllowReplacement'), mbInformation, MB_OK, IDOK);
      Result := False;
    end;
end;

#ifdef DownloadEngines
procedure UnpackMediaTools(const ArchiveName: String);
var TempDir: String; Code: Integer;
begin
  TempDir := ExpandConstant('{tmp}');
  if not Exec(ExpandConstant('{sys}\tar.exe'), '-xf "' + TempDir + '\' + ArchiveName +
    '" -C "' + TempDir + '"', TempDir, SW_HIDE, ewWaitUntilTerminated, Code) then
    RaiseException('Windows tar could not start: ' + SysErrorMessage(Code));
  if Code <> 0 then
    RaiseException('Windows tar could not extract ' + ArchiveName + ' (exit code ' + IntToStr(Code) + ').');
end;
#endif

function PrepareToInstall(var NeedsRestart: Boolean): String;
var InstalledVersion: String; Comparison: Integer; AllowReplacement: Boolean;
begin
  Result := '';
  if not CheckInstalledVersion(InstalledVersion, Comparison) then
    Result := CustomMessage('VersionUnknown')
  else if Comparison >= 0 then begin
    AllowReplacement := False;
    if ReplacementPage <> nil then
      AllowReplacement := (InstalledVersion = ConfirmedFromVersion) and ReplacementPage.Values[0];
    if not AllowReplacement then
      Result := FmtMessage(CustomMessage('ReplacementDescription'), [InstalledVersion, '{#AppVersion}']);
  end;
  if Result <> '' then Exit;
#ifdef DownloadEngines
  MediaToolsPage.Clear;
  MediaToolsPage.Add('{#FFmpegUrl}', 'ffmpeg.zip', '{#FFmpegHash}');
  MediaToolsPage.Add('{#QpdfUrl}', 'qpdf.zip', '{#QpdfHash}');
  MediaToolsPage.Show;
  try
    try
      MediaToolsPage.Download;
      UnpackMediaTools('ffmpeg.zip');
      UnpackMediaTools('qpdf.zip');
    except
      Result := 'Setup could not prepare the media tools. ' + GetExceptionMessage;
    end;
  finally
    MediaToolsPage.Hide;
  end;
#endif
end;

function ShellRegistration(const Mode: String): Boolean;
var Code: Integer; Arguments: String;
begin
  Arguments := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\packaging\register-shell.ps1') + '" -Mode ' + Mode;
  if Mode = 'Install' then Arguments := Arguments + ' -Development';
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    Arguments,
    ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('shell') then begin
    if not ShellRegistration('Install') then
      MsgBox('Context Actions was installed, but Explorer registration failed. The local preview requires Developer Mode to be enabled beforehand. No Windows security settings were changed. See docs\build.md for registration details.', mbError, MB_OK);
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
