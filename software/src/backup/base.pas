unit base;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, StrUtils, ZConnection, ZDataset, ZAbstractRODataset,
  DataPortSerial, DataPortHTTP, DateUtils,
  fphttpclient, opensslsockets, fpjson, jsonparser, DB,
  setmain, DataPortUART, LazSerial, LazSynaSer, Math;

type
  TOnTcpJsonReceived = procedure(Sender: TObject; const AJson: TJSONData) of object;

  { TdmBase }

  TdmBase = class(TDataModule)
    DataPortHTTP1: TDataPortHTTP;
    dsdevices: TDataSource;
    LazSerial1: TLazSerial;
    tbltipos: TZTable;
    ZConnection1: TZConnection;
    zqryaux: TZQuery;
    zqrymedidas: TZQuery;
    zqrydevices: TZQuery;
    tbldevices: TZTable;
    procedure DataModuleCreate(Sender: TObject);
    procedure DataModuleDestroy(Sender: TObject);
    procedure DataPortSerial1DataAppear(Sender: TObject; const AMsg: AnsiString);
    procedure LazSerial1RxData(Sender: TObject);
    procedure LazSerial1Status(Sender: TObject; Reason: THookSerialReason;
      const Value: string);
  private
    FBaseUrl: string;
    FHttpTimeoutMs: Integer;
    FOnTcpJsonReceived: TOnTcpJsonReceived;

    FSerialBuf: RawByteString;
    FLines: TStringList;

    // ==== CACHE último valor por (device,tipo) ====
    FLastVals: TStringList; // itens no formato "id:tipo=valor"
    function  LastKey(const AIdDevice: Int64; ATipoMedida: Integer): string;
    function  TryGetLastFromCache(const Key: string; out V: Double): Boolean;
    procedure UpdateCache(const Key: string; const V: Double);
    procedure ResetLastValues;

    function BuildUrl(const APath: string): string;

    function BaudFromCode(Code: Integer): Integer;
    function DataBitsFromCode(Code: Integer): Integer;
    function ParityCharFromCode(Code: Integer): AnsiChar;
    function StopBitsFromCode(Code: Integer): TSerialStopBits;

    procedure ProcessSerialBuffer;
  public
    serialdevid : integer; //Posicao da serial devid

    { ====== BANCO ====== }
    procedure AplicaConfigBanco;
    procedure InsertDevice;
    procedure EditDevices;

    { ====== SERIAL ====== }
    function AtualizaConSerial(const AReabrir: Boolean = True): Boolean;

    function GetIDPorta(const AID: Int64): string;

    { ====== TCP/HTTP + JSON ====== }
    procedure HttpConfig(const ABaseUrl: string; const ATimeoutMs: Integer = 5000);
    function TcpGet(const APath: string; out AResponseText: string): Boolean;
    function TcpGetJson(const APath: string; out JsonResp: TJSONData): Boolean;

    { ===== DEVICES ===== }
    function DevicesOpenAll: Boolean;
    function DevicesSearchByName(const ANameLike: string): Boolean;
    function DeviceFindById(const AId: Int64): Boolean;

    function DeviceInsert(): Int64; overload;
    function DeviceInsertNow(): Int64; overload;

    property OnTcpJsonReceived: TOnTcpJsonReceived read FOnTcpJsonReceived write FOnTcpJsonReceived;

    function BuscaDevices(const AFiltro: string = ''): Boolean;
    function ListaPortasTipo1(out APortas: TStringList): Boolean;
    function ListaPortasTipo2(out APortas: TStringList): Boolean;
    function ConsultaIPTempHum(const AIp: string; out AJson: TJSONData): Boolean;

    // <- ALTERADA: só grava se mudou; retorna 0 quando não houve mudança
    function RegistraMedida(const AIdDevice: Int64; ATipoMedida: Integer;
      const AValor: Double; const ADthrCad: TDateTime = 0): Int64;

    function BuscaMedidas(const AIdDevice: integer;
      const ADataInicio, ADataFim: TDateTime): Boolean;
    function GeraRelatorioDiario(const ADataInicio, ADataFim: TDateTime): Boolean;
    function BuscaDeviceIdPorNome(const ANome: string): Int64;
  end;

var
  dmBase: TdmBase;

implementation

{$R *.lfm}

uses
  // para usar main.FSETMAIN sem ciclo na interface
  main;

{ ---------- helper SQL genérico ---------- }
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

{ ---------- BUSCAS SIMPLES ---------- }

function TdmBase.BuscaDeviceIdPorNome(const ANome: string): Int64;
begin
  Result := 0;
  zqryaux.Close;
  zqryaux.SQL.Text :=
    'select id_device from devices where nome = :n limit 1';
  zqryaux.ParamByName('n').AsString := ANome;
  try
    zqryaux.Open;
    if not zqryaux.IsEmpty then
      Result := zqryaux.FieldByName('id_device').AsLargeInt;
  except
    Result := 0;
  end;
end;

function TdmBase.BuscaDevices(const AFiltro: string): Boolean;
begin
  zqryaux.Close;
  if Trim(AFiltro) = '' then
    zqryaux.SQL.Text := 'select * from devices order by nome'
  else
  begin
    zqryaux.SQL.Text :=
      'select * from devices where nome like :n order by nome';
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

function TdmBase.ListaPortasTipo1(out APortas: TStringList): Boolean;
var
  porta: string;
  idv:   Int64;
begin
  if Assigned(APortas) then
    FreeAndNil(APortas);
  APortas := TStringList.Create;
  APortas.OwnsObjects := False;

  Result := False;

  zqryaux.Close;
  zqryaux.SQL.Text :=
    'select * '+
    'from devices '+
    'where tipo = 1 '+
    'order by porta';


  try
    zqryaux.Open;
    while not zqryaux.EOF do
    begin
      porta := zqryaux.FieldByName('porta').AsString;
      idv   := zqryaux.FieldByName('id_device').AsLargeInt;
      APortas.AddObject(porta, TObject(PtrInt(idv)));
      zqryaux.Next;
    end;
    Result := (APortas.Count > 0);
  except
    // mantém False
  end;
end;

function TdmBase.ListaPortasTipo2(out APortas: TStringList): Boolean;
var
  porta: string;
  idv:   Int64;
begin
  if Assigned(APortas) then
    FreeAndNil(APortas);
  APortas := TStringList.Create;
  APortas.OwnsObjects := False;

  Result := False;

  zqryaux.Close;
  zqryaux.SQL.Text :=
    'select * '+
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
      porta := zqryaux.FieldByName('porta').AsString;
      idv   := zqryaux.FieldByName('id_device').AsLargeInt;
      APortas.AddObject(porta, TObject(PtrInt(idv)));
      zqryaux.Next;
    end;
    Result := (APortas.Count > 0);
  except
    // mantém False
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
  if TcpGetJson(baseUrl + '/dht', AJson) then Exit(True);
  FreeAndNil(AJson);

  if TcpGetJson(baseUrl + '/json', AJson) then Exit(True);
  FreeAndNil(AJson);

  if TcpGetJson(baseUrl + '/', AJson) then
  begin
    if (AJson.JSONType in [jtObject, jtArray]) then Exit(True)
    else FreeAndNil(AJson);
  end;
end;

{ ========== CACHE helpers ========== }

function TdmBase.LastKey(const AIdDevice: Int64; ATipoMedida: Integer): string;
begin
  Result := IntToStr(AIdDevice) + ':' + IntToStr(ATipoMedida);
end;

function TdmBase.TryGetLastFromCache(const Key: string; out V: Double): Boolean;
var
  ix: Integer;
begin
  Result := False;
  V := 0;
  if not Assigned(FLastVals) then Exit;
  ix := FLastVals.IndexOfName(Key);
  if ix >= 0 then
    Result := TryStrToFloat(FLastVals.ValueFromIndex[ix], V);
end;

procedure TdmBase.UpdateCache(const Key: string; const V: Double);
var
  ix: Integer;
  fs: TFormatSettings;
begin
  if not Assigned(FLastVals) then Exit;
  fs := DefaultFormatSettings;
  fs.DecimalSeparator := '.'; // grava invariável
  ix := FLastVals.IndexOfName(Key);
  if ix >= 0 then
    FLastVals.ValueFromIndex[ix] := FloatToStr(V, fs)
  else
    FLastVals.Add(Key + '=' + FloatToStr(V, fs));
end;

procedure TdmBase.ResetLastValues;
begin
  if Assigned(FLastVals) then FLastVals.Clear;
end;

function TdmBase.RegistraMedida(const AIdDevice: Int64; ATipoMedida: Integer;
  const AValor: Double; const ADthrCad: TDateTime): Int64;
var
  d: TDateTime;
  key: string;
  lastV: Double;
  hasLast: Boolean;
  epsilon: Double;
begin
  // Retornos: -1 erro; 0 = não inseriu (sem mudança); >0 id inserido
  Result := -1;
  if (AIdDevice <= 0) then Exit;

  epsilon := 0.01; // tolerância para “mudou”

  if (not ZConnection1.Connected) then
  try
    ZConnection1.Connect;
  except
    Exit;
  end;

  key := LastKey(AIdDevice, ATipoMedida);

  // 1) cache
  hasLast := TryGetLastFromCache(key, lastV);

  // 2) se não tem no cache, busca último no banco (bootstrap)
  if not hasLast then
  begin
    zqryaux.Close;
    zqryaux.SQL.Text :=
      'select valor '+
      '  from medidas '+
      ' where id_device = :iddev '+
      '   and tipomedida = :t '+
      ' order by dthrcad desc, rowid desc '+
      ' limit 1';
    zqryaux.ParamByName('iddev').AsLargeInt := AIdDevice;
    zqryaux.ParamByName('t').AsInteger      := ATipoMedida;
    try
      zqryaux.Open;
      if not zqryaux.EOF then
      begin
        lastV   := zqryaux.FieldByName('valor').AsFloat;
        hasLast := True;
        UpdateCache(key, lastV);
      end;
    finally
      zqryaux.Close;
    end;
  end;

  // 3) comparação
  if hasLast and (Abs(AValor - lastV) < epsilon) then
  begin
    Result := 0; // sem mudança -> não grava
    Exit;
  end;

  // 4) insere
  if ADthrCad > 0 then d := ADthrCad else d := Now;

  zqryaux.Close;
  zqryaux.SQL.Text :=
    'insert into medidas (dthrcad, tipomedida, valor, id_device) '+
    'values (:d, :t, :v, :iddev)';
  try
    zqryaux.ParamByName('d').AsDateTime     := d;
    zqryaux.ParamByName('t').AsInteger      := ATipoMedida; // 0=temp, 1=humidade
    zqryaux.ParamByName('v').AsFloat        := AValor;
    zqryaux.ParamByName('iddev').AsLargeInt := AIdDevice;
    zqryaux.ExecSQL;

    zqryaux.SQL.Text := 'select last_insert_rowid() as id';
    zqryaux.Open;
    try
      if not zqryaux.FieldByName('id').IsNull then
      begin
        Result := zqryaux.FieldByName('id').AsLargeInt;
        // Atualiza cache somente após sucesso
        UpdateCache(key, AValor);
      end;
    finally
      zqryaux.Close;
    end;
  except
    // mantém -1
  end;
end;

{ ---------- ciclo de vida ---------- }

procedure TdmBase.DataModuleCreate(Sender: TObject);
begin
  AplicaConfigBanco;

  FBaseUrl       := '';
  FHttpTimeoutMs := 5000;

  FSerialBuf := '';
  FLines     := TStringList.Create;

  // inicializa cache de últimos valores
  FLastVals := TStringList.Create;
  FLastVals.CaseSensitive := False;
  FLastVals.Sorted        := False;
  FLastVals.Duplicates    := dupIgnore;
  ResetLastValues;
end;

procedure TdmBase.DataModuleDestroy(Sender: TObject);
begin
  FreeAndNil(FLines);
  FreeAndNil(FLastVals);
end;

procedure TdmBase.DataPortSerial1DataAppear(Sender: TObject; const AMsg: AnsiString);
begin
  FSerialBuf := FSerialBuf + AMsg;
  ProcessSerialBuffer;

  if Length(FSerialBuf) > 1*1024*1024 then
    FSerialBuf := RightStr(FSerialBuf, 64*1024);
end;

procedure TdmBase.LazSerial1RxData(Sender: TObject);

  // Extrai o primeiro número (sinal e separador decimal aceitos) de uma substring
  function ExtractNumber(const S: string; out V: Double): Boolean;
  var
    numStr: string;
    ch: Char;
    i: Integer;
    fs: TFormatSettings;
  begin
    Result := False;
    numStr := '';

    for i := 1 to Length(S) do
    begin
      ch := S[i];
      if (ch in ['0'..'9']) or (ch in ['+','-']) or (ch in ['.',',']) then
        numStr := numStr + ch
      else
      begin
        if numStr <> '' then Break;
      end;
    end;

    numStr := Trim(numStr);
    if numStr = '' then Exit;

    fs := DefaultFormatSettings;
    fs.DecimalSeparator := '.';
    Result := TryStrToFloat(StringReplace(numStr, ',', '.', [rfReplaceAll]), V, fs);
    if not Result then
    begin
      fs := DefaultFormatSettings;
      fs.DecimalSeparator := ',';
      Result := TryStrToFloat(StringReplace(numStr, '.', ',', [rfReplaceAll]), V, fs);
    end;
  end;

  function SliceAfterTokenUntil(const S, Token: string; const Stops: TSysCharSet): string;
  var
    p, j: Integer;
  begin
    Result := '';
    p := Pos(Token, S);
    if p <= 0 then Exit;
    Inc(p, Length(Token));
    j := p;
    while (j <= Length(S)) and not (S[j] in Stops) do
      Inc(j);
    Result := Trim(Copy(S, p, j - p));
  end;

var
  raw, norm, linha, sTemp, sHum: string;
  tempVal, humVal: Double;
  okT, okH: Boolean;
  sl: TStringList;
  i: Integer;
begin
  if not LazSerial1.DataAvailable then Exit;

  raw := LazSerial1.ReadData;  // pode vir 1+ linhas
  frmmain.RegistraLog(raw);
  if raw = '' then Exit;

  // Normaliza quebras de linha para LF e percorre cada linha
  norm := StringReplace(raw, #13#10, #10, [rfReplaceAll]);
  norm := StringReplace(norm, #13, #10, [rfReplaceAll]);

  sl := TStringList.Create;
  try
    {$IFDEF WINDOWS}
    sl.LineBreak := #10; // garante que #10 seja tratado como separador no Windows
    {$ENDIF}
    sl.Text := norm;

    for i := 0 to sl.Count - 1 do
    begin
      linha := Trim(sl[i]);
      if linha = '' then Continue;

      // Exemplo esperado:
      // [DHT] T=24.8C -> cal=24.8C | H=45.1% -> cal=45.1%
      sTemp := SliceAfterTokenUntil(linha, 'T=', ['C','c']);
      sHum  := SliceAfterTokenUntil(linha, 'H=', ['%']);

      okT := ExtractNumber(sTemp, tempVal);
      okH := ExtractNumber(sHum,  humVal);

      if (serialdevid > 0) then
      begin
        if okT then
          dmBase.RegistraMedida(serialdevid, 0, tempVal);
        if okH then
          dmBase.RegistraMedida(serialdevid, 1, humVal);
      end;
    end;
  finally
    sl.Free;
  end;
end;


procedure TdmBase.LazSerial1Status(Sender: TObject; Reason: THookSerialReason;
  const Value: string);
begin
end;

procedure TdmBase.ProcessSerialBuffer;
var
  pLF: SizeInt;
  line: RawByteString;
  s: string;
begin
  while True do
  begin
    pLF := Pos(#10, FSerialBuf);
    if pLF = 0 then Break;

    line := Copy(FSerialBuf, 1, pLF - 1);
    if (Length(line) > 0) and (line[Length(line)] = #13) then
      SetLength(line, Length(line) - 1);

    Delete(FSerialBuf, 1, pLF);

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

  ZConnection1.Protocol := 'sqlite';

  if Assigned(FSETMAIN) and (Trim(FSETMAIN.LocalBanco) <> '') then
  begin
    ZConnection1.Database := FSETMAIN.LocalBanco;
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

procedure TdmBase.InsertDevice;
begin
  tbldevices.Open;
  tbldevices.Insert;
end;

procedure TdmBase.EditDevices;
begin
  tbldevices.Open;
  tbldevices.Edit;
end;

{ ---------- mapeamento serial ---------- }

function TdmBase.BaudFromCode(Code: Integer): Integer;
begin
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
    1: Result := stb2;
  else
    Result := stb1;
  end;
end;

function TdmBase.AtualizaConSerial(const AReabrir: Boolean): Boolean;
begin
  Result := False;
  if not Assigned(FSETMAIN) then Exit(False);

  try
    if LazSerial1.Active then
      LazSerial1.Close;
  except end;

  try
    // configure aqui se necessário
    if AReabrir then
      LazSerial1.Open;

    Result := True;
  except
    Result := False;
  end;
end;

function TdmBase.GetIDPorta(const AID: Int64): string;
begin
  Result := '';

  zqryaux.Close;
  zqryaux.SQL.Text :=
    'select porta '+
    'from devices '+
    'where id_device = :id';
  zqryaux.ParamByName('id').AsLargeInt := AID;

  try
    zqryaux.Open;
    if not zqryaux.IsEmpty then
      Result := Trim(zqryaux.FieldByName('porta').AsString);
  except
    on E: Exception do
      WriteLn('Erro em GetIDPorta: ', E.Message);
  end;
end;

function TdmBase.BuildUrl(const APath: string): string;
var
  L, R: string;
begin
  if FBaseUrl = '' then Exit(APath);
  L := FBaseUrl; R := APath;
  if (L <> '') and (L[Length(L)] = '/') then SetLength(L, Length(L) - 1);
  if (R <> '') and (R[1] = '/') then Delete(R, 1, 1);
  Result := L + '/' + R;
end;

{ ---------- DEVICES (consultas) ---------- }

function TdmBase.DevicesOpenAll: Boolean;
begin
  Result := TryOpenDevicesWithSQL(
    zqrydevices,
    'select d.*, '+
    '       t.* '+
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
    'select d.*, '+
    '       t.id_tipo as id_tipo '+
    'from devices d '+
    'join tipos t on d.tipo = t.id_tipo '+
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
    'select d.*, '+
    '       t.id_tipo as id_tipo '+
    'from devices d '+
    'join tipos t on d.tipo = t.id_tipo '+
    'where (d.id = :id or d.id_device = :id) '+
    'limit 1',
    'id', AId
  );

  if Result and (dsdevices.DataSet <> zqrydevices) then
    dsdevices.DataSet := zqrydevices;
end;

{ ---------- MEDIDAS ---------- }

function TdmBase.BuscaMedidas(const AIdDevice: integer;
  const ADataInicio, ADataFim: TDateTime): Boolean;
begin
  zqrymedidas.Close;
  zqrymedidas.SQL.Text :=
    'select m.* '+
    'from medidas m '+
    'where (m.id_device = :p_id_device) '+
    '  and (m.dthrcad >= :p_ini) '+
    '  and (m.dthrcad <= :p_fim) '+
    'order by m.dthrcad';

  zqrymedidas.ParamByName('p_id_device').AsLargeInt := AIdDevice;
  zqrymedidas.ParamByName('p_ini').AsDateTime := StartOfTheDay(ADataInicio);
  zqrymedidas.ParamByName('p_fim').AsDateTime := EndOfTheDay(ADataFim);

  zqrymedidas.Open;
  Result := not zqrymedidas.IsEmpty;
end;

function TdmBase.GeraRelatorioDiario(const ADataInicio, ADataFim: TDateTime): Boolean;
begin
  zqrymedidas.Close;
  zqrymedidas.SQL.Text :=
    'WITH base AS ('+
    '  SELECT '+
    '    date(m.dthrcad) AS dia_iso,'+          //-- YYYY-MM-DD para agrupar/ordenar
    '    m.tipomedida    AS tipomedida,'+
    '    m.valor         AS valor,'+
    '    m.dthrcad       AS dthrcad,'+
    '    ROW_NUMBER() OVER ('+
    '      PARTITION BY date(m.dthrcad), m.tipomedida '+
    '      ORDER BY m.dthrcad DESC '+
    '    ) AS rn '+
    '  FROM medidas m '+
    '  WHERE m.dthrcad >= :p_ini AND m.dthrcad <= :p_fim '+
    ') '+
    'SELECT '+
    // dia exibido como dd/mm/YYYY e tipado como CHAR(10) p/ evitar Memo
    '  CAST(strftime(''%d/%m/%Y'', b.dia_iso) AS CHAR(10)) AS dia, '+
    // tipo como CHAR(12): "Temperatura" (11) / "Humidade" (8)
    '  CAST(CASE b.tipomedida '+
    '         WHEN 0 THEN ''Temperatura'' '+
    '         WHEN 1 THEN ''Humidade'' '+
    '         ELSE ''Desconhecido'' '+
    '      END AS CHAR(12)) AS tipo, '+
    '  MIN(b.valor) AS valor_min, '+
    '  MAX(b.valor) AS valor_max, '+
    '  MAX(CASE WHEN b.rn = 1 THEN b.valor END) AS valor_ultimo, '+
    // coluna oculta só para ordenar corretamente por data
    '  b.dia_iso AS dia_ord '+
    'FROM base b '+
    'GROUP BY b.dia_iso, b.tipomedida '+
    'ORDER BY b.dia_iso, b.tipomedida;';

  zqrymedidas.ParamByName('p_ini').AsDateTime := StartOfTheDay(ADataInicio);
  zqrymedidas.ParamByName('p_fim').AsDateTime := EndOfTheDay(ADataFim);

  zqrymedidas.Open;
  Result := not zqrymedidas.IsEmpty;
end;






{ ---------- INSERT DEVICE ---------- }

function TdmBase.DeviceInsert(): Int64;
begin
  Result := -1;
  try
    if tbldevices.State in [dsInsert, dsEdit] then
      tbldevices.Post;

    if tbldevices.CachedUpdates then
      tbldevices.ApplyUpdates;

    zqryaux.Close;
    zqryaux.SQL.Text := 'select last_insert_rowid() as id';
    zqryaux.Open;
    try
      if not zqryaux.FieldByName('id').IsNull then
        Result := zqryaux.FieldByName('id').AsLargeInt;
    finally
      zqryaux.Close;
    end;

    DevicesOpenAll;
  except
    Result := -1;
  end;
end;

function TdmBase.DeviceInsertNow(): Int64;
begin
  if tbldevices.State = dsInsert then
    if tbldevices.FindField('dtcad') <> nil then
      tbldevices.FieldByName('dtcad').AsDateTime := Now;
  Result := DeviceInsert();
end;

{ ---------- TCP/HTTP ---------- }

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

