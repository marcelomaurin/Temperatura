unit main;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, ExtCtrls,
  PopupNotifier, ComCtrls, Menus, AnchorDockPanel, UniqueInstance, uplaysound,
  untsalesSwitch, DataPortSerial, DataPortHTTP, NiceSideBar, base, setmain,
  caddevice;

type

  { Tfrmmain }

  Tfrmmain = class(TForm)
    HeaderControl1: THeaderControl;
    ImageList1: TImageList;
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
    tsSobre: TTabSheet;
    tvItem: TTreeView;
    UniqueInstance1: TUniqueInstance;
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure MenuItem2Click(Sender: TObject);
    procedure MenuItem7Click(Sender: TObject);
  private
    FNodeDevices: TTreeNode; // guarda o nó "Devices"

    procedure Inicializar;
    procedure Arvore_Reset;

  public
    // Adiciona um device sob o nó "Devices" e vincula um ponteiro ao Node.Data
    function AdicionaDevice(const ANome: string; APtr: Pointer): TTreeNode;
    procedure CadEquipamentos();
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

procedure Tfrmmain.FormDestroy(Sender: TObject);
begin
    // Persiste e libera as configurações
  FreeAndNil(FSETMAIN);
  // dmBase será liberado automaticamente por ter Owner = Self
end;

procedure Tfrmmain.MenuItem2Click(Sender: TObject);
begin
  Close;
end;

procedure Tfrmmain.MenuItem7Click(Sender: TObject);
begin
  CadEquipamentos();
end;

procedure Tfrmmain.Inicializar;
begin
  Arvore_Reset;
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

end.

