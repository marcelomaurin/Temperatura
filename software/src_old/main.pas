unit main;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls, Buttons,
  ExtCtrls, Menus, PopupNotifier, ComCtrls, LazSerial, FileUtil, LazFileUtils,
  LazSynaSer, Plotpanel, synaser, IdHTTPServer, lNetComponents, LedNumber,
  setmain, registro, peso, setup, lNet, log, IdCustomHTTPServer, Math,
  IdCompressionIntercept, IdSSLOpenSSL, IdSchedulerOfThreadDefault, IdContext,
  // JSON e utilidades
  fpjson, jsonparser, LCLType, StrUtils;

Const
  Version    : string = '0.03';
  PortTemp   = 8098;
  ServerName : string = 'localhost';

type

  { Tfrmmain }

  Tfrmmain = class(TForm)
    btConectar: TButton;
    btDesconectar1: TButton;
    btMonitorar1: TToggleBox;
    btsair: TToggleBox;
    btSetup: TButton;
    Button1: TButton;
    Button2: TButton;
    edIP: TEdit;
    IdHTTPServer1: TIdHTTPServer;
    IdHTTPServer2: TIdHTTPServer;
    IdSchedulerOfThreadDefault1: TIdSchedulerOfThreadDefault;
    IdServerCompressionIntercept1: TIdServerCompressionIntercept;
    IdServerIOHandlerSSLOpenSSL1: TIdServerIOHandlerSSLOpenSSL;
    Label1: TLabel;
    Label2: TLabel;
    lbSensoriamento: TLabel;
    lbVersao: TLabel;
    lbstatus: TLabel;
    LazSerial1: TLazSerial;
    lbIPS: TListBox;
    // Mantido para compatibilidade com o .lfm, mas NÃO é utilizado
    LHTTPClientComponent1: TLHTTPClientComponent;
    // TCP “genérico” do seu projeto (server/eco). Continua igual.
    LTCPComponent1: TLTCPComponent;
    MenuItem1: TMenuItem;
    MenuItem2: TMenuItem;
    MenuItem3: TMenuItem;
    btlog: TMenuItem;
    PageControl1: TPageControl;
    Panel1: TPanel;
    PlotPanel1: TPlotPanel;
    PlotPanel2: TPlotPanel;
    popTray: TPopupMenu;
    PopupNotifier1: TPopupNotifier;
    TabSheet1: TTabSheet;
    TabSheet2: TTabSheet;
    TabSheet3: TTabSheet;
    tsMonitorarHum: TTabSheet;
    Timer2: TTimer;
    tsMonitorarTemp: TTabSheet;
    Timer1: TTimer;
    btMonitorar: TToggleBox;
    TrayIcon1: TTrayIcon;
    procedure btConectarClick(Sender: TObject);
    procedure btDesconectar1Click(Sender: TObject);
    procedure btlogClick(Sender: TObject);
    procedure btMonitorar1Change(Sender: TObject);
    procedure btMonitorarChange(Sender: TObject);
    procedure btsairChange(Sender: TObject);
    procedure btSetupClick(Sender: TObject);
    procedure btTestaClick(Sender: TObject);
    procedure Button1Click(Sender: TObject);
    procedure Button2Click(Sender: TObject);
    procedure FormCloseQuery(Sender: TObject; var CanClose: boolean);
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure IdHTTPServer1CommandGet(AContext: TIdContext;
      ARequestInfo: TIdHTTPRequestInfo; AResponseInfo: TIdHTTPResponseInfo);
    procedure LazSerial1BlockSerialStatus(Sender: TObject;
      Reason: THookSerialReason; const Value: string);
    procedure LazSerial1RxData(Sender: TObject);
    procedure LazSerial1Status(Sender: TObject; Reason: THookSerialReason;
      const Value: string);
    procedure LTCPComponent1Connect(aSocket: TLSocket);
    procedure LTCPComponent1Receive(aSocket: TLSocket);
    procedure MenuItem1Click(Sender: TObject);
    procedure MenuItem2Click(Sender: TObject);
    procedure MenuItem3Click(Sender: TObject);
    procedure SdpoSerial1BlockSerialStatus(Sender: TObject;
      Reason: THookSerialReason; const Value: string);
    procedure SdpoSerial1RxData(Sender: TObject);
    procedure Timer1StartTimer(Sender: TObject);
    procedure Timer1StopTimer(Sender: TObject);
    procedure Timer1Timer(Sender: TObject);
    procedure Timer2Timer(Sender: TObject);
  private
    Lbuffer: String;

    // --- controle de reentrância ---
    FHttpInFlight : Boolean;  // true enquanto há um GET em andamento
    FCollecting   : Boolean;  // true enquanto Timer2 está coletando


    // armazenamento para gráficos
    FTemps : TStringList;   // temperaturas (string) alinhadas com lbIPS
    FHums  : TStringList;   // umidades     (string)



    // handlers para o TLTCPComponent criado dinamicamente no GET
    procedure OnConnect(aSocket: TLSocket);
    procedure OnReceive(aSocket: TLSocket);
    procedure OnError(const msg: string; aSocket: TLSocket); // assinatura 0.6.2
    procedure OnDisc(aSocket: TLSocket);

    procedure ListDev();
    function PegaSerial() : String;
    procedure SalvarContexto();
    procedure Setup();
    procedure getPage(aSocket : TLSocket; PeerAddress : string; mensagem: string);
    procedure RespostaHTMLCabecalho(aSocket: TLSocket);

    procedure SalvarLista();     // grava lbIPS.Items
    procedure CarregarLista();   // restaura lista salvo no início

    // ===== helpers =====
    procedure EnsureListsSize;
    function  BuildURL(const Item: string): string;
    procedure ParseUrl(const AUrl: String; out Host, Path: String; out Port: Integer);
    function  ExtractHttpBody(const Resp: String): String;
    function  ReplaceChar(const S: String; OldC, NewC: Char): String;
    function  JsonNumToFloat(const J: TJSONData; out V: Double): Boolean;

    // HTTP GET via TLTCPComponent (modelo do unit registro)
    function  HttpGetViaTcp(const AUrl: string; out Body: string; const TimeoutMs: Integer = 6000): Boolean;

    // Usa o GET acima para pegar temperatura/umidade
    function  FetchTempHum(const AUrl: string; out ATemp, AHum: Double): Boolean;

    procedure DrawBars(const Panel: TPlotPanel; const Values, Labels: TStrings; const Title, UnitText: string);
  public
    // buffers/flags usados na requisição atual
    Req: string;
    chunk : string;
    Resp : string;
    Failed : boolean;
    Done: Boolean;
    procedure LHTTPClientComponent1DoneInput(ASocket: TLHTTPClientSocket);
    function  LHTTPClientComponent1Input(ASocket: TLHTTPClientSocket; ABuffer: PChar; ASize: Integer): Integer;

  end;

var
  frmmain: Tfrmmain;

implementation

{$R *.lfm}

const
  LIST_FILENAME = 'srvtemp_ips.lst';
  CRLF = #13#10;

{ Tfrmmain }

function Tfrmmain.LHTTPClientComponent1Input(ASocket: TLHTTPClientSocket;
  ABuffer: PChar; ASize: Integer): Integer;
var
  chunk: string;
begin
  SetString(chunk, ABuffer, ASize);
  FHttpBody := FHttpBody + chunk;   // acumula corpo
  Result := ASize;                  // MUITO importante: informar bytes consumidos
end;

procedure Tfrmmain.LHTTPClientComponent1DoneInput(ASocket: TLHTTPClientSocket);
begin
  FHttpDone := True;                // marca resposta concluída
end;

function Tfrmmain.HttpGetSync(const AUrl: string; out Body: string; const TimeoutMs: Integer = 6000): Boolean;
var
  Host, Path: string;
  Port: Integer;
  t0: QWord;
begin
  Result := False; Body := '';
  if (AUrl = '') then Exit;

  // evita 2ª requisição concorrente
  if FHttpInFlight then Exit(False);
  FHttpInFlight := True;
  try
    ParseUrl(AUrl, Host, Path, Port);

    FHttpBody := '';
    FHttpDone := False;

    LHTTPClientComponent1.Host    := Host;
    LHTTPClientComponent1.Port    := Port;
    LHTTPClientComponent1.URI     := Path;
    LHTTPClientComponent1.Method  := hmGet;
    LHTTPClientComponent1.Timeout := TimeoutMs;

    LHTTPClientComponent1.SendRequest;

    t0 := GetTickCount64;
    repeat
      // processa eventos de socket, sem liberar UI (evita reentrância pelo Timer)
      LHTTPClientComponent1.CallAction;
      Sleep(1);
      if FHttpDone then Break;
    until (GetTickCount64 - t0) >= QWord(TimeoutMs);

    if not FHttpDone then Exit(False);
    Body := Trim(FHttpBody);
    Result := Body <> '';
  finally
    FHttpInFlight := False;
  end;
end;


procedure Tfrmmain.FormCreate(Sender: TObject);
begin
  Lbuffer := '';

  FHttpInFlight := False;
  FCollecting   := False;

  LHTTPClientComponent1.OnInput     := @LHTTPClientComponent1Input;
  LHTTPClientComponent1.OnDoneInput := @LHTTPClientComponent1DoneInput;


  frmlog      := TfrmLog.Create(Self);
  frmsetup    := Tfrmsetup.Create(Self);
  Fsetmain    := TSetmain.Create();
  Self.Left   := Fsetmain.posx;
  Self.Top    := Fsetmain.posy;

  frmSetup.edSerialPort.Text := FSETMAIN.COMPORT;

  frmRegistrar := TfrmRegistrar.Create(Self);
  frmRegistrar.Identifica();

  // cria, mas não mostra a tela de peso ao iniciar
  frmpeso := TFrmpeso.Create(Self);

  ListDev();
  CarregarLista(); // restaura a lista de IPs

  lbVersao.Caption := Version;

  FTemps := TStringList.Create;
  FHums  := TStringList.Create;

  FCollecting   := False;
  FHttpInFlight := False;

  // estado inicial dos buffers/flags da requisição
  Req    := '';
  chunk  := '';
  Resp   := '';
  Failed := False;
  Done   := False;

  // Timer2 começa desligado; ative pelo toggle
  Timer2.Enabled := False;
end;

procedure Tfrmmain.FormDestroy(Sender: TObject);
begin
  SalvarContexto();
  Fsetmain.Free();
  frmlog.Free;
  frmRegistrar.Free;
  frmSetup.Free;
  frmpeso.Free;

  FreeAndNil(FTemps);
  FreeAndNil(FHums);
end;

procedure Tfrmmain.IdHTTPServer1CommandGet(AContext: TIdContext;
  ARequestInfo: TIdHTTPRequestInfo; AResponseInfo: TIdHTTPResponseInfo);
var
  buffer: ansistring;
begin
  // responde JSON simples com a temperatura exibida na tela de "peso"
  AResponseInfo.ContentType := 'application/json; charset=utf-8';
  buffer := '{'                           + LineEnding +
            '  "rs": {'                   + LineEnding +
            '    "Temperatura": ' +
            '"' + frmpeso.lbTemperatura.Caption + '"' + LineEnding +
            '  }'                         + LineEnding +
            '}'                           + LineEnding;
  AResponseInfo.ContentText := buffer;
end;

procedure Tfrmmain.LazSerial1BlockSerialStatus(Sender: TObject;
  Reason: THookSerialReason; const Value: string);
begin
  if (LazSerial1.Active) then
    lbstatus.Caption := 'Open'
  else
    lbstatus.Caption := 'Close';
  Application.ProcessMessages();
end;

procedure Tfrmmain.LazSerial1RxData(Sender: TObject);
var
  info, line, leftPart, rightPart: string;
  p, pC, pPct: Integer;
  tempStr, humStr: string;
  tempVal, humVal: Double;
  fs: TFormatSettings;

  function PopLineFromBuffer(var S: string): string;
  var
    i: Integer;
  begin
    i := Pos(#13, S);
    if i = 0 then i := Pos(#10, S);
    if i = 0 then Exit('');
    Result := Copy(S, 1, i - 1);
    Delete(S, 1, i);
    while (Length(S) > 0) and ((S[1] = #10) or (S[1] = #13)) do
      Delete(S, 1, 1);
  end;

  function TrimPrefixArrow(const S: string): string;
  var
    T: string;
  begin
    T := TrimLeft(S);
    if (LeftStr(T, 2) = '->') then
      Result := TrimLeft(Copy(T, 3, MaxInt))
    else
      Result := T;
  end;

  function NormalizeNumber(const S: string): string;
  var
    T: string;
  begin
    T := Trim(S);
    T := StringReplace(T, ' ', '', [rfReplaceAll]);
    T := StringReplace(T, ',', '.', [rfReplaceAll]);
    Result := T;
  end;

begin
  fs := DefaultFormatSettings;
  fs.DecimalSeparator := '.';

  if LazSerial1.DataAvailable then
  begin
    info := LazSerial1.ReadData();
    if info <> '' then
      Lbuffer := Lbuffer + info;
  end;

  while True do
  begin
    line := PopLineFromBuffer(Lbuffer);
    if line = '' then Break;

    line := Trim(line);
    if line = '' then Continue;

    line := TrimPrefixArrow(line);

    p := Pos(',', line);
    if p > 0 then
    begin
      leftPart  := Trim(Copy(line, 1, p - 1));
      rightPart := Trim(Copy(line, p + 1, MaxInt));

      pC := Pos('C', leftPart);
      if pC > 1 then
        tempStr := Trim(Copy(leftPart, 1, pC - 1))
      else
        tempStr := Trim(leftPart);

      pPct := Pos('%', rightPart);
      if pPct > 1 then
        humStr := Trim(Copy(rightPart, 1, pPct - 1))
      else
        humStr := Trim(rightPart);

      tempStr := NormalizeNumber(tempStr);
      humStr  := NormalizeNumber(humStr);

      if not TryStrToFloat(tempStr, tempVal, fs) then tempVal := 0;
      if not TryStrToFloat(humStr, humVal, fs) then humVal := 0;

      if Assigned(frmpeso) then
      begin
        frmpeso.Temperatura(FormatFloat('0.00', tempVal));
        frmpeso.Umidade(FormatFloat('0.00', humVal));
      end;

      Application.ProcessMessages;
    end;
  end;
end;

procedure Tfrmmain.LazSerial1Status(Sender: TObject; Reason: THookSerialReason;
  const Value: string);
begin
end;

procedure Tfrmmain.LTCPComponent1Connect(aSocket: TLSocket);
begin
  aSocket.SendMessage('Connected!');
end;

procedure Tfrmmain.LTCPComponent1Receive(aSocket: TLSocket);
var
  mensagem : string;
begin
  aSocket.GetMessage(mensagem);
  frmlog.Log('rec:'+mensagem);
  getPage(aSocket, aSocket.PeerAddress, mensagem);
  LTCPComponent1.CallAction();
end;

procedure Tfrmmain.MenuItem1Click(Sender: TObject);
begin
  Show();
end;

procedure Tfrmmain.MenuItem2Click(Sender: TObject);
begin
  Setup();
end;

procedure Tfrmmain.MenuItem3Click(Sender: TObject);
begin
  // abre a tela de temperatura/umidade quando o usuário quiser
  frmPeso.Show();
end;

procedure Tfrmmain.SdpoSerial1BlockSerialStatus(Sender: TObject;
  Reason: THookSerialReason; const Value: string);
begin
end;

procedure Tfrmmain.SdpoSerial1RxData(Sender: TObject);
begin
end;

procedure Tfrmmain.Timer1StartTimer(Sender: TObject);
begin
  lbstatus.Caption := 'Lendo...';
end;

procedure Tfrmmain.Timer1StopTimer(Sender: TObject);
begin
  lbstatus.Caption := 'Não Lendo';
end;

procedure Tfrmmain.Timer1Timer(Sender: TObject);
begin
  Application.ProcessMessages();
end;

procedure Tfrmmain.Timer2Timer(Sender: TObject);
var
  i: Integer;
  url: string;
  t, h: Double;
begin
  // já coletando? evita reentrada
  if FCollecting then Exit;

  FCollecting := True;
  Timer2.Enabled := False;  // não deixa disparar de novo durante a coleta
  try
    EnsureListsSize;

    for i := 0 to lbIPS.Items.Count - 1 do
    begin
      url := BuildURL(lbIPS.Items[i]);
      if FetchTempHum(url, t, h) then
      begin
        FTemps[i] := FormatFloat('0.00', t);
        FHums[i]  := FormatFloat('0.00', h);
      end
      else
      begin
        FTemps[i] := '';
        FHums[i]  := '';
      end;
    end;

    // redesenha ao final (uma vez só)
    DrawBars(PlotPanel1, FTemps, lbIPS.Items, 'Temperatura atual por IP', '°C');
    DrawBars(PlotPanel2, FHums,  lbIPS.Items, 'Umidade atual por IP',     '%');

  finally
    FCollecting := False;
    Timer2.Enabled := btMonitorar.Checked; // só reabilita se o toggle estiver ON
  end;
end;


procedure Tfrmmain.Button1Click(Sender: TObject);
begin
  //PegaSerial();
end;

procedure Tfrmmain.Button2Click(Sender: TObject);
var
  v: string;
begin
  v := Trim(edIP.Text);
  if v = '' then Exit;

  // evita duplicados
  if lbIPS.Items.IndexOf(v) < 0 then
  begin
    lbIPS.Items.Append(v);
    lbIPS.Sorted := True;
    SalvarLista();
    edIP.Text:= '';

    EnsureListsSize;
  end;
end;

procedure Tfrmmain.FormCloseQuery(Sender: TObject; var CanClose: boolean);
begin
  CanClose := False;
  Hide;
  if not TrayIcon1.Visible then
     TrayIcon1.Visible := True;
end;

procedure Tfrmmain.btConectarClick(Sender: TObject);
begin
  try
    LazSerial1.Close;
    LazSerial1.Device    := FSETMAIN.COMPORT;
    LazSerial1.BaudRate  := TBaudRate(FSETMAIN.BAUDRATE);
    LazSerial1.DataBits  := TDataBits(FSETMAIN.DATABIT);
    LazSerial1.Parity    := TParity(FSETMAIN.PARIDADE);
    LazSerial1.StopBits  := TStopBits(FSETMAIN.STOPBIT);
    LazSerial1.Open;
    Application.ProcessMessages();
  finally
    Timer1.Enabled := not Timer1.Enabled;
    TrayIcon1.Visible := True;
    TrayIcon1.Hint := 'Connected';
    IdHTTPServer1.Active := True; // porta configurada no objeto
    Hide;
  end;
end;

procedure Tfrmmain.btDesconectar1Click(Sender: TObject);
begin
  Timer1.Enabled := False;
  LazSerial1.Close;
  Application.ProcessMessages();
end;

procedure Tfrmmain.btlogClick(Sender: TObject);
begin
  frmLog.Show;
end;

procedure Tfrmmain.btMonitorar1Change(Sender: TObject);
begin
  lbIPS.Items.clear;
end;

procedure Tfrmmain.btMonitorarChange(Sender: TObject);
begin
  Timer2.Enabled := btMonitorar.Checked;
  if Timer2.Enabled then
    Timer2Timer(nil)  // força primeira coleta imediata
  else
  begin
    // limpa gráficos quando parar (opcional)
    FTemps.Clear; FHums.Clear;
    DrawBars(PlotPanel1, FTemps, lbIPS.Items, 'Temperatura atual por IP', '°C');
    DrawBars(PlotPanel2, FHums,  lbIPS.Items, 'Umidade atual por IP',     '%');
  end;
end;

procedure Tfrmmain.btsairChange(Sender: TObject);
begin
  Application.Terminate;
end;

procedure Tfrmmain.btSetupClick(Sender: TObject);
begin
  Setup();
end;

procedure Tfrmmain.btTestaClick(Sender: TObject);
begin
end;

procedure Tfrmmain.ListDev();
begin
  //cbserial.Text :=  PegaSerial();
end;

function Tfrmmain.PegaSerial(): String;
var
  ListOfFiles: TStringList;
  Directory : string;
begin
  Result := '';
  ListOfFiles := TStringList.Create();
  try
    {$IFDEF LINUX}
    Directory := '/dev';
    FindAllFiles(ListOfFiles, Directory, '*', False);
    {$ENDIF}
  finally
    ListOfFiles.Free;
  end;
end;

procedure Tfrmmain.SalvarContexto();
begin
  FSETMAIN.posx := Self.Left;
  FSetMain.posy := Self.Top;
  FSETMAIN.SalvaContexto();
end;

procedure Tfrmmain.Setup();
begin
  frmSetup.edSerialPort.Text   := FSETMAIN.COMPORT;
  frmSetup.cbBaudrate.ItemIndex:= FSETMAIN.BAUDRATE;
  frmSetup.cbDatabits.ItemIndex:= FSETMAIN.DATABIT;
  frmSetup.rgParity.ItemIndex  := FSETMAIN.PARIDADE;
  frmSetup.rgStopbit.ItemIndex := FSETMAIN.STOPBIT;
  frmSetup.Show();
end;

procedure Tfrmmain.RespostaHTMLCabecalho(aSocket: TLSocket);
var
  buffer : string;
begin
  buffer :='HTTP/1.1 200 OK '+#10;
  buffer := buffer +'Content-Type: text/html'+#10;
  buffer := buffer + '<!DOCTYPE HTML>'+#10;
  buffer := buffer + '<html>'+#10;
  buffer := buffer + '<head>'+#10;
  buffer := buffer + '<title>Meu SRV</title>'+#10;
  buffer := buffer + '</head>'+#10;
  buffer := buffer + '<body>'+#10;
  buffer := buffer + 'hello'+#10;
  buffer := buffer + '</body>'+#10;
  buffer := buffer + '</html>'+#10;
end;

procedure Tfrmmain.getPage(aSocket: TLSocket; PeerAddress: string;
  mensagem: string);
begin
  RespostaHTMLCabecalho(aSocket);
end;

{ ===== Persistência da lista ===== }

procedure Tfrmmain.SalvarLista();
var
  path, fn: String;
  SL: TStringList;
begin
  path := GetAppConfigDir(False);
  if not DirectoryExists(path) then
    CreateDir(path);

  fn := IncludeTrailingPathDelimiter(path) + LIST_FILENAME;

  SL := TStringList.Create;
  try
    SL.Assign(lbIPS.Items);
    SL.Text := Trim(SL.Text);
    SL.Sorted := True;
    SL.Duplicates := dupIgnore;
    SL.SaveToFile(fn);
  finally
    SL.Free;
  end;
end;

procedure Tfrmmain.CarregarLista();
var
  path, fn: String;
begin
  path := GetAppConfigDir(False);
  fn   := IncludeTrailingPathDelimiter(path) + LIST_FILENAME;
  if FileExists(fn) then
  begin
    lbIPS.Items.LoadFromFile(fn);
    lbIPS.Sorted := True;
  end;
end;

{ ======= helpers ======= }

procedure Tfrmmain.EnsureListsSize;
var
  n, i: Integer;
begin
  n := lbIPS.Items.Count;

  while FTemps.Count < n do FTemps.Add('');
  while FHums.Count  < n do FHums.Add('');

  while FTemps.Count > n do FTemps.Delete(FTemps.Count-1);
  while FHums.Count  > n do FHums.Delete(FHums.Count-1);

  // garante strings válidas
  for i := 0 to n-1 do
  begin
    if FTemps[i] = '' then FTemps[i] := '';
    if FHums[i]  = '' then FHums[i]  := '';
  end;
end;

function Tfrmmain.BuildURL(const Item: string): string;
  function EndsWith(const S, Suffix: string): Boolean;
  var L, LS: Integer;
  begin
    LS := Length(Suffix); L := Length(S);
    Result := (L>=LS) and (Copy(S, L-LS+1, LS) = Suffix);
  end;
var
  host: string;
begin
  host := Trim(Item);
  if host = '' then Exit('');
  // aceita "ip", "ip:port", "http://ip", "http://ip:port"
  if not EndsWith(LowerCase(host), '/ws/coleta') then
  begin
    if (host <> '') and (host[Length(host)] <> '/') then
      host := host + '/';
    host := host + 'ws/coleta';
  end;
  Result := host;
end;

procedure Tfrmmain.ParseUrl(const AUrl: String; out Host, Path: String; out Port: Integer);
var
  U, HP: String; pSlash, pColon: SizeInt;
begin
  Host := ''; Path := '/'; Port := 80;
  U := Trim(AUrl);
  if U = '' then Exit;

  // remove esquema
  if Pos('://', U) > 0 then
    U := Copy(U, Pos('://', U) + 3, MaxInt);

  // separa host[:port] / path
  pSlash := Pos('/', U);
  if pSlash > 0 then begin
    HP   := Copy(U, 1, pSlash - 1);
    Path := Copy(U, pSlash, MaxInt);
    if Path = '' then Path := '/';
  end else begin
    HP   := U;
    Path := '/';
  end;

  // host : port
  pColon := Pos(':', HP);
  if pColon > 0 then begin
    Host := Copy(HP, 1, pColon - 1);
    Port := StrToIntDef(Copy(HP, pColon + 1, MaxInt), 80);
  end else begin
    Host := HP;
    Port := 80;
  end;

  if Host = '' then Host := '127.0.0.1';
  if Path = '' then Path := '/';
end;

function Tfrmmain.ExtractHttpBody(const Resp: String): String;
var
  p: SizeInt;
begin
  p := Pos(CRLF + CRLF, Resp);
  if p > 0 then Exit(Copy(Resp, p + 4, MaxInt));
  // fallback para LFLF
  p := Pos(#10#10, Resp);
  if p > 0 then Exit(Copy(Resp, p + 2, MaxInt));
  Result := Resp; // já é corpo
end;

function Tfrmmain.ReplaceChar(const S: String; OldC, NewC: Char): String;
var
  R: String; i: SizeInt;
begin
  R := S;
  for i := 1 to Length(R) do
    if R[i] = OldC then R[i] := NewC;
  Result := R;
end;

function Tfrmmain.JsonNumToFloat(const J: TJSONData; out V: Double): Boolean;
var
  FS: TFormatSettings; S: String;
begin
  Result := False; V := NaN;
  if (J = nil) or (J.JSONType = jtNull) then Exit;

  case J.JSONType of
    jtNumber: begin V := J.AsFloat; Result := not IsNan(V); end;
    jtString:
      begin
        S  := Trim(J.AsString);
        S  := ReplaceChar(S, ',', '.');
        FS := DefaultFormatSettings; FS.DecimalSeparator := '.';
        Result := TryStrToFloat(S, V, FS);
      end;
  end;
end;

procedure Tfrmmain.OnConnect(aSocket: TLSocket);
begin
  aSocket.SendMessage(Req);
end;

procedure Tfrmmain.OnReceive(aSocket: TLSocket);
begin
  aSocket.GetMessage(chunk);
  if chunk <> '' then
    Resp := Resp + chunk;
end;

procedure Tfrmmain.OnError(const msg: string; aSocket: TLSocket);
begin
  Failed := True;
  if Assigned(frmlog) then
    frmlog.Log('HTTP GET erro: ' + msg);
  // loop encerrará no Disconnect/timeout
end;

procedure Tfrmmain.OnDisc(aSocket: TLSocket);
begin
  Done := True;
end;

function Tfrmmain.HttpGetViaTcp(const AUrl: string; out Body: string; const TimeoutMs: Integer): Boolean;
var
  Host, Path : string;
  Port: Integer;
  t0: QWord;
  tcp: TLTCPComponent;
begin
  Resp  := '';
  chunk := '';
  Done  := False;
  Failed:= False;
  Body := ''; Result := False;
  if (AUrl = '') then Exit;

  // trava paralelismo
  if FHttpInFlight then Exit(False);
  FHttpInFlight := True;
  try
    ParseUrl(AUrl, Host, Path, Port);

    Req :=
      'GET ' + Path + ' HTTP/1.0' + CRLF +
      'Host: ' + Host + CRLF +
      'Accept: application/json' + CRLF +
      'Connection: close' + CRLF + CRLF;

    Resp := ''; chunk := '';
    Done := False; Failed := False;

    tcp := TLTCPComponent.Create(nil);
    try
      tcp.Timeout      := TimeoutMs;
      tcp.OnConnect    := @OnConnect;
      tcp.OnReceive    := @OnReceive;
      tcp.OnError      := @OnError;      // assinatura compatível 0.6.2
      tcp.OnDisconnect := @OnDisc;

      if not tcp.Connect(Host, Port) then
      begin
        tcp.CallAction;                  // processa início
        if not tcp.Connected then Exit(False);
      end;

      t0 := GetTickCount64;
      repeat
        tcp.CallAction;
        Sleep(1);
        if Done then Break;
      until (GetTickCount64 - t0) >= QWord(TimeoutMs);

      if not Done then
      begin
        tcp.Disconnect(True);
        Exit(False);
      end;

      if Failed or (Resp = '') then Exit(False);

      Body := Trim(ExtractHttpBody(Resp));
      Result := Body <> '';
    finally
      tcp.Free;
    end;
  finally
    FHttpInFlight := False;
  end;
end;

function Tfrmmain.FetchTempHum(const AUrl: string; out ATemp, AHum: Double): Boolean;
var
  S: string; J, N: TJSONData; Obj: TJSONObject;
  OkTemp, OkHum: Boolean;
begin
  if FHttpInFlight then Exit(False);

  Result := False; ATemp := NaN; AHum := NaN;

  if not HttpGetViaTcp(AUrl, S, 6000) then Exit(False);
  if S = '' then Exit(False);

  try
    J := GetJSON(S);
    try
      if J.JSONType <> jtObject then Exit(False);
      Obj := TJSONObject(J);

      N := Obj.FindPath('temperature'); OkTemp := JsonNumToFloat(N, ATemp);
      N := Obj.FindPath('humidity');    OkHum  := JsonNumToFloat(N, AHum);

      Result := OkTemp or OkHum;
    finally
      J.Free;
    end;
  except
    Result := False;
  end;
end;

procedure Tfrmmain.DrawBars(const Panel: TPlotPanel; const Values, Labels: TStrings; const Title, UnitText: string);
var
  C: TCanvas;
  W, H, i, n: Integer;
  margin, barW, x0, y0, x, y: Integer;
  innerW, innerH: Integer;
  denBars: Integer;
  maxVal, v, denY: Double;
  lbl: string;

  function SafeMaxVal: Double;
  begin
    if (IsNan(maxVal)) or (IsInfinite(maxVal)) or (maxVal <= 0) then
      Result := 1.0
    else
      Result := maxVal;
  end;

begin
  C := Panel.Canvas;
  W := Max(1, Panel.ClientWidth);
  H := Max(1, Panel.ClientHeight);

  C.Brush.Color := clWhite;
  C.FillRect(0,0,W,H);

  C.Pen.Color := clGray;
  C.Rectangle(0,0,W-1,H-1);

  n := Labels.Count;
  if n <= 0 then
  begin
    C.Font.Color := clGrayText;
    C.TextOut(8,8,'Sem itens para monitorar.');
    Exit;
  end;

  // maior valor (escala)
  maxVal := 0.0;
  for i := 0 to n-1 do
    if (i < Values.Count) and TryStrToFloat(Values[i], v) then
      if v > maxVal then maxVal := v;

  denY := SafeMaxVal;  // divisor protegido

  margin := 32;
  innerW := W - 2*margin; if innerW < 1 then innerW := 1;
  innerH := H - 2*margin; if innerH < 1 then innerH := 1;

  x0 := margin;
  y0 := H - margin;

  // eixos
  C.Pen.Color := clBlack;
  C.MoveTo(x0, y0);
  C.LineTo(x0 + innerW, y0);     // X
  C.MoveTo(x0, y0);
  C.LineTo(x0, y0 - innerH);     // Y

  // barras (divisor seguro)
  denBars := Max(1, 2*n);
  barW    := Max(10, innerW div denBars);

  x := x0 + 10;

  C.Brush.Color := RGBToColor(80, 160, 255);
  C.Pen.Color := clNavy;

  for i := 0 to n-1 do
  begin
    v := 0.0;
    if (i < Values.Count) and TryStrToFloat(Values[i], v) then
    begin
      y := y0 - Round(innerH * (v / denY)); // sem divisão por zero
      C.Rectangle(x, y, x + barW, y0);

      lbl := FormatFloat('0.0', v) + ' ' + UnitText;
      C.Font.Color := clBlack;
      C.TextOut(x, y - 14, lbl);
    end
    else
    begin
      C.Pen.Color := clSilver;
      C.Brush.Style := bsClear;
      C.Rectangle(x, y0-2, x + barW, y0);
      C.Brush.Style := bsSolid;
      C.Pen.Color := clNavy;
    end;

    // rótulo
    C.Font.Color := clGray;
    lbl := Labels[i];
    if Pos('http', LowerCase(lbl)) = 1 then
    begin
      if Length(lbl) > 20 then
        lbl := Copy(lbl, 1, 20) + '…'
      else
        lbl := Copy(lbl, 1, 20);
    end;
    C.TextOut(x, y0 + 4, lbl);

    x := x + barW + barW;
  end;

  // título
  C.Font.Color := clBlack;
  C.TextOut(margin, 6, Title);
end;


end.

