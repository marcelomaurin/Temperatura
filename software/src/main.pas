unit main;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, ExtCtrls,
  PopupNotifier, ComCtrls, Menus, StdCtrls, DBCtrls, AnchorDockPanel,
  UniqueInstance, uplaysound, untsalesSwitch, DataPortSerial, DataPortHTTP,
  AdvLed, NiceSideBar, medidas, base, setmain, caddevice, fpjson, jsonparser,
  DB, hint, configuracoes; // <-- garantir estes na seção implementation uses;

Const
  Versao =  '0.5';

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
    MenuItem2: TMenuItem;
    MenuItem3: TMenuItem;
    MenuItem4: TMenuItem;
    MenuItem5: TMenuItem;
    MenuItem6: TMenuItem;
    MenuItem7: TMenuItem;
    MenuItem8: TMenuItem;
    PageControl1: TPageControl;
    Panel1: TPanel;
    Panel2: TPanel;
    Panel3: TPanel;
    Panel4: TPanel;
    Panel5: TPanel;
    playsound1: Tplaysound;
    PopupNotifier1: TPopupNotifier;
    Splitter1: TSplitter;
    StatusBar1: TStatusBar;
    tsLog: TTabSheet;
    tbStatus: TTabSheet;
    Timer1: TTimer;
    tmProcessa: TTimer;
    tsSobre: TTabSheet;
    tvItem: TTreeView;
    UniqueInstance1: TUniqueInstance;
    procedure btVarredura1Click(Sender: TObject);
    procedure btVarreduraClick(Sender: TObject);
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure HeaderControl1SectionClick(HeaderControl: TCustomHeaderControl;
      Section: THeaderSection);
    procedure MenuItem2Click(Sender: TObject);
    procedure MenuItem3Click(Sender: TObject);
    procedure MenuItem7Click(Sender: TObject);
    procedure MenuItem8Click(Sender: TObject);
    procedure Timer1Timer(Sender: TObject);
    procedure tmProcessaStartTimer(Sender: TObject);
    procedure tmProcessaStopTimer(Sender: TObject);
    procedure tmProcessaTimer(Sender: TObject);
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
  end;

var
  frmmain: Tfrmmain;

implementation

{$R *.lfm}

{ Tfrmmain }

procedure Tfrmmain.FormCreate(Sender: TObject);
begin
  // Carrega/gera configurações (srvtemp.cfg) via TSetMain
  FSETMAIN := TSetMain.Create;
  FSETMAIN.CarregaContexto;
  lbversao.Caption:= Versao;

  frmhint := tfrmhint.create(self);

  // DataModule da aplicação
  dmBase := TdmBase.Create(Self);

  Inicializar;
end;

procedure Tfrmmain.btVarreduraClick(Sender: TObject);
begin
  tmProcessa.Enabled:=true;
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
  end;
  if(Section.OriginalIndex=3) then
  begin
     //Configuraçoes
  end;
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
end;

procedure Tfrmmain.tmProcessaStopTimer(Sender: TObject);
begin
  AdvLed1.Blink:= false;
  AdvLed1.State:=lsOff;
end;

procedure Tfrmmain.tmProcessaTimer(Sender: TObject);
begin
  AdvLed1.State:=lsOn;
  AdvLed1.Blink:=false;
  VerreDevices();
  application.ProcessMessages;
end;

procedure Tfrmmain.Inicializar;
begin
  Arvore_Reset;
  AdicionaDevices; // já carrega os devices ao abrir
end;

procedure Tfrmmain.Arvore_Reset;
begin
  tvItem.Items.Clear;

  // Cria o nó raiz "Devices" e guarda a referência
  FNodeDevices := tvItem.Items.Add(nil, 'Devices');
  FNodeDevices.ImageIndex:=4;
  // (Opcional) Deixa o nó aberto
  if Assigned(FNodeDevices) then
    FNodeDevices.Expand(False);
end;

procedure Tfrmmain.RegistraLog(info: string);
begin
  meLog.Append(datetimetostr(now)+' '+info);
end;

function Tfrmmain.AdicionaDevice(const ANome: string; APtr: Pointer): TTreeNode;
var
  item : TTreeNode;
begin
  // Garante que o nó "Devices" exista
  if (FNodeDevices = nil) then
  begin
    FNodeDevices := tvItem.Items.Add(nil, 'Devices');
    FNodeDevices.ImageIndex:= 4;
  end;



  // Cria o nó filho com o nome do device
  item := tvItem.Items.AddChild(FNodeDevices, ANome);
  item.ImageIndex:=1;
  result := item;

  // Vincula o ponteiro ao node (recuperável depois via Node.Data)
  if Assigned(Result) then
    Result.Data := APtr;

  // (Opcional) Mantém "Devices" expandido ao adicionar
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
  // Garante nó pai "Devices"
  if (FNodeDevices = nil) then
  begin
    FNodeDevices := tvItem.Items.Add(nil, 'Devices');
    FNodeDevices.ImageIndex:=4;
    if Assigned(FNodeDevices) then
      FNodeDevices.Expand(False);
  end;

  // Limpa filhos atuais (opcional; remova se quiser manter existentes)
  if Assigned(FNodeDevices) then
    FNodeDevices.DeleteChildren;

  // Busca no banco via dmBase (usa zqryaux)
  if not dmBase.BuscaDevices('') then
    Exit;

  dmBase.zqryaux.First;
  while not dmBase.zqryaux.EOF do
  begin
    DevId   := dmBase.zqryaux.FieldByName('id_device').AsLargeInt;
    DevNome := dmBase.zqryaux.FieldByName('nome').AsString;

    // Cria nó como FILHO de FNodeDevices
    N := tvItem.Items.AddChild(FNodeDevices, DevNome);
    n.ImageIndex:= 1;

    // Armazena o ID no nó:
    // OBS: TTreeNode no Lazarus NÃO tem Tag; usamos Data (Pointer) com cast seguro.
    if Assigned(N) then
      N.Data := Pointer(PtrInt(DevId));

    dmBase.zqryaux.Next;
  end;

  // Mantém o grupo aberto
  if Assigned(FNodeDevices) then
    FNodeDevices.Expand(False);
end;

procedure Tfrmmain.VerreDevices();
begin
   chamarTipo1;
   //chamarTipo2;
end;

procedure Tfrmmain.chamarTipo1;
var
  L: TStringList;
  i: Integer;
  J: TJSONData;
  tempVal, humVal: Double;
  o: TJSONObject;
  DevId: Int64;
  porta : string;
begin
  L := TStringList.Create;
  if not dmBase.ListaPortasTipo1(L) then
  begin
    L.Free;
    Exit;
  end;
  if(L.Count >1) then
  begin
    ShowMessage('Só pode existir um device serial!');
    tmProcessa.Enabled:=false;
    L.free;
    Exit;
  end;

  try
    for i := 0 to L.Count - 1 do
    begin
      // id_device vem em Objects[i] como Pointer -> PtrInt
      if Assigned(L.Objects[i]) then
        DevId := PtrInt(L.Objects[i])
      else
        DevId := -1;

      dmbase.serialdevid:= DevID;

      porta := dmBase.GetIDPorta(DevID);
      if(not dmbase.LazSerial1.Active) then
      begin
        dmbase.LazSerial1.Device:= porta;
        dmbase.AtualizaConSerial(true);
      end;
    end;
  finally
    L.Free; // seguro, pois OwnsObjects=False
  end;
end;

procedure Tfrmmain.chamarTipo2;
var
  L: TStringList;
  i: Integer;
  J: TJSONData;
  tempVal, humVal: Double;
  o: TJSONObject;
  DevId: Int64;
begin
  L := TStringList.Create;
  if not dmBase.ListaPortasTipo2(L) then
  begin
    L.Free;
    Exit;
  end;

  try
    for i := 0 to L.Count - 1 do
    begin
      // id_device vem em Objects[i] como Pointer -> PtrInt
      if Assigned(L.Objects[i]) then
        DevId := PtrInt(L.Objects[i])
      else
        DevId := -1;

      // Cada item da lista é uma "porta"/IP
      if dmBase.ConsultaIPTempHum(L[i], J) then
      try
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

        // Exemplo simples: atualiza a barra de status com o último IP/leituras
        if ((tempVal <> 0) and (humVal <> 0)) then
          StatusBar1.SimpleText :=
            Format('%s -> T=%.2f °C  H=%.2f %%', [L[i], tempVal, humVal])
        else
          StatusBar1.SimpleText := L[i] + ' -> JSON recebido (sem campos previstos)';

        // Registrar em banco usando o DevId derivado do Pointer
        if DevId > 0 then
        begin
          // 0 = temperatura, 1 = humidade
          if (tempVal <> 0) then
            dmBase.RegistraMedida(DevId, 0, tempVal);
          if (humVal <> 0) then
            dmBase.RegistraMedida(DevId, 1, humVal);
        end;

      finally
        J.Free;
      end
      else
      begin
        StatusBar1.SimpleText := L[i] + ' -> falha ao obter JSON';
      end;
    end;
  finally
    L.Free; // seguro, pois OwnsObjects=False
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

