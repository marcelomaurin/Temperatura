unit main;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls, Buttons,
  ExtCtrls, Menus, PopupNotifier, ComCtrls, LazSerial, FileUtil, LazFileUtils,
  LazSynaSer, Plotpanel, synaser, IdHTTPServer, lNetComponents, LedNumber,
  setmain, registro, peso, setup, lNet, log, IdCustomHTTPServer,
  IdCompressionIntercept, IdSSLOpenSSL, IdSchedulerOfThreadDefault, IdContext,
  // === novos para cliente HTTP, JSON e desenho ===
  IdHTTP, fpjson, jsonparser, Math, LCLType;

Const
  Version    : string = '0.03';
  PortTemp   = 8098;
  ServerName : string = 'localhost';

type

  { Tfrmmain }

  Tfrmmain = class(TForm)
    btConectar: TButton;
    btDesconectar1: TButton;
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

    // ===== novos campos =====
    FHttp  : TIdHTTP;       // cliente HTTP para GET nos IPs
    FTemps : TStringList;   // temperaturas por índice (alinhado com lbIPS)
    FHums  : TStringList;   // umidades por índice

    procedure ListDev();
    function PegaSerial() : String;
    procedure SalvarContexto();
    procedure Setup();
    procedure getPage(aSocket : TLSocket; PeerAddress : string; mensagem: string);
    procedure RespostaHTMLCabecalho(aSocket: TLSocket);

    procedure SalvarLista();     // grava lbIPS.Items em %AppData%/… (ou ~/.config)
    procedure CarregarLista();   // restaura lista salvo no início

    // ===== helpers novos =====
    procedure EnsureListsSize;
    function BuildURL(const Item: string): string;
    function FetchTempHum(const AUrl: string; out ATemp, AHum: Double): Boolean;
    procedure DrawBars(const Panel: TPlotPanel; const Values, Labels: TStrings; const Title, UnitText: string);
  public
  end;

var
  frmmain: Tfrmmain;

implementation

{$R *.lfm}

const
  LIST_FILENAME = 'srvtemp_ips.lst';

{ Tfrmmain }

procedure Tfrmmain.FormCreate(Sender: TObject);
begin
  Lbuffer := '';

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

  // cliente HTTP + armazenamento
  FHttp := TIdHTTP.Create(Self);
  FHttp.ConnectTimeout := 1500;
  FHttp.ReadTimeout    := 2000;
  FHttp.Request.UserAgent := 'srvtemp-monitor/0.1';

  FTemps := TStringList.Create;
  FHums  := TStringList.Create;

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

  FreeAndNil(FHttp);
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
  // coleta periódica controlada pelo Timer2
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

  // redesenha os gráficos
  DrawBars(PlotPanel1, FTemps, lbIPS.Items, 'Temperatura atual por IP', '°C');
  DrawBars(PlotPanel2, FHums,  lbIPS.Items, 'Umidade atual por IP',     '%');
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

{ ===== Persistência da lista (igual ao setmain, na pasta do app) ===== }

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

{ ======= helpers novos ======= }

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
begin
  // Se o usuário digitou só o IP, adiciona http:// e caminho padrão
  if Pos('http', LowerCase(Item)) <> 1 then
    Result := 'http://' + Item + '/ws/temperatura'
  else
    Result := IncludeTrailingPathDelimiter(Item) + 'ws/temperatura';
end;


function Tfrmmain.FetchTempHum(const AUrl: string; out ATemp, AHum: Double): Boolean;
var
  S: string;
  J: TJSONData;
begin
  Result := False;
  ATemp := NaN;
  AHum  := NaN;

  try
    // aumenta timeouts antes da chamada
    FHttp.ConnectTimeout := 5000; // 5 segundos
    FHttp.ReadTimeout    := 6000; // 6 segundos
    S := FHttp.Get(AUrl);
  except
    Exit(False);
  end;

  try
    J := GetJSON(S);
    try
      // espera:
      // { "temperature": 28.1, "humidity": 64.7, "unit_temp":"C", "unit_humidity":"%RH" }
      if (J.JSONType = jtObject) then
      begin
        if (J.FindPath('temperature') <> nil) then
          ATemp := J.FindPath('temperature').AsFloat;
        if (J.FindPath('humidity') <> nil) then
          AHum := J.FindPath('humidity').AsFloat;
        Result := (not IsNan(ATemp)) or (not IsNan(AHum));
      end;
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
  maxVal, v: Double;
  lbl: string;
begin
  C := Panel.Canvas;
  W := Panel.ClientWidth;
  H := Panel.ClientHeight;

  C.Brush.Color := clWhite;
  C.FillRect(0,0,W,H);

  C.Pen.Color := clGray;
  C.Rectangle(0,0,W-1,H-1);

  n := Labels.Count;
  if n = 0 then
  begin
    C.Font.Color := clGrayText;
    C.TextOut(8,8,'Sem itens para monitorar.');
    Exit;
  end;

  // pega maior valor (escala)
  maxVal := 0;
  for i := 0 to n-1 do
    if (i < Values.Count) and TryStrToFloat(Values[i], v) then
      if v > maxVal then maxVal := v;

  if maxVal <= 0 then maxVal := 1;

  margin := 32;
  x0 := margin;
  y0 := H - margin;

  // eixos
  C.Pen.Color := clBlack;
  C.MoveTo(x0, y0);
  C.LineTo(W - margin div 2, y0);     // X
  C.MoveTo(x0, y0);
  C.LineTo(x0, margin div 2);         // Y

  // barras
  barW := Max(10, (W - 2*margin) div Max(1, 2*n));
  x := x0 + 10;

  C.Brush.Color := RGBToColor(80, 160, 255);
  C.Pen.Color := clNavy;

  for i := 0 to n-1 do
  begin
    v := 0;
    if (i < Values.Count) and TryStrToFloat(Values[i], v) then
    begin
      y := y0 - Round((H - 2*margin) * (v / maxVal));
      C.Rectangle(x, y, x + barW, y0);

      // label valor
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

    // rótulo abaixo (encurta se for URL)
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

