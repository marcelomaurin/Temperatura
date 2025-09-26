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
    tbldevicesdtcad: TZDateTimeField;
    tbldevicesnome: TZRawStringField;
    tbldevicesporta: TZRawCLobField;
    tbldevicestipo: TZInt64Field;
    tbltipos: TZTable;
    ZConnection1: TZConnection;
    zqryaux: TZQuery;
    zqrydevices: TZQuery;
    tbldevices: TZTable;
    procedure DataModuleCreate(Sender: TObject);
    procedure DataModuleDestroy(Sender: TObject);
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
    procedure editarDevice;

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

    { ===== DEVICES (consulta/insert) ===== }
    // Abre todos os devices no zqrydevices (dataset do dsdevices)
    function DevicesOpenAll: Boolean;

    // Busca por nome (LIKE %ANameLike%). Resultado em zqrydevices
    function DevicesSearchByName(const ANameLike: string): Boolean;

    // Carrega um device específico por ID (aceita id ou id_device)
    function DeviceFindById(const AId: Int64): Boolean;

    // INSERT: com ou sem dtcad informado; retorna o ID (SQLite)
    function DeviceInsert(): Int64; overload;
    function DeviceInsertNow(): Int64; overload;

    property OnTcpJsonReceived: TOnTcpJsonReceived read FOnTcpJsonReceived write FOnTcpJsonReceived;

    function BuscaDevices(const AFiltro: string = ''): Boolean;
    function ListaPortasTipo2(out APortas: TStringList): Boolean;
    function ConsultaIPTempHum(const AIp: string; out AJson: TJSONData): Boolean;

  end;

var
  dmBase :  TdmBase;

implementation

{$R *.lfm}

{ TdmBase }

function TdmBase.BuscaDevices(const AFiltro: string): Boolean;
begin
  zqryaux.Close;

  if Trim(AFiltro) = '' then
  begin
    // tenta id_device; se seu schema usa "id", o SELECT abaixo já lida via alias
    zqryaux.SQL.Text :=
      'select '+
      '  * '+
      'from devices '+
      'order by nome';
  end
  else
  begin
    zqryaux.SQL.Text :=
      'select '+
      ' * '+
      'from devices '+
      'where nome like :n '+
      'order by nome';
    zqryaux.ParamByName('n').AsString := '%' + AFiltro + '%';
  end;

  try
    zqryaux.Open;
    Result := not zqryaux.IsEmpty;
  except
    zqryaux.Close;
    Result := False;
  end;
end;

function TdmBase.ListaPortasTipo2(out APortas: TStringList): Boolean;
begin
  if Assigned(APortas) then APortas.Clear else APortas := TStringList.Create;
  Result := False;

  zqryaux.Close;
  zqryaux.SQL.Text :=
    'select distinct porta '+
    'from devices '+
    'where tipo = :t '+
    '  and porta is not null '+
    '  and trim(porta) <> '''' '+
    'order by porta';
  zqryaux.ParamByName('t').AsInteger := 2;

  try
    zqryaux.Open;
    while not zqryaux.EOF do
    begin
      APortas.Add(zqryaux.FieldByName('porta').AsString);
      zqryaux.Next;
    end;
    Result := (APortas.Count > 0);
  except
    // mantém Result=False
  end;
end;


function TdmBase.ConsultaIPTempHum(const AIp: string; out AJson: TJSONData): Boolean;
  function NormalizeBase(const S: string): string;
  begin
    if (Pos('http://', LowerCase(S)) = 1) or (Pos('https://', LowerCase(S)) = 1) then
      Exit(S);
    Result := 'http://' + S;
  end;
var
  baseUrl: string;
begin
  Result := False;
  AJson  := nil;

  baseUrl := Trim(NormalizeBase(AIp));
  // Tenta endpoints mais comuns
  if TcpGetJson(baseUrl + '/dht', AJson) then Exit(True);
  if Assigned(AJson) then FreeAndNil(AJson);

  if TcpGetJson(baseUrl + '/json', AJson) then Exit(True);
  if Assigned(AJson) then FreeAndNil(AJson);

  if TcpGetJson(baseUrl + '/', AJson) then
  begin
    if (AJson.JSONType in [jtObject, jtArray]) then
      Exit(True)
    else
      FreeAndNil(AJson);
  end;
end;



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
  // tenta dll no mesmo dir do .exe
  dllPath := ExtractFilePath(ParamStr(0)) + 'sqlite3.dll';
  if FileExists(dllPath) then
    ZConnection1.LibraryLocation := dllPath;

  ZConnection1.Protocol := 'sqlite';

  if Assigned(FSETMAIN) and (Trim(FSETMAIN.LocalBanco) <> '') then
  begin
    ZConnection1.Database := FSETMAIN.LocalBanco;
    // fallback: se houver uma sqlite3.dll ao lado do banco, usa também
    dllPath := ExtractFilePath(FSETMAIN.LocalBanco) + 'sqlite3.dll';
    if FileExists(dllPath) then
      ZConnection1.LibraryLocation := dllPath;
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

procedure TdmBase.editarDevice;
begin
  tbldevices.Open;
  tbldevices.Insert;
end;

function TdmBase.BaudFromCode(Code: Integer): Integer;
begin
  // mapeamento típico (ajuste conforme teu SetMain)
  case Code of
    0:  Result := 1200;
    1:  Result := 1800;
    2:  Result := 2400;
    3:  Result := 4800;
    4:  Result := 9600;
    5:  Result := 14400;
    6:  Result := 19200;
    7:  Result := 38400;
    8:  Result := 56000;
    9:  Result := 57600;
    10: Result := 115200;
    11: Result := 230400;
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
    0: Result := 'N'; // None
    1: Result := 'O'; // Odd
    2: Result := 'E'; // Even
    3: Result := 'M'; // Mark
    4: Result := 'S'; // Space
  else
    Result := 'N';
  end;
end;

function TdmBase.StopBitsFromCode(Code: Integer): TSerialStopBits;
begin
  case Code of
    0: Result := stb1;
    1: Result := stb2; // (use stb1_5 se teu componente suportar)
  else
    Result := stb1;
  end;
end;

function TdmBase.AtualizaConSerial(const AReabrir: Boolean): Boolean;
begin
  Result := False;
  if not Assigned(FSETMAIN) then Exit(False);

  try
    if DataPortSerial1.Active then
      DataPortSerial1.Close;
  except end;

  try
    DataPortSerial1.Port     := FSETMAIN.COMPORT;
    DataPortSerial1.BaudRate := BaudFromCode(FSETMAIN.BAUDRATE);
    DataPortSerial1.DataBits := DataBitsFromCode(FSETMAIN.DATABIT);
    DataPortSerial1.Parity   := ParityCharFromCode(FSETMAIN.PARIDADE);
    DataPortSerial1.StopBits := StopBitsFromCode(FSETMAIN.STOPBIT);
    // FlowControl mantém default (sfcNone), ajuste aqui se desejar

    if AReabrir then
      DataPortSerial1.Open;

    Result := True;
  except
    Result := False;
  end;
end;

function TdmBase.BuildUrl(const APath: string): string;
var
  L, R: string;
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
  // JOIN explícito evita produto cartesiano e é portável
  Result := TryOpenDevicesWithSQL(
    zqrydevices,
    'select d.*, t.* '+
    'from devices d '+
    'join tipo t on d.tipo = t.id_tipo '+
    'order by d.nome'
  );

  if Result and (dsdevices.DataSet <> zqrydevices) then
    dsdevices.DataSet := zqrydevices;
end;

function TdmBase.DevicesSearchByName(const ANameLike: string): Boolean;
var
  likeStr: string;
begin
  likeStr := '%' + ANameLike + '%';

  Result := TryOpenDevicesWithSQL(
    zqrydevices,
    'select d.*, t.* '+
    'from devices d '+
    'join tipo t on d.tipo = t.id_tipo '+
    'where d.nome like :n '+
    'order by d.nome',
    '', 0, 'n', likeStr
  );

  if Result and (dsdevices.DataSet <> zqrydevices) then
    dsdevices.DataSet := zqrydevices;
end;

function TdmBase.DeviceFindById(const AId: Int64): Boolean;
begin
  Result := TryOpenDevicesWithSQL(
    zqrydevices,
    'select d.*, t.* '+
    'from devices d '+
    'join tipo t on d.tipo = t.id_tipo '+
    'where (d.id = :id or d.id_device = :id) '+
    'limit 1',
    'id', AId
  );

  if Result and (dsdevices.DataSet <> zqrydevices) then
    dsdevices.DataSet := zqrydevices;
end;

function TdmBase.DeviceInsert(): Int64;
begin
  Result := -1;
  try
    // Garante que o registro atual seja gravado
    if tbldevices.State in [dsInsert, dsEdit] then
      tbldevices.Post;

    // Se estiver usando CachedUpdates, aplica agora
    if tbldevices.CachedUpdates then
      tbldevices.ApplyUpdates;

    // Pega o último ID gerado (SQLite) na MESMA conexão
    zqryaux.Close;
    zqryaux.SQL.Text := 'select last_insert_rowid() as id';
    zqryaux.Open;
    try
      if not zqryaux.FieldByName('id').IsNull then
        Result := zqryaux.FieldByName('id').AsLargeInt;
    finally
      zqryaux.Close;
    end;

    // Refresh da lista padrão
    DevicesOpenAll;

  except
    Result := -1;
    // (opcional) logar exceção
  end;
end;

function TdmBase.DeviceInsertNow(): Int64;
begin
  // Preenche dtcad no registro ATUAL (sem parâmetros), se houver o campo
  if tbldevices.State = dsInsert then
    if tbldevices.FindField('dtcad') <> nil then
      tbldevices.FieldByName('dtcad').AsDateTime := Now;

  Result := DeviceInsert();
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

end.

