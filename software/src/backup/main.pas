unit main;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, ExtCtrls,
  PopupNotifier, ComCtrls, Menus, StdCtrls, AnchorDockPanel, UniqueInstance,
  uplaysound, untsalesSwitch, DataPortSerial, DataPortHTTP, AdvLed, NiceSideBar,
  base, setmain, caddevice,  fpjson, jsonparser; // <-- garantir estes na seção implementation uses;

type

  { Tfrmmain }

  Tfrmmain = class(TForm)
    AdvLed1: TAdvLed;
    btVarredura: TButton;
    btVarredura1: TButton;
    HeaderControl1: THeaderControl;
    ImageList1: TImageList;
    Label1: TLabel;
    MainMenu1: TMainMenu;
    MenuItem1: TMenuItem;
    MenuItem2: TMenuItem;
    MenuItem3: TMenuItem;
    MenuItem4: TMenuItem;
    MenuItem5: TMenuItem;
    MenuItem6: TMenuItem;
    MenuItem7: TMenuItem;
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
    tbStatus: TTabSheet;
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
    procedure MenuItem7Click(Sender: TObject);
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
    procedure chamarTipo2;
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
     //Medidas
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

procedure Tfrmmain.MenuItem7Click(Sender: TObject);
begin
  CadEquipamentos();
  AdicionaDevices;
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
  // (Opcional) Deixa o nó aberto
  if Assigned(FNodeDevices) then
    FNodeDevices.Expand(False);
end;

function Tfrmmain.AdicionaDevice(const ANome: string; APtr: Pointer): TTreeNode;
begin
  // Garante que o nó "Devices" exista
  if (FNodeDevices = nil) then
  begin
    FNodeDevices := tvItem.Items.Add(nil, 'Devices');
    FNodeDevices.ImageIndex:= 4;
  end;



  // Cria o nó filho com o nome do device
  Result := tvItem.Items.AddChild(FNodeDevices, ANome);

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
   chamarTipo2;
end;

procedure Tfrmmain.chamarTipo2;
var
  L: TStringList;
  i: Integer;
  J: TJSONData;
  tempVal, humVal: Double;
  o: TJSONObject;
begin
  if not dmBase.ListaPortasTipo2(L) then
    Exit;
  try
    for i := 0 to L.Count - 1 do
    begin
      // Cada item da lista é uma "porta" (no teu schema ela guarda o IP/host)
      if dmBase.ConsultaIPTempHum(L[i], J) then
      try
        tempVal := NaN;
        humVal  := NaN;

        if (J <> nil) and (J.JSONType = jtObject) then
        begin
          o := TJSONObject(J);
          if o.Find('temperature') <> nil then
            tempVal := o.Find('temperature').AsFloat;
          if o.Find('humidity') <> nil then
            humVal := o.Find('humidity').AsFloat;
        end;

        // Exemplo simples: atualiza a barra de status com o último IP/leituras
        if (not IsNan(tempVal)) or (not IsNan(humVal)) then
          StatusBar1.SimpleText :=
            Format('%s -> T=%.2f °C  H=%.2f %%', [L[i], tempVal, humVal])
        else
          StatusBar1.SimpleText := L[i] + ' -> JSON recebido (sem campos previstos)';

      finally
        J.Free;
      end
      else
      begin
        StatusBar1.SimpleText := L[i] + ' -> falha ao obter JSON';
      end;
    end;
  finally
    L.Free;
  end;
end;


end.

