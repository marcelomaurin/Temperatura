unit main;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls, Buttons,
  ExtCtrls, Menus, PopupNotifier, LazSerial, FileUtil, LazFileUtils, LazSynaSer,
  synaser, IdHTTPServer, lNetComponents, LedNumber, setmain, registro, peso,
  setup, lNet, log, IdCustomHTTPServer,  IdCompressionIntercept,
  IdSSLOpenSSL, IdSchedulerOfThreadDefault,IdContext;

Const
    Version : string =  '0.02';
    PortTemp = 8098;
    ServerName :string = 'localhost';


type

  { Tfrmmain }


  Tfrmmain = class(TForm)
    btDesconectar1: TButton;
    Button1: TButton;
    btConectar: TButton;
    btSetup: TButton;
    IdHTTPServer1: TIdHTTPServer;
    IdSchedulerOfThreadDefault1: TIdSchedulerOfThreadDefault;
    IdServerCompressionIntercept1: TIdServerCompressionIntercept;
    IdServerIOHandlerSSLOpenSSL1: TIdServerIOHandlerSSLOpenSSL;
    lbVersao: TLabel;
    lbstatus: TLabel;
    LazSerial1: TLazSerial;
    LTCPComponent1: TLTCPComponent;
    MenuItem1: TMenuItem;
    MenuItem2: TMenuItem;
    MenuItem3: TMenuItem;
    btlog: TMenuItem;
    popTray: TPopupMenu;
    PopupNotifier1: TPopupNotifier;
    Timer1: TTimer;
    btsair: TToggleBox;
    TrayIcon1: TTrayIcon;
    procedure btConectarClick(Sender: TObject);
    procedure btDesconectar1Click(Sender: TObject);
    procedure btlogClick(Sender: TObject);
    procedure btsairChange(Sender: TObject);
    procedure btSetupClick(Sender: TObject);
    procedure btTestaClick(Sender: TObject);
    procedure Button1Click(Sender: TObject);
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
  private
    Lbuffer: String;
    procedure ListDev();
    function PegaSerial() : String;
    procedure SalvarContexto();
    procedure Setup();
    procedure getPage(aSocket : TLSocket; PeerAddress : string; mensagem: string);
    procedure RespostaHTMLCabecalho(aSocket: TLSocket);
  public

  end;

var
  frmmain: Tfrmmain;

implementation

{$R *.lfm}

{ Tfrmmain }

procedure Tfrmmain.FormCreate(Sender: TObject);
begin
  Lbuffer:= '';
  frmlog := TfrmLog.create(self);
  frmsetup := Tfrmsetup.create(self);
  Fsetmain := TSetmain.create();
  self.left := Fsetmain.posx;
  self.top := fsetmain.posy;
  frmSetup.edSerialPort.text := FSETMAIN.COMPORT;
  frmRegistrar := TfrmRegistrar.Create(self);
  frmRegistrar.Identifica();
  frmpeso := TFrmpeso.create(self);
  frmpeso.show();
  ListDev();
  lbVersao.Caption:= Version;
end;

procedure Tfrmmain.FormDestroy(Sender: TObject);
begin
  SalvarContexto();
  Fsetmain.free();
  frmlog.free;
  frmRegistrar.free;
  frmSetup.free;
end;

procedure Tfrmmain.IdHTTPServer1CommandGet(AContext: TIdContext;
  ARequestInfo: TIdHTTPRequestInfo; AResponseInfo: TIdHTTPResponseInfo);
var
  buffer: ansistring;
begin
 //  buffer :='HTTP/1.1 200 OK '+#10;
 //buffer := buffer +'Content-Type: text/html'+#10;
 //buffer := buffer + '<!DOCTYPE HTML>'+#10;
 buffer :=  '<html>'+#10;
 buffer := buffer + '<head>'+#10;
 buffer := buffer + '<title>Meu SRV</title>'+#10;
 buffer := buffer + '</head>'+#10;
 buffer := buffer + '<body>'+#10;
 buffer := buffer + '{';
 buffer := buffer + '"rs":{';
 buffer := buffer + '"Temperatura":' ;
 buffer := buffer + '"'+frmpeso.lbTemperatura.Caption+'"';
 buffer := buffer + '}';
 buffer := buffer + '}'+#10;
 buffer := buffer + '</body>'+#10;
 buffer := buffer + '</html>'+#10;
 AResponseInfo.ContentText := buffer;
end;

procedure Tfrmmain.LazSerial1BlockSerialStatus(Sender: TObject;
  Reason: THookSerialReason; const Value: string);
begin
  if(LazSerial1.Active) then
  begin
    //Shape1.Color:= clRed;
    lbstatus.Caption:= 'Open';
  end
  else
  begin
    //Shape1.Color:= clwhite;
    lbstatus.Caption:= 'close';
  end;
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
    // Procura primeiro CR ou LF
    i := Pos(#13, S);
    if i = 0 then i := Pos(#10, S);
    if i = 0 then
      Exit(''); // ainda não há linha completa

    Result := Copy(S, 1, i - 1);
    // Remove CR/LF (cobre \r, \n, \r\n)
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
    // Remove espaços
    T := StringReplace(T, ' ', '', [rfReplaceAll]);
    // Troca vírgula decimal por ponto (para BR)
    // (Não mexe na vírgula separadora porque já separamos antes)
    T := StringReplace(T, ',', '.', [rfReplaceAll]);
    Result := T;
  end;

begin
  // Configura ponto como separador decimal para TryStrToFloat
  fs := DefaultFormatSettings;
  fs.DecimalSeparator := '.';

  // 1) Ler e acumular no buffer
  if LazSerial1.DataAvailable then
  begin
    info := LazSerial1.ReadData();
    if info <> '' then
      Lbuffer := Lbuffer + info;
  end;

  // 2) Processar todas as linhas completas que já estiverem no buffer
  while True do
  begin
    line := PopLineFromBuffer(Lbuffer);
    if line = '' then
      Break; // sem linha completa, aguarda próxima chegada

    line := Trim(line);
    if line = '' then
      Continue;

    // 3) Remove o prefixo "->" se houver
    line := TrimPrefixArrow(line);
    // Agora esperamos algo como: "28.00C, 53.00%" (ou "28,00C, 53,00%")

    // 4) Separa em duas partes pelo primeiro separador de vírgula
    p := Pos(',', line);
    if p > 0 then
    begin
      leftPart  := Trim(Copy(line, 1, p - 1));       // "28.00C"
      rightPart := Trim(Copy(line, p + 1, MaxInt));  // "53.00%"

      // 4.1) Extrai temperatura: até o 'C'
      pC := Pos('C', leftPart);
      if pC > 1 then
        tempStr := Trim(Copy(leftPart, 1, pC - 1))
      else
        tempStr := Trim(leftPart); // fallback se vier sem 'C'

      // 4.2) Extrai umidade: até o '%'
      pPct := Pos('%', rightPart);
      if pPct > 1 then
        humStr := Trim(Copy(rightPart, 1, pPct - 1))
      else
        humStr := Trim(rightPart); // fallback se vier sem '%'

      // 5) Normaliza números (vírgula -> ponto, remove espaços)
      tempStr := NormalizeNumber(tempStr);
      humStr  := NormalizeNumber(humStr);

      // 6) Converte e usa (se falhar conversão, ainda assim envia string limpa)
      if not TryStrToFloat(tempStr, tempVal, fs) then
        tempVal := NaN;
      if not TryStrToFloat(humStr, humVal, fs) then
        humVal := NaN;

      // 7) Entrega para a UI (ajuste para o que você já usa)
      //   Se seus métodos aceitarem string (como no seu exemplo original):
      if Assigned(frmpeso) then
      begin
        frmpeso.Temperatura(FormatFloat('0.00', tempVal)); // ou simplesmente tempStr
        frmpeso.Umidade(FormatFloat('0.00', humVal));      // crie este método caso não exista
      end;

      Application.ProcessMessages;
    end
    else
    begin
      // Linha sem vírgula — não está no formato esperado, ignore ou logue:
      // ShowMessage('Linha inválida: ' + line);
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
  //frmLog.Log('Connected:'+aSocket.PeerAddress);
end;

procedure Tfrmmain.LTCPComponent1Receive(aSocket: TLSocket);
var
  mensagem : string;
  strnro : string;
  posicao : integer;
begin
  //Mensagem recebida padrao Fila:nro+#13
  aSocket.GetMessage(mensagem);
  //PopupNotifier1.Text:=mensagem;
  //PopupNotifier1.Show;
  //frmlog.Log('Receive:'+aSocket.PeerAddress+',msg:'+mensagem);
  //if (mensagem <> '') then
  //if (pos(mensagem,'GET / HTTP/1.1')<>-1) then
  begin
     frmlog.Log('rec:'+mensagem);
     (*
      if (POS(mensagem, 'PESO:')>=0) then
      begin
        aSocket.SendMessage('PESO:'+ frmPeso.lbPeso.Caption +#13);  //Vou implementar aqui
        aSocket.Disconnect(true);
      end;
      *)
     getPage(aSocket, aSocket.PeerAddress, mensagem);
  end;
  //aSocket.Disconnect(true);
  LTCPComponent1.CallAction();
end;

procedure Tfrmmain.MenuItem1Click(Sender: TObject);
begin
  show();
end;

procedure Tfrmmain.MenuItem2Click(Sender: TObject);
begin
       Setup();
end;

procedure Tfrmmain.MenuItem3Click(Sender: TObject);
begin
  frmPeso.show();
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
  lbstatus.Caption:= 'Lendo...';
end;

procedure Tfrmmain.Timer1StopTimer(Sender: TObject);
begin
 lbstatus.Caption:= 'Não Lendo';
end;

procedure Tfrmmain.Timer1Timer(Sender: TObject);
begin
  //LazSerial1.WriteData(#05);
  Application.ProcessMessages();
end;

procedure Tfrmmain.Button1Click(Sender: TObject);
begin
   //PegaSerial();
end;

procedure Tfrmmain.FormCloseQuery(Sender: TObject; var CanClose: boolean);
begin
  //if QuestionDlg('Sair?','Deseja sair? ',);
  canClose := false;
  hide;
  if(not TrayIcon1.Visible) then
  begin
     TrayIcon1.Visible:=true;
  end;
end;

procedure Tfrmmain.btConectarClick(Sender: TObject);
begin

  try
    LazSerial1.close;
    LazSerial1.Device := FSETMAIN.COMPORT;
    LazSerial1.BaudRate:= TBaudRate(FSETMAIN.BAUDRATE);
    LazSerial1.DataBits:= TDataBits(FSETMAIN.DATABIT);
    //LazSerial1.FlowControl:= TFlowControl(FSETMAIN.;
    LazSerial1.Parity:= TParity(FSETMAIN.PARIDADE);
    LazSerial1.StopBits:= TStopBits(FSETMAIN.STOPBIT);

    LazSerial1.Open;
    Application.ProcessMessages();

  finally
    Timer1.Enabled:= not Timer1.Enabled;
    TrayIcon1.Visible:=true;
    TrayIcon1.Hint:='Connected';
    //IdHTTPServer1.DefaultPort (PortTemp);
    IdHTTPServer1.active := true;
    hide;
  end;


end;

procedure Tfrmmain.btDesconectar1Click(Sender: TObject);
begin
  //SdpoSerial1.close;
  Timer1.Enabled:= false;
  LazSerial1.close;
  Application.ProcessMessages();
end;

procedure Tfrmmain.btlogClick(Sender: TObject);
begin
  frmLog.show;
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
  posicao : integer;
begin


  ListOfFiles := TStringList.create();
  {$IFDEF LINUX}
  Directory := '/dev';
  FindAllFiles ( ListOfFiles , Directory ,  '*' ,  false ) ;
  posicao := 0;
  //ListOfFiles.Find('ttyS',posicao);
  //ListOfFiles.Sorted := true;
  cbserial.items.Clear;
  cbserial.Items.text:= ListOfFiles.Text;
  {$ENDIF}
end;

procedure Tfrmmain.SalvarContexto();
begin
  (*
  FSETMAIN.empresa := edEmpresa.text;
  FSETMAIN.Localizacao :=  edlocalizacao.text;
  FSETMAIN.Tipo1 :=  edTipo1.text;
  FSETMAIN.Tipo2 := edTipo2.text;
  FSETMAIN.Tipo3 := edTipo3.text;
  FSETMAIN.Contagem1 :=  strtoint(edCont1.text);
  FSETMAIN.Contagem2 := strtoint(edCont2.text);
  FSETMAIN.Contagem3 := strtoint( edCont3.text);
  *)
  FSETMAIN.posx := self.left;
  FSetMain.posy := self.top;
  (*
  FSetmain.painel:= edPainel.text;
  Fsetmain.tipoimp := cbTipoImp.ItemIndex;
  Fsetmain.modeloimp := cbModeImp.ItemIndex;
  *)
  //FSetmain.COMPORT := cbserial.text;
  (*
  Fsetmain.EXEC:= cbIniciar.Checked;
  *)
  FSETMAIN.SalvaContexto();

end;

procedure Tfrmmain.Setup();
begin
  frmSetup.edSerialPort.text := FSETMAIN.COMPORT;
  frmSetup.cbBaudrate.ItemIndex:= FSETMAIN.BAUDRATE;
  frmSetup.cbDatabits.ItemIndex:= FSETMAIN.DATABIT;
  frmSetup.rgParity.ItemIndex:= FSETMAIN.PARIDADE;
  //frmSetup.rgFlowControl.ItemIndex:=FSETMAIN.;
  frmSetup.rgStopbit.ItemIndex := FSETMAIN.STOPBIT;
  frmSetup.show();
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

 //aSocket.SendMessage('Connection: close'+#10+#13);
 //aSocket.Send(UTF8Char(buffer),length(buffer));

end;

procedure Tfrmmain.getPage(aSocket: TLSocket; PeerAddress: string;
  mensagem: string);
var
  buffer : WIDEstring;
begin

  RespostaHTMLCabecalho(aSocket);
  //aSocket.SendMessage('Host:'+ServerName+' '+#10+#13);
  //buffer := buffer + 'Refresh: 5';
  //buffer := buffer + #13#10;
  (*
  aSocket.SendMessage('<!DOCTYPE HTML>'+#10+#13);
  aSocket.SendMessage('<html>'+#10+#13);
  aSocket.SendMessage('<head>'+#10+#13);
  aSocket.SendMessage('</head>'+#10+#13);
  aSocket.SendMessage('<body>'+#10+#13);
  aSocket.SendMessage('hello '+#10+#13);
  aSocket.SendMessage('</body>'+#10+#13);
  aSocket.SendMessage('</html>'+#10+#13);
  //aSocket.Send(buffer,sizeof(buffer));
  frmLog.Log('ENV:'+buffer);
  //aSocket.SendMessage(buffer);
  //LTCPComponent1.CallAction();
  *)
end;

end.

