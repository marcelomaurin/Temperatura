unit base;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, StrUtils, ZConnection, ZDataset, ZAbstractRODataset,
  DataPortSerial, DataPortHTTP,
  fphttpclient, opensslsockets, fpjson, jsonparser, DB,
  setmain, DataPortUART;

type
  // Callback opcional quando um JSON de resposta chegar via TCP/HTTP
  TOnTcpJsonReceived = procedure(Sender: TObject; const AJson: TJSONData) of object;

  { TdmBase }

  TdmBase = class(TDataModule)
    DataPortHTTP1: TDataPortHTTP;
    DataPortSerial1: TDataPortSerial;
    dsdevices: TDataSource;
    ZConnection1: TZConnection;
    zqryaux: TZQuery;
    zqrydevices: TZQuery;
    zqrydevicesdtcad: TZDateTimeField;
    zqrydevicesid_device: TZInt64Field;
    zqrydevicesnome: TZRawStringField;
    zqrydevicesporta: TZRawCLobField;
    zqrydevicestipo: TZInt64Field;
    tbldevices: TZTable;
    tbldevicesdtcad: TZDateTimeField;
    tbldevicesnome: TZRawStringField;
    tbldevicesporta: TZRawCLobField;
    tbldevicestipo: TZInt64Field;
    procedure DataModuleCreate(Sender: TObject);
    procedure DataModuleDestroy(Sender: TObject);
    // >>> assinatura correta do evento conforme TMsgEvent:
    procedure DataPortSerial1DataAppear(Sender: TObject; const AMsg: AnsiString);
  private
    FBaseUrl: string;
    FHttpTimeoutMs: Integer;
    FOnTcpJsonReceived: TOnTcpJsonReceived;

    // Buffer incremental e fila de linhas completas
    FSerialBuf: RawByteString;
    FLines: TStringList;

    function BuildUrl(const APath: string): string;

    // Helpers de mapeamento de códigos do SetMain
    function BaudFromCode(Code: Integer): Integer;
    function DataBitsFromCode(Code: Integer): Integer;
    function ParityCharFromCode(Code: Integer): AnsiChar;
    function StopBitsFromCode(Code: Integer): TSerialStopBits;

    // Trata o buffer acumulado, gerando linhas completas
    procedure ProcessSerialBuffer;
  public
    { ====== BANCO ====== }
    procedure AplicaConfigBanco;

    { ====== SERIAL (DataPortSerial1) ====== }
    function AtualizaConSerial(const AReabrir: Boolean = True): Boolean;

    function SerialConfig(const APort: string; ABaud: Integer): Boolean;
    function SerialOpen: Boolean;
    procedure SerialClose;
    function SerialWrite(const S: RawByteString): Boolean;

    // Lê uma LINHA já montada pelo OnDataAppear (espera até TimeoutMs)
    function SerialReadLine(out S: string; const TimeoutMs: Integer = 1000): Boolean;
    function SerialWriteRead(const OutS: RawByteString; out InLine: string; const TimeoutMs: Integer = 1000): Boolean;

    // Utilitários da fila de linhas
    procedure SerialFlushBuffer;
    function SerialHasLine: Boolean;
    function SerialPopLine(out L: string): Boolean;

    { ====== TCP/HTTP + JSON (usa TFPHTTPClient) ====== }
    procedure HttpConfig(const ABaseUrl: string; const ATimeoutMs: Integer = 5000);
    function TcpGet(const APath: string; out AResponseText: string): Boolean;
    function TcpGetJson(const APath: string; out JsonResp: TJSONData): Boolean;
    //function TcpPostJsonText(const APath: string; const JsonBody: TJSONData; out AResponseText: string): Boolean;
    //function TcpPostJson(const APath: string; const JsonBody: TJSONData; out JsonResp: TJSONData): Boolean;

       // ===== DEVICES (consulta/insert) =====
    // Abre todos os devices no zqrydevices (dataset do dsdevices)
    function DevicesOpenAll: Boolean;

    // Busca por nome (LIKE %ANameLike%). Resultado em zqrydevices
    function DevicesSearchByName(const ANameLike: string): Boolean;

    // Carrega um device específico por ID (aceita id ou id_device)
    function DeviceFindById(const AId: Int64): Boolean;

    // INSERT: retorna o ID gerado (via last_insert_rowid)
    function DeviceInsert(const ANome, APorta: string; ATipo: Integer; const AData: TDateTime): Int64;
    function DeviceInsertNow(const ANome, APorta: string; ATipo: Integer): Int64; // dtcad := Now

    property OnTcpJsonReceived: TOnTcpJsonReceived read FOnTcpJsonReceived write FOnTcpJsonReceived;
  end;

var
  dmBase :  TdmBase;

implementation

{$R *.lfm}

{ TdmBase }

procedure TdmBase.DataModuleCreate(Sender: TObject);
begin
  AplicaConfigBanco;

  FBaseUrl       := '';
  FHttpTimeoutMs := 5000;

  // buffers
  FSerialBuf := '';
  FLines     := TStringList.Create;

  // garante handler conectado:
  //DataPortSerial1.OnDataAppear := @DataPortSerial1DataAppear;

  // aplica configuração do setmain e abre
  AtualizaConSerial(True);
end;

procedure TdmBase.DataModuleDestroy(Sender: TObject);
begin
  FreeAndNil(FLines);
end;

procedure TdmBase.DataPortSerial1DataAppear(Sender: TObject; const AMsg: AnsiString);
begin
  // chega como "pacote" do thread serial — acumula e quebra por linhas
  FSerialBuf := FSerialBuf + AMsg;
  ProcessSerialBuffer;

  // proteção: evita buffer infinito se não vier LF
  if Length(FSerialBuf) > 1*1024*1024 then
    FSerialBuf := RightStr(FSerialBuf, 64*1024);
end;

procedure TdmBase.ProcessSerialBuffer;
var
  pLF: SizeInt;
  line: RawByteString;
  s: string;
begin
  // Linha: terminada em LF; CR final é opcional
  while True do
  begin
    pLF := Pos(#10, FSerialBuf);
    if pLF = 0 then Break;

    line := Copy(FSerialBuf, 1, pLF - 1);
    if (Length(line) > 0) and (line[Length(line)] = #13) then
      SetLength(line, Length(line) - 1);

    Delete(FSerialBuf, 1, pLF);

    // normaliza e enfileira
    s := UTF8Encode(UTF8Decode(line));
    FLines.Add(s);
  end;
end;

procedure TdmBase.AplicaConfigBanco;
var
  dllPath: string;
begin
  dllPath := ExtractFilePath(ParamStr(0)) + 'sqlite3.dll';
  if FileExists(dllPath) then
    ZConnection1.LibraryLocation := dllPath;

  ZConnection1.Protocol:= 'sqlite';
  if Assigned(FSETMAIN) and (Trim(FSETMAIN.LocalBanco) <> '') then
    begin
      ZConnection1.Database := FSETMAIN.LocalBanco;
      ZConnection1.LibraryLocation:= ExtractFilePath(FSETMAIN.LocalBanco)+'\sqlite3.dll';

    end
  else
    ZConnection1.Database := 'c:\db\temperatura.db';

  if not ZConnection1.Connected then
  try
    ZConnection1.Connect;
  except
    // silencioso
  end;
end;

function TdmBase.BaudFromCode(Code: Integer): Integer;
begin
  case Code of
    0: Result := 1200;
    1: Result := 1800;
    2: Result := 1200;
    3: Result := 2400;
    4: Result := 4800;
    5: Result := 9600;
    6: Result := 14400;
    7: Result := 19200;
    8: Result := 38400;
    9: Result := 56000;
    10: Result := 57600;
    11: Result := 115200;
  else
    Result := 9600;
  end;
end;

function TdmBase.DataBitsFromCode(Code: Integer): Integer;
begin
  case Code of
    0: Result := 8;
    1: Result := 7;
    2: Result := 6;
    3: Result := 5;
  else
    Result := 8;
  end;
end;

function TdmBase.ParityCharFromCode(Code: Integer): AnsiChar;
begin
  case Code of
    0: Result := 'N';
    1: Result := 'O';
    2: Result := 'E';
    3: Result := 'M';
    4: Result := 'S';
  else
    Result := 'N';
  end;
end;

function TdmBase.StopBitsFromCode(Code: Integer): TSerialStopBits;
begin
  case Code of
    0: Result := stb1;
    1: Result := stb2;   // se precisar de 1.5, ajuste seu code map
    2: Result := stb2;
  else
    Result := stb1;
  end;
end;

function TdmBase.AtualizaConSerial(const AReabrir: Boolean): Boolean;
begin
  Result := False;
  if not Assigned(FSETMAIN) then Exit(False);

  try
    DataPortSerial1.Close;
  except end;

  try
    DataPortSerial1.Port       := FSETMAIN.COMPORT;
    DataPortSerial1.BaudRate   := BaudFromCode(FSETMAIN.BAUDRATE);
    DataPortSerial1.DataBits   := DataBitsFromCode(FSETMAIN.DATABIT);
    DataPortSerial1.Parity     := ParityCharFromCode(FSETMAIN.PARIDADE);
    DataPortSerial1.StopBits   := StopBitsFromCode(FSETMAIN.STOPBIT);
    // FlowControl mantém default (sfcNone), ajuste aqui se desejar

    if AReabrir then
      DataPortSerial1.Open;

    Result := True;
  except
    Result := False;
  end;
end;

function TdmBase.BuildUrl(const APath: string): string;
var L, R: string;
begin
  if FBaseUrl = '' then Exit(APath);
  L := FBaseUrl; R := APath;
  if (L <> '') and (L[Length(L)] = '/') then SetLength(L, Length(L)-1);
  if (R <> '') and (R[1] = '/') then Delete(R, 1, 1);
  Result := L + '/' + R;
end;

// ---------- DEVICES helpers internos ----------
function TryOpenDevicesWithSQL(AQry: TZQuery; const ASQL: string;
  const AParamName: string = ''; const AParamValueInt64: Int64 = 0;
  const AParamName2: string = ''; const AParamValueStr: string = ''): Boolean;
begin
  Result := False;
  AQry.Close;
  AQry.SQL.Text := ASQL;
  if AParamName <> '' then
    AQry.ParamByName(AParamName).AsLargeInt := AParamValueInt64;
  if AParamName2 <> '' then
    AQry.ParamByName(AParamName2).AsString := AParamValueStr;
  try
    AQry.Open;
    Result := True;
  except
    AQry.Close;
    Result := False;
  end;
end;

function TdmBase.DevicesOpenAll: Boolean;
begin
  // 1ª tentativa: banco com coluna "id"
  Result := TryOpenDevicesWithSQL(
    zqrydevices,
    'select id_device, nome, tipo, porta, dtcad from devices order id_device'
  );

  // 2ª tentativa: banco com coluna "id_device"
  if not Result then
    Result := TryOpenDevicesWithSQL(
      zqrydevices,
      'select id_device as id_device, nome, tipo, porta, dtcad from devices order by id_device'
    );

  // garante o DataSource
  if Result and (dsdevices.DataSet <> zqrydevices) then
    dsdevices.DataSet := zqrydevices;
end;

function TdmBase.DevicesSearchByName(const ANameLike: string): Boolean;
var likeStr: string;
begin
  likeStr := '%' + ANameLike + '%';

  // 1ª tentativa: esquema com "id"
  Result := TryOpenDevicesWithSQL(
    zqrydevices,
    'select id as id_device, nome, tipo, porta, dtcad '+
    'from devices where nome like :n order by nome',
    '', 0, 'n', likeStr
  );

  // 2ª tentativa: esquema com "id_device"
  if not Result then
    Result := TryOpenDevicesWithSQL(
      zqrydevices,
      'select id_device as id_device, nome, tipo, porta, dtcad '+
      'from devices where nome like :n order by nome',
      '', 0, 'n', likeStr
    );

  if Result and (dsdevices.DataSet <> zqrydevices) then
    dsdevices.DataSet := zqrydevices;
end;

function TdmBase.DeviceFindById(const AId: Int64): Boolean;
begin
  // 1ª tentativa: coluna "id"
  Result := TryOpenDevicesWithSQL(
    zqrydevices,
    'select id as id_device, nome, tipo, porta, dtcad '+
    'from devices where id = :id limit 1',
    'id', AId
  );

  // 2ª tentativa: coluna "id_device"
  if not Result then
    Result := TryOpenDevicesWithSQL(
      zqrydevices,
      'select id_device as id_device, nome, tipo, porta, dtcad '+
      'from devices where id_device = :id limit 1',
      'id', AId
    );

  if Result and (dsdevices.DataSet <> zqrydevices) then
    dsdevices.DataSet := zqrydevices;
end;

function TdmBase.DeviceInsert(const ANome, APorta: string; ATipo: Integer; const AData: TDateTime): Int64;
begin
  Result := -1;
  // Insere só as colunas não-PK (independe se PK chama "id" ou "id_device")
  zqryaux.Close;
  zqryaux.SQL.Text :=
    'insert into devices (nome, tipo, porta, dtcad) '+
    'values (:nome, :tipo, :porta, :dtcad)';
  zqryaux.ParamByName('nome').AsString     := ANome;
  zqryaux.ParamByName('tipo').AsInteger    := ATipo;
  zqryaux.ParamByName('porta').AsString    := APorta;
  zqryaux.ParamByName('dtcad').AsDateTime  := AData;

  try
    zqryaux.ExecSQL;

    // pega o último ID (SQLite)
    zqryaux.SQL.Text := 'select last_insert_rowid() as id';
    zqryaux.Open;
    try
      Result := zqryaux.FieldByName('id').AsLargeInt;
    finally
      zqryaux.Close;
    end;

    // Atualiza lista em tela se estiver aberta
    if zqrydevices.Active then
      DevicesOpenAll;

  except
    // se quiser, logue o erro aqui
    Result := -1;
  end;
end;

function TdmBase.DeviceInsertNow(const ANome, APorta: string; ATipo: Integer): Int64;
begin
  Result := DeviceInsert(ANome, APorta, ATipo, Now);
end;


{ ====== SERIAL utilitários ====== }

function TdmBase.SerialConfig(const APort: string; ABaud: Integer): Boolean;
begin
  Result := False;
  try
    DataPortSerial1.Port     := APort;
    DataPortSerial1.BaudRate := ABaud;
    Result := True;
  except
    Result := False;
  end;
end;

function TdmBase.SerialOpen: Boolean;
begin
  Result := False;
  try
    DataPortSerial1.Open;
    Result := True;
  except
    Result := False;
  end;
end;

procedure TdmBase.SerialClose;
begin
  try
    DataPortSerial1.Close;
  except
  end;
end;

function TdmBase.SerialWrite(const S: RawByteString): Boolean;
begin
  // usa Push (já thread-safe no componente)
  Result := DataPortSerial1.Push(S);
end;

function TdmBase.SerialReadLine(out S: string; const TimeoutMs: Integer): Boolean;
var
  t0: QWord;
begin
  S := '';
  t0 := GetTickCount64;

  // espera até ter linha enfileirada pelo OnDataAppear
  repeat
    if SerialPopLine(S) then
      Exit(True);
    Sleep(2);
  until (GetTickCount64 - t0) >= QWord(TimeoutMs);

  Result := False;
end;

function TdmBase.SerialWriteRead(const OutS: RawByteString; out InLine: string; const TimeoutMs: Integer): Boolean;
begin
  InLine := '';
  if not SerialWrite(OutS) then Exit(False);
  Result := SerialReadLine(InLine, TimeoutMs);
end;

procedure TdmBase.SerialFlushBuffer;
begin
  FSerialBuf := '';
  if Assigned(FLines) then FLines.Clear;
end;

function TdmBase.SerialHasLine: Boolean;
begin
  Result := Assigned(FLines) and (FLines.Count > 0);
end;

function TdmBase.SerialPopLine(out L: string): Boolean;
begin
  L := '';
  if not SerialHasLine then Exit(False);
  L := FLines[0];
  FLines.Delete(0);
  Result := True;
end;

{ ====== TCP/HTTP + JSON ====== }

procedure TdmBase.HttpConfig(const ABaseUrl: string; const ATimeoutMs: Integer);
begin
  FBaseUrl       := Trim(ABaseUrl);
  FHttpTimeoutMs := ATimeoutMs;
end;

function TdmBase.TcpGet(const APath: string; out AResponseText: string): Boolean;
var
  C: TFPHTTPClient;
  Url: string;
begin
  Result := False;
  AResponseText := '';
  C := TFPHTTPClient.Create(nil);
  try
    C.AddHeader('Accept','*/*');
    C.ConnectTimeout := FHttpTimeoutMs;
    //C.ReadTimeout    := FHttpTimeoutMs;
    Url := BuildUrl(APath);
    try
      AResponseText := C.Get(Url);
      Result := True;
    except
      AResponseText := '';
      Exit(False);
    end;
  finally
    C.Free;
  end;
end;

function TdmBase.TcpGetJson(const APath: string; out JsonResp: TJSONData): Boolean;
var
  S: string;
begin
  JsonResp := nil;
  if not TcpGet(APath, S) then Exit(False);
  try
    JsonResp := GetJSON(S);
    Result := Assigned(JsonResp);
    if Result and Assigned(FOnTcpJsonReceived) then
      FOnTcpJsonReceived(Self, JsonResp);
  except
    JsonResp := nil;
    Result   := False;
  end;
end;

(*
function TdmBase.TcpPostJsonText(const APath: string; const JsonBody: TJSONData; out AResponseText: string): Boolean;
var
  C: TFPHTTPClient;
  Req, Resp: TStringStream;
  Url: string;
begin
  Result := False;
  AResponseText := '';
  C := TFPHTTPClient.Create(nil);
  Req := TStringStream.Create(JsonBody.AsJSON); // corpo JSON (UTF-8 por padrão no Lazarus)
  Resp := TStringStream.Create('');             // para receber a resposta
  try
    C.AddHeader('Content-Type','application/json; charset=utf-8');
    C.AddHeader('Accept','application/json, */*');
    C.ConnectTimeout := FHttpTimeoutMs;
    //C.ReadTimeout    := FHttpTimeoutMs;

    Url := BuildUrl(APath);
    try
      // Post NÃO retorna string — grava em 'Resp'
      C.Post(Url, Req, Resp);
      AResponseText := Resp.DataString;
      Result := True;
    except
      AResponseText := '';
      Result := False;
    end;
  finally
    Resp.Free;
    Req.Free;
    C.Free;
  end;
end;
*)

(*
function TdmBase.TcpPostJson(const APath: string; const JsonBody: TJSONData; out JsonResp: TJSONData): Boolean;
var
  S: string;
begin
  JsonResp := nil;
  if not TcpPostJsonText(APath, JsonBody, S) then Exit(False);
  try
    JsonResp := GetJSON(S);
    Result := Assigned(JsonResp);
    if Result and Assigned(FOnTcpJsonReceived) then
      FOnTcpJsonReceived(Self, JsonResp);
  except
    JsonResp := nil;
    Result   := False;
  end;
end;
*)

end.

