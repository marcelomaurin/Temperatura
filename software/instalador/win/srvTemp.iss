; Script gerado pelo Inno Setup Script Wizard
; Instalador completo do srvTemp – MAURINSOFT

#define MyAppName "srvTemp"
#define MyAppVersion "0.5"
#define MyAppPublisher "MAURINSOFT"
#define MyAppURL "http://maurinsoft.com.br"
#define MyAppExeName "Temperatura.exe"
#define MySrcBase   "D:\projetos\maurinsoft\Temperatura\software"
#define MyDbPath    MySrcBase + "\db"
#define MyLibPath   MySrcBase + "\libs"
#define MyBinPath   MySrcBase + "\src"

[Setup]
AppId={{25E22926-C829-4F42-B173-8912FF91F4D2}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
OutputBaseFilename=srvTemp_setup_05
Compression=lzma
SolidCompression=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
UsePreviousAppDir=yes
PrivilegesRequired=admin

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Types]
Name: "srvTemp"; Description: {cm:T_srvTemp}

[Components]
Name: "srvTemp"; Description: "Arquivos do srvTemp"; Types: srvTemp;

[CustomMessages]
T_srvTemp=srvTemp
TD_srvTemp=Install demo to srvTemp

[Dirs]
; Cria C:\db com permissão de modificação para usuários e não remove no uninstall
Name: "C:\db"; Permissions: users-modify; Flags: uninsneveruninstall

[Files]
; Executável principal
Source: "{#MyBinPath}\Temperatura.exe"; DestDir: "{app}"; Flags: ignoreversion

; Banco de dados (db) -> C:\db
Source: "{#MyDbPath}\*"; DestDir: "C:\db"; \
    Flags: recursesubdirs createallsubdirs ignoreversion uninsneveruninstall

; Bibliotecas (libs) -> C:\db
Source: "{#MyLibPath}\*"; DestDir: "C:\db"; \
    Flags: recursesubdirs createallsubdirs ignoreversion uninsneveruninstall

[Icons]
; Menu Iniciar
Name: "{commonprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
; Atalho na área de trabalho (opcional)
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
; Iniciar com Windows (pós-login) — mantém por compatibilidade
Name: "{commonstartup}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Registry]
; Auto-start para todos os usuários em cada logon (HKLM\...\Run)
; Adiciona string: "srvTemp"="C:\Program Files\srvTemp\Temperatura.exe"
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "{#MyAppName}"; \
    ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue

[Run]
; Executa ao final da instalação
Filename: "{app}\{#MyAppExeName}"; \
Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; \
Flags: nowait postinstall skipifsilent
