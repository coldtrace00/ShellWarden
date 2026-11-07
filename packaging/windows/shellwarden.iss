; ShellWarden installer (Inno Setup 6). build.bat package compiles it when
; Inno Setup is installed:  https://jrsoftware.org/isdl.php
; Manual:  ISCC.exe /DAppVersion=0.2.0 /DSourceDir=..\..\dist\ShellWarden shellwarden.iss

#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\ShellWarden"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{8C4E2B61-5A3F-4C1D-9E7B-2F6D0A9C3E51}
AppName=ShellWarden
AppVersion={#AppVersion}
AppVerName=ShellWarden {#AppVersion}
AppPublisher=ShellWarden
DefaultDirName={autopf}\ShellWarden
DefaultGroupName=ShellWarden
DisableProgramGroupPage=yes
; always ask for the folder (it is also where the data lives)
DisableDirPage=no
; no "Select Setup Language" box: the installer follows the Windows language.
; The program's own language is chosen inside it (Settings -> Language, or the
; button on the unlock screen) and can be changed at any time.
ShowLanguageDialog=no
; Per-user install by default (no admin rights needed); the user can choose
; "install for all users" in the dialog.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=ShellWarden-{#AppVersion}-setup
SetupIconFile=..\..\branding\shellwarden.ico
UninstallDisplayIcon={app}\ShellWarden.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\..\LICENSE

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

; The folder chosen here is the only place ShellWarden writes to:
; <folder>\data holds the vault, keys, logs, settings, backups.
[Messages]
en.SelectDirBrowseLabel=Everything ShellWarden stores (vault with hosts, passwords and keys, logs, settings, backups) will be kept in this folder, in "data". Click Next to continue, or Browse to choose another folder.
uk.SelectDirBrowseLabel=Усе, що зберігає ShellWarden (сховище з хостами, паролями й ключами, журнали, налаштування, копії), лежатиме в цій теці, у «data». Натисніть «Далі», або «Огляд», щоб вибрати іншу теку.
ru.SelectDirBrowseLabel=Всё, что хранит ShellWarden (хранилище с хостами, паролями и ключами, журналы, настройки, копии), будет лежать в этой папке, в «data». Нажмите «Далее» или «Обзор», чтобы выбрать другую папку.
fr.SelectDirBrowseLabel=Tout ce que ShellWarden enregistre (coffre avec hôtes, mots de passe et clés, journaux, réglages, sauvegardes) restera dans ce dossier, dans « data ». Cliquez sur Suivant, ou sur Parcourir pour choisir un autre dossier.
es.SelectDirBrowseLabel=Todo lo que guarda ShellWarden (bóveda con hosts, contraseñas y claves, registros, ajustes, copias) quedará en esta carpeta, en «data». Pulse Siguiente, o Examinar para elegir otra carpeta.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Dirs]
; writable for the user even under Program Files, and kept on uninstall (it is the user's vault).
; Right after installing, [Code] below narrows it to this user only (+ SYSTEM and Administrators).
Name: "{app}\data"; Permissions: users-modify; Flags: uninsneveruninstall

[Files]
; Everything windeployqt produced (never the portable marker or someone's data).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "portable,portable.txt,data\*"; Flags: ignoreversion recursesubdirs createallsubdirs
; the marker makes the app keep everything in {app}\data
Source: "portable.marker"; DestDir: "{app}"; DestName: "portable"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\ShellWarden"; Filename: "{app}\ShellWarden.exe"
Name: "{autodesktop}\ShellWarden"; Filename: "{app}\ShellWarden.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ShellWarden.exe"; Description: "{cm:LaunchProgram,ShellWarden}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Remove the kill-switch firewall rule if one is left (needs admin; harmless otherwise).
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ShellWarden kill switch"""; Flags: runhidden; RunOnceId: "RemoveFirewallRule"; Check: IsAdminInstallMode

; The vault, keys and logs (%APPDATA%\ShellWarden or {app}\data) are NOT
; removed on uninstall: they are the user's data. Delete that folder by hand if wanted.
; Updates run this installer again with /SILENT: the previous choice of tasks is kept.

[Code]
// The data folder holds the vault, its backups and the journal: other user
// accounts on this computer must not be able to read it. Give the user who
// installs full control, then drop inherited permissions and "Users".
// Runs on every install and update, so older installs are fixed too.
// If the first step fails, nothing is taken away (the folder stays usable).
procedure MakeDataPrivate();
var
  Dir, Icacls: String;
  Code: Integer;
begin
  Dir := ExpandConstant('{app}\data');
  Icacls := ExpandConstant('{sys}\icacls.exe');
  if not DirExists(Dir) then
    exit;
  if Exec(Icacls, '"' + Dir + '" /grant "' + GetUserNameString() + ':(OI)(CI)F"', '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0) then
    Exec(Icacls, '"' + Dir + '" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F /remove:g *S-1-5-32-545 *S-1-1-0 *S-1-5-11',
         '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    MakeDataPrivate();
end;
