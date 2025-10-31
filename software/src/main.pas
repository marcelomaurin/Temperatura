unit main;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, ExtCtrls,
  PopupNotifier, ComCtrls, Menus, StdCtrls, DBCtrls, AnchorDockPanel,
  UniqueInstance, uplaysound, untsalesSwitch, DataPortSerial, DataPortHTTP,
  AdvLed, LedNumber, NiceSideBar, medidas, base, setmain, caddevice, fpjson,
  jsonparser, DB, hint, configuracoes, reldiario;

Const
  Versao =  '0.7';

type

  { Tfrmmain }

  Tfrmmain = class(TForm)
    AdvLed1: TAdvLed;
    btVarredura: TButton;
    btVarredura1: TButton;
    HeaderControl1: THeaderControl;
    ImageList1: TImageList;
    Label1: TLabel;
    Label2: TLabel;
    lbversao: TLabel;
    MainMenu1: TMainMenu;
    meLog: TMemo;
    MenuItem1: TMenuItem;
    mimostrar: TMenuItem;
    mnesconder: TMenuItem;
    MenuItem2: TMenuItem;
    MenuItem3: TMenuItem;
    MenuItem4: TMenuItem;
    MenuItem5: TMenuItem;
    MenuItem6: TMenuItem;
    MenuItem7: TMenuItem;
    MenuItem8: TMenuItem;
    MenuItem9: TMenuItem;
    PageControl1: TPageControl;
    Panel1: TPanel;
    Panel2: TPanel;
    Panel3: TPanel;
    Panel4: TPanel;
    Panel5: TPanel;
    playsound1: Tplaysound;
    popTray: TPopupMenu;
    Splitter1: TSplitter;
    StatusBar1: TStatusBar;
    TrayIcon1: TTrayIcon;
    tsLog: TTabSheet;
    tbStatus: TTabSheet;
    Timer1: TTimer;
    tmProcessa: TTimer;
    tsSobre: TTabSheet;
    tvItem: TTreeView;
    UniqueInstance1: TUniqueInstance;
    procedure btVarredura1Click(Sender: TObject);
    procedure btVarreduraClick(Sender: TObject);
    procedure FormCloseQuery(Sender: TObject; var CanClose: Boolean);
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure FormShow(Sender: TObject);
    procedure HeaderControl1SectionClick(HeaderControl: TCustomHeaderControl;
      Section: THeaderSection);
    procedure mimostrarClick(Sender: TObject);
    procedure MenuItem2Click(Sender: TObject);
    procedure MenuItem3Click(Sender: TObject);
    procedure MenuItem7Click(Sender: TObject);
    procedure MenuItem8Click(Sender: TObject);
    procedure MenuItem9Click(Sender: TObject);
    procedure mnesconderClick(Sender: TObject);
    procedure Timer1Timer(Sender: TObject);
    procedure tmProcessaStartTimer(Sender: TObject);
    procedure tmProcessaStopTimer(Sender: TObject);
    procedure tmProcessaTimer(Sender: TObject);
    procedure UniqueInstance1OtherInstance(Sender: TObject;
      ParamCount: Integer; const Parameters: array of String);
  private
    FNodeDevices: TTreeNode; // guarda o nó "Devices"

    procedure Inicializar;
    procedure Arvore_Reset;

  public
    // Adiciona um device sob o nó "Devices" e vincula um ponteiro ao Node.Data
    function AdicionaDevice(const ANome: string; APtr: Pointer): TTreeNode;
    procedure CadEquipamentos();
    procedure AdicionaDevices;
    procedure VerreDevices();
    procedure chamarTipo1;
    procedure chamarTipo2;
    procedure ChamaMedidas();
    procedure Configuracoes();
    procedure RegistraLog(info : string);
    procedure ChamaRelatorioDiario();
  end;

var
  frmmain: Tfrmmain;

implementation

{$R *.lfm}

{ Tfrmmain }

procedure Tfrmmain.FormCreate(Sender: TObject);
begin
  PageControl1.ActivePage := tsSobre;
  // Carrega/gera configurações (srvtemp.cfg) via TSetMain
  FSETMAIN := TSetMain.Create;
  FSETMAIN.CarregaContexto;
  lbversao.Caption:= Versao;

  frmhint := tfrmhint.create(self);

  // DataModule da aplicação
  dmBase := TdmBase.Create(Self);
  UniqueInstance1.Enabled:= true;
  Application.ProcessMessages;
  Sleep(2000);

  Inicializar;
end;

procedure Tfrmmain.btVarreduraClick(Sender: TObject);
begin
  tmProcessa.Enabled:=true;
end;

procedure Tfrmmain.FormCloseQuery(Sender: TObject; var CanClose: Boolean);
var
  resp: Integer;
begin
  resp := MessageDlg(
    'Deseja realmente fechar o programa?' + LineEnding +
    'Escolha "Não" para apenas minimizar.',
    mtConfirmation, [mbYes, mbNo, mbCancel], 0
  );

  case resp of
    mrYes:  // Fecha
      CanClose := True;

    mrNo:   // Minimiza
      begin
        CanClose := False;
        hide;
      end;

    mrCancel: // Cancela qualquer ação
      CanClose := False;
  end;
end;


procedure Tfrmmain.btVarredura1Click(Sender: TObject);
begin
  tmProcessa.Enabled:=false;
end;

procedure Tfrmmain.FormDestroy(Sender: TObject);
begin
  // Persiste e libera as configurações
  FreeAndNil(FSETMAIN);
  FreeAndNil(frmhint);
  // dmBase será liberado automaticamente por ter Owner = Self
end;

procedure Tfrmmain.FormShow(Sender: TObject);
begin

end;

procedure Tfrmmain.HeaderControl1SectionClick(
  HeaderControl: TCustomHeaderControl; Section: THeaderSection);
begin
  if (Section.OriginalIndex = 0) then
  begin
    // Cadastros
    CadEquipamentos();
    // Após fechar o cadastro, recarrega a árvore
    AdicionaDevices;
  end;
  if(Section.OriginalIndex=1) then
  begin
     ChamaMedidas();

  end;
  if(Section.OriginalIndex=2) then
  begin
     //Relatorios
     ChamaRelatorioDiario();
  end;
  if(Section.OriginalIndex=3) then
  begin
     Configuracoes();
  end;
end;

procedure Tfrmmain.mimostrarClick(Sender: TObject);
begin
  frmmain.show;
end;

procedure Tfrmmain.MenuItem2Click(Sender: TObject);
begin
  Close;
end;

procedure Tfrmmain.MenuItem3Click(Sender: TObject);
begin
  Configuracoes();
end;

procedure Tfrmmain.MenuItem7Click(Sender: TObject);
begin
  CadEquipamentos();
  AdicionaDevices;
end;

procedure Tfrmmain.MenuItem8Click(Sender: TObject);
begin
  ChamaMedidas();
end;

procedure Tfrmmain.MenuItem9Click(Sender: TObject);
begin
  ChamaRelatorioDiario();
end;

procedure Tfrmmain.mnesconderClick(Sender: TObject);
begin
  hide;
end;

procedure Tfrmmain.Timer1Timer(Sender: TObject);
var
  sClock, sConn, sHint: string;
begin
  sClock := FormatDateTime('dd/mm/yyyy hh:nn:ss', Now);
  if StatusBar1.Panels[0].Text <> sClock then
    StatusBar1.Panels[0].Text := sClock;

  if Assigned(dmbase) and Assigned(dmbase.ZConnection1) and dmbase.ZConnection1.Connected then
    sConn := 'Banco Conectado'
  else
    sConn := 'Banco Desconectado';

  if StatusBar1.Panels[1].Text <> sConn then
    StatusBar1.Panels[1].Text := sConn;

  sHint := Application.Hint;
  if StatusBar1.Panels[2].Text <> sHint then
    StatusBar1.Panels[2].Text := sHint;
end;

procedure Tfrmmain.tmProcessaStartTimer(Sender: TObject);
begin
  AdvLed1.Blink:= true;
  MessageHint('Servidor iniciou o monitoramento!');
  Application.ProcessMessages;

end;

procedure Tfrmmain.tmProcessaStopTimer(Sender: TObject);
begin
  AdvLed1.Blink:= false;
  AdvLed1.State:=lsOff;
  MessageHint('Servidor parou o monitoramento!');
end;

procedure Tfrmmain.tmProcessaTimer(Sender: TObject);
begin
  AdvLed1.State:=lsOn;
  AdvLed1.Blink:=false;
  VerreDevices();
  application.ProcessMessages;
end;

procedure Tfrmmain.UniqueInstance1OtherInstance(Sender: TObject;
  ParamCount: Integer; const Parameters: array of String);
begin
  // Se detectou outra instância rodando e esta não é a principal, encerra
  if not tmProcessa.Enabled then
  begin
    //ShowMessage('Outra instância já está em execução. Encerrando esta...');
    MessageHint('Outra instância já está em execução. Encerrando esta...');
    Application.ProcessMessages;
    Sleep(6000);
    Application.Terminate;
    Halt(0);
  end;
end;


procedure Tfrmmain.Inicializar;
begin
  Arvore_Reset;
  AdicionaDevices; // já carrega os devices ao abrir
  if(fsetmain.varrendo) then
  begin

     tmProcessa.Enabled:=true;
     MessageHint('Servidor inicializado automáticamente!');
  end;
end;

procedure Tfrmmain.Arvore_Reset;
begin
  tvItem.Items.Clear;

  // Cria o nó raiz "Devices" e guarda a referência
  FNodeDevices := tvItem.Items.Add(nil, 'Devices');
  FNodeDevices.ImageIndex:=4;
  if Assigned(FNodeDevices) then
    FNodeDevices.Expand(False);
end;

procedure Tfrmmain.RegistraLog(info: string);
begin
  meLog.Append(datetimetostr(now)+' '+info);
end;

procedure Tfrmmain.ChamaRelatorioDiario();
begin
  frmRelDiario := TfrmRelDiario.create(self);
  frmRelDiario.showmodal;
  FreeAndNil(frmRelDiario);
end;

function Tfrmmain.AdicionaDevice(const ANome: string; APtr: Pointer): TTreeNode;
var
  item : TTreeNode;
begin
  if (FNodeDevices = nil) then
  begin
    FNodeDevices := tvItem.Items.Add(nil, 'Devices');
    FNodeDevices.ImageIndex:= 4;
  end;

  item := tvItem.Items.AddChild(FNodeDevices, ANome);
  item.ImageIndex:=1;
  result := item;

  if Assigned(Result) then
    Result.Data := APtr;

  if Assigned(FNodeDevices) then
    FNodeDevices.Expand(False);
end;

procedure Tfrmmain.CadEquipamentos();
begin
  frmcaddevice := Tfrmcaddevice.create(self);
  frmcaddevice.ShowModal;
  frmcaddevice.free;
  frmcaddevice := nil;
end;

procedure Tfrmmain.AdicionaDevices;
var
  N: TTreeNode;
  DevId: Int64;
  DevNome: string;
begin
  if (FNodeDevices = nil) then
  begin
    FNodeDevices := tvItem.Items.Add(nil, 'Devices');
    FNodeDevices.ImageIndex:=4;
    if Assigned(FNodeDevices) then
      FNodeDevices.Expand(False);
  end;

  if Assigned(FNodeDevices) then
    FNodeDevices.DeleteChildren;

  if not dmBase.BuscaDevices('') then
    Exit;

  dmBase.zqryaux.First;
  while not dmBase.zqryaux.EOF do
  begin
    DevId   := dmBase.zqryaux.FieldByName('id_device').AsLargeInt;
    DevNome := dmBase.zqryaux.FieldByName('nome').AsString;

    N := tvItem.Items.AddChild(FNodeDevices, DevNome);
    n.ImageIndex:= 1;

    if Assigned(N) then
      N.Data := Pointer(PtrInt(DevId));

    dmBase.zqryaux.Next;
  end;

  if Assigned(FNodeDevices) then
    FNodeDevices.Expand(False);
end;

procedure Tfrmmain.VerreDevices();
begin
  chamarTipo1;
  chamarTipo2;
end;

procedure Tfrmmain.chamarTipo1;
var
  L: TStringList;
  DevId: Int64;
  porta: string;
begin
  L := TStringList.Create;
  try
    // Lista portas do tipo 1
    if not dmBase.ListaPortasTipo1(L) then
    begin
      RegistraLog('ListaPortasTipo1 falhou ou não retornou dados.');
      Exit;
    end;

    if L.Count = 0 then
    begin
      RegistraLog('Nenhum device serial do tipo 1 encontrado.');
      tmProcessa.Enabled := False;
      Exit;
    end;

    if L.Count > 1 then
    begin
      RegistraLog('Existe mais de um device serial. Permitido apenas 1.');
      tmProcessa.Enabled := False;
      Exit;
    end;

    porta := L[0];

    // OBS: lógica existente do seu código – busca id por "nome"
    DevId := dmBase.BuscaDeviceIdPorPorta(porta);
    dmBase.serialdevid := DevId;

    if porta = '' then
    begin
      RegistraLog('Porta serial não encontrada (porta vazia).');
      if DevId > 0 then dmBase.UpdateDeviceStatus(DevId, False); // INATIVO
      tmProcessa.Enabled := False;
      Exit;
    end;

    // Configura e conecta
    dmBase.LazSerial1.Device := porta;

    if not dmBase.LazSerial1.Active then
    begin
      RegistraLog('Tentando conectar na porta ' + porta);
      if not dmBase.AtualizaConSerial(True) then
      begin
        RegistraLog('Falha ao conectar (AtualizaConSerial retornou False) em ' + porta);
        if DevId > 0 then dmBase.UpdateDeviceStatus(DevId, False); // INATIVO
        tmProcessa.Enabled := False;
        Exit;
      end;

      if not dmBase.LazSerial1.Active then
      begin
        RegistraLog('Falha ao conectar na porta ' + porta);
        MessageHint('Falha ao conectar na porta ' + porta);
        if DevId > 0 then dmBase.UpdateDeviceStatus(DevId, False); // INATIVO
        tmProcessa.Enabled := False;
        Exit;
      end;

      RegistraLog('Conectado na porta ' + porta);
      if DevId > 0 then dmBase.UpdateDeviceStatus(DevId, True); // ATIVO
    end
    else
    begin
      // já estava ativo
      if DevId > 0 then dmBase.UpdateDeviceStatus(DevId, True);
    end;

  finally
    L.Free;
  end;
end;

procedure Tfrmmain.chamarTipo2;
var
  L: TStringList;
  i, tentativas: Integer;
  J: TJSONData;
  tempVal, humVal: Double;
  o: TJSONObject;
  DevId: Int64;
  ok: Boolean;
  endpoint: string;
begin
  L := TStringList.Create;
  try
    // Lista os dispositivos do tipo 2 (HTTP/IP)
    if not dmBase.ListaPortasTipo2(L) then
    begin
      RegistraLog('ListaPortasTipo2 falhou ou não retornou dados.');
      Exit;
    end;

    if L.Count = 0 then
    begin
      RegistraLog('Nenhum device do tipo 2 encontrado.');
      Exit;
    end;

    for i := 0 to L.Count - 1 do
    begin
      // Em L[i] esperamos o endpoint/host do device.
      endpoint := L[i];

      // id_device vem em Objects[i] como Pointer -> PtrInt
      if Assigned(L.Objects[i]) then
        DevId := PtrInt(L.Objects[i])
      else
        DevId := -1;

      ok := False;
      tentativas := 0;
      J := nil;

      // Até 3 tentativas para consultar o endpoint do device
      repeat
        Inc(tentativas);
        RegistraLog(Format('Tipo2 [%s] tentativa %d/3...', [endpoint, tentativas]));

        // Consulta o JSON de temperatura/umidade
        if dmBase.ConsultaIPTempHum(endpoint, J) then
        begin
          ok := True;
          Break;
        end
        else
        begin
          // Falha nesta tentativa
          ok := False;
          Sleep(500); // pequeno intervalo entre tentativas
        end;
      until (tentativas >= 3);

      try
        if ok then
        begin
          // Conseguiu falar com o endpoint -> ATIVO
          if DevId > 0 then
            dmBase.UpdateDeviceStatus(DevId, True);

          tempVal := 0;
          humVal  := 0;

          if (J <> nil) and (J.JSONType = jtObject) then
          begin
            o := TJSONObject(J);
            if o.Find('temperature') <> nil then
              tempVal := o.Find('temperature').AsFloat;
            if o.Find('humidity') <> nil then
              humVal := o.Find('humidity').AsFloat;
          end;

          if (tempVal <> 0) or (humVal <> 0) then
            RegistraLog(Format('Tipo2 [%s] leitura -> T=%.2f °C  H=%.2f %%', [endpoint, tempVal, humVal]))
          else
            RegistraLog(Format('Tipo2 [%s] JSON recebido sem campos previstos (temperature/humidity).', [endpoint]));

          // Grava medidas se houver id do device
          if DevId > 0 then
          begin
            if (tempVal <> 0) then
              dmBase.RegistraMedida(DevId, 0, tempVal); // 0 = temperatura
            if (humVal <> 0) then
              dmBase.RegistraMedida(DevId, 1, humVal); // 1 = umidade
          end;
        end
        else
        begin
          // Falhou 3 vezes -> INATIVO
          RegistraLog(Format('Tipo2 [%s] falhou em 3 tentativas. Marcando INATIVO.', [endpoint]));
          MessageHint(Format('%s -> falha ao obter JSON após 3 tentativas', [endpoint]));
          if DevId > 0 then
            dmBase.UpdateDeviceStatus(DevId, False); // INATIVO
        end;
      finally
        J.Free;
      end;
    end;
  finally
    L.Free;
  end;
end;



procedure Tfrmmain.ChamaMedidas();
begin
  if (frmmedidas = nil) then
  begin
    frmmedidas := Tfrmmedidas.create(self);
  end;
  frmmedidas.show();
end;

procedure Tfrmmain.Configuracoes();
begin
  frmConfiguracoes := TfrmConfiguracoes.Create(self);
  frmConfiguracoes.showmodal;
  frmConfiguracoes.free;
  frmConfiguracoes := nil;
end;

end.

