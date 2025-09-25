// Objetivo: construir os parametros de setup da classe principal
// Criado por Marcelo Maurin Martins
// Data:18/08/2019

unit setmain;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, funcoes;

const
  filename = 'srvtemp.cfg';

type
  { TSetMain }

  TSetMain = class(TObject)
    constructor Create();
    destructor Destroy(); override;
  private
    arquivo      : TStringList;
    ckdevice     : Boolean;
    FPATH        : string;
    FPosX        : Integer;
    FPosY        : Integer;
    FHide        : Boolean;
    FEXEC        : Boolean;
    FCOM         : string;
    FBAUD        : Integer;
    FDTBIT       : Integer;
    FPARI        : Integer;
    FSTBIT       : Integer;
    FEmpresa     : string;
    FLocalizacao : string;
    FLocalBanco  : string;

    procedure Default();
    procedure SetPOSX(value : Integer);
    procedure SetPOSY(value : Integer);
    procedure SetDevice(const Value : Boolean);
    procedure SetHide(value : Boolean);
    procedure SetEXEC(value : Boolean);
    procedure SetCOM(value : string);
    procedure SetBAUD(value : Integer);
    procedure SetDTBIT(value : Integer);
    procedure SetPARI(value : Integer);
    procedure SetSTBIT(value : Integer);
    procedure SetEmpresa(value: string);
    procedure SetLocalizacao(value: string);
    procedure SetLocalBanco(value: string);

  public

    property device      : Boolean read ckdevice     write SetDevice;
    property posx        : Integer read FPosX        write SetPOSX;
    property posy        : Integer read FPosY        write SetPOSY;
    property Hide        : Boolean read FHide        write SetHide;
    property EXEC        : Boolean read FEXEC        write SetEXEC;
    property COMPORT     : string  read FCOM         write SetCOM;
    property BAUDRATE    : Integer read FBAUD        write SetBAUD;
    property DATABIT     : Integer read FDTBIT       write SetDTBIT;
    property PARIDADE    : Integer read FPARI        write SetPARI;
    property STOPBIT     : Integer read FSTBIT       write SetSTBIT;
    property Empresa     : string  read FEmpresa     write SetEmpresa;
    property Localizacao : string  read FLocalizacao write SetLocalizacao;
    property LocalBanco  : string  read FLocalBanco  write SetLocalBanco;
    procedure SalvaContexto();
    procedure CarregaContexto();

  end;

var
  FSETMAIN : TSetMain;

implementation

{ ===== Setters básicos ===== }

procedure TSetMain.SetPOSX(value: Integer);
begin
  FPosX := value;
end;

procedure TSetMain.SetPOSY(value: Integer);
begin
  FPosY := value;
end;

procedure TSetMain.SetDevice(const Value: Boolean);
begin
  ckdevice := Value;
end;

procedure TSetMain.SetHide(value: Boolean);
begin
  FHide := value;
end;

procedure TSetMain.SetEXEC(value: Boolean);
begin
  FEXEC := value;
end;

procedure TSetMain.SetCOM(value: string);
begin
  FCOM := value;
end;

procedure TSetMain.SetBAUD(value: Integer);
begin
  FBAUD := value;
end;

procedure TSetMain.SetDTBIT(value: Integer);
begin
  FDTBIT := value;
end;

procedure TSetMain.SetPARI(value: Integer);
begin
  FPARI := value;
end;

procedure TSetMain.SetSTBIT(value: Integer);
begin
  FSTBIT := value;
end;

procedure TSetMain.SetEmpresa(value: string);
begin
  FEmpresa := value;
end;

procedure TSetMain.SetLocalizacao(value: string);
begin
  FLocalizacao := value;
end;

procedure TSetMain.SetLocalBanco(value: string);
begin
  FLocalBanco := value;
end;

{ ===== Valores default ===== }

procedure TSetMain.Default();
begin
  ckdevice  := False;
  FEXEC     := False;
  FHide     := False;

  {$IFDEF LINUX}
  FCOM := '/dev/ttyS0';
  {$ENDIF}
  {$IFDEF WINDOWS}
  FCOM := 'COM13';
  {$ENDIF}

  FBAUD  := 3;  // (* 2400 *), mantendo seu mapeamento original
  FDTBIT := 0;  // 8 data bits
  FPARI  := 0;  // Paridade N
  FSTBIT := 0;  // Stop 1

  FEmpresa     := 'maurinsoft';
  FLocalizacao := 'nothing';

  // **NOVO**: Local do banco com default solicitado
  FLocalBanco  := 'c:\db\temperatura.db';
end;

{ ===== Carregar/Salvar contexto ===== }

procedure TSetMain.CarregaContexto();
var
  posicao: Integer;
begin
  if BuscaChave(arquivo,'DEVICE:',posicao) then
    device := (RetiraInfo(arquivo.Strings[posicao])='1');

  if BuscaChave(arquivo,'POSX:',posicao) then
    FPOSX := StrToIntDef(RetiraInfo(arquivo.Strings[posicao]), FPOSX);

  if BuscaChave(arquivo,'POSY:',posicao) then
    FPOSY := StrToIntDef(RetiraInfo(arquivo.Strings[posicao]), FPOSY);

  if BuscaChave(arquivo,'HIDE:',posicao) then
    FHide := StrToBoolDef(RetiraInfo(arquivo.Strings[posicao]), FHide);

  if BuscaChave(arquivo,'EXEC:',posicao) then
    FEXEC := StrToBoolDef(RetiraInfo(arquivo.Strings[posicao]), FEXEC);

  if BuscaChave(arquivo,'COMPORT:',posicao) then
    FCOM := RetiraInfo(arquivo.Strings[posicao]);

  if BuscaChave(arquivo,'BAUDRATE:',posicao) then
    FBAUD := StrToIntDef(RetiraInfo(arquivo.Strings[posicao]), FBAUD);

  if BuscaChave(arquivo,'DATABIT:',posicao) then
    FDTBIT := StrToIntDef(RetiraInfo(arquivo.Strings[posicao]), FDTBIT);

  if BuscaChave(arquivo,'PARIDADE:',posicao) then
    FPARI := StrToIntDef(RetiraInfo(arquivo.Strings[posicao]), FPARI);

  if BuscaChave(arquivo,'STOPBIT:',posicao) then
    FSTBIT := StrToIntDef(RetiraInfo(arquivo.Strings[posicao]), FSTBIT);

  if BuscaChave(arquivo,'EMPRESA:',posicao) then
    FEMPRESA := RetiraInfo(arquivo.Strings[posicao]);

  if BuscaChave(arquivo,'LOCALIZACAO:',posicao) then
    FLOCALIZACAO := RetiraInfo(arquivo.Strings[posicao]);

  // **NOVO**: LOCALBANCO
  if BuscaChave(arquivo,'LOCALBANCO:',posicao) then
    FLocalBanco := RetiraInfo(arquivo.Strings[posicao]);
end;

procedure TSetMain.SalvaContexto();
begin
  arquivo.Clear;
  arquivo.Append('DEVICE:'      + iif(ckdevice,'1','0'));
  arquivo.Append('POSX:'        + IntToStr(FPOSX));
  arquivo.Append('POSY:'        + IntToStr(FPOSY));
  arquivo.Append('HIDE:'        + BoolToStr(FHide));
  arquivo.Append('EXEC:'        + BoolToStr(FEXEC));
  arquivo.Append('COMPORT:'     + FCOM);
  arquivo.Append('BAUDRATE:'    + IntToStr(FBAUD));
  arquivo.Append('DATABIT:'     + IntToStr(FDTBIT));
  arquivo.Append('PARIDADE:'    + IntToStr(FPARI));
  arquivo.Append('STOPBIT:'     + IntToStr(FSTBIT));
  arquivo.Append('EMPRESA:'     + FEmpresa);
  arquivo.Append('LOCALIZACAO:' + FLocalizacao);
  // **NOVO**: LOCALBANCO
  arquivo.Append('LOCALBANCO:'  + FLocalBanco);

  arquivo.SaveToFile(FPATH + filename);
end;

{ ===== Construtor/Destrutor ===== }

constructor TSetMain.Create();
begin
  inherited Create;
  arquivo := TStringList.Create;

  {$IFDEF LINUX}
  FPATH := GetAppConfigDir(False);
  if not DirectoryExists(FPATH) then
    CreateDir(FPATH);
  {$ENDIF}

  {$IFDEF WINDOWS}
  FPATH := GetAppConfigDir(False);
  if not DirectoryExists(FPATH) then
    CreateDir(FPATH);
  {$ENDIF}

  if FileExists(FPATH + filename) then
  begin
    arquivo.LoadFromFile(FPATH + filename);
    Default;          // garante defaults antes de sobrescrever
    CarregaContexto;  // sobrescreve com o que existe no arquivo
  end
  else
  begin
    Default;
  end;
end;

destructor TSetMain.Destroy();
begin
  SalvaContexto();
  FreeAndNil(arquivo);
  inherited Destroy;
end;

end.

