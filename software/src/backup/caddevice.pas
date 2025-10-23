unit caddevice;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, ExtCtrls, ComCtrls,
  DBCtrls, DBGrids, StdCtrls, base, setmain, DB, csvdataset;

type

  { Tfrmcaddevice }

  Tfrmcaddevice = class(TForm)
    btEditar: TButton;
    btSalvar: TButton;
    btCancelar: TButton;
    Button1: TButton;
    Button2: TButton;
    btNovo: TButton;
    cbPesquisaTipo: TComboBox;
    CSVDataset1: TCSVDataset;
    DBComboBox1: TDBLookupComboBox;
    dbnome: TDBEdit;
    dbnome1: TDBEdit;
    dscaddevices: TDataSource;
    dstipos: TDataSource;
    dsdevices: TDataSource;
    DBGrid1: TDBGrid;
    DBNavigator1: TDBNavigator;
    edPesqNome: TEdit;
    Label1: TLabel;
    Label2: TLabel;
    Label3: TLabel;
    Label4: TLabel;
    Label5: TLabel;
    PageControl1: TPageControl;
    Panel1: TPanel;
    Panel2: TPanel;
    Panel3: TPanel;
    Splitter1: TSplitter;
    tbPesquisa: TTabSheet;
    tbEdit: TTabSheet;
    btCadastrar: TToggleBox;
    procedure btCadastrarChange(Sender: TObject);
    procedure btCancelarClick(Sender: TObject);
    procedure btEditarClick(Sender: TObject);
    procedure btNovoClick(Sender: TObject);
    procedure btSalvarClick(Sender: TObject);
    procedure dstiposDataChange(Sender: TObject; Field: TField);
    procedure dstiposStateChange(Sender: TObject);
    procedure FormCreate(Sender: TObject);
    procedure FormShow(Sender: TObject);
  private

  public

  end;

var
  frmcaddevice: Tfrmcaddevice;

implementation

{$R *.lfm}

{ Tfrmcaddevice }

procedure Tfrmcaddevice.btCancelarClick(Sender: TObject);
begin

end;

procedure Tfrmcaddevice.btEditarClick(Sender: TObject);
begin
  tbEdit.Enabled:= true;
  PageControl1.ActivePage := tbEdit;
  dmBase.EditDevices();
end;

procedure Tfrmcaddevice.btNovoClick(Sender: TObject);
begin
   tbEdit.Enabled:= true;
   PageControl1.ActivePage := tbEdit;
   dmBase.InsertDevice();
end;

procedure Tfrmcaddevice.btCadastrarChange(Sender: TObject);
begin
  tbEdit.Enabled := true;
  dmBase.EditDevices();
  PageControl1.ActivePage := tbEdit;
end;

procedure Tfrmcaddevice.btSalvarClick(Sender: TObject);
var
  novoId: Int64;
begin
  // supondo: dbnome.Text = nome; dbnome1.Text = porta; DBComboBox1.ItemIndex = tipo
  novoId := dmBase.DeviceInsertNow();
  if novoId > 0 then
  begin
    ShowMessage('Device inserido. ID='+IntToStr(novoId));
    dmBase.DevicesOpenAll; // atualiza grid
  end
  else
  begin
    ShowMessage('Falha ao inserir device.');
  end;
  tbEdit.Enabled:= false;
  PageControl1.ActivePage := tbPesquisa;
end;

procedure Tfrmcaddevice.dstiposDataChange(Sender: TObject; Field: TField);
begin

end;

procedure Tfrmcaddevice.dstiposStateChange(Sender: TObject);
begin

end;

procedure Tfrmcaddevice.FormCreate(Sender: TObject);
begin
  dmBase.DevicesOpenAll; // carrega na abertura
  dmBase.tbltipos.open;
  dmbase.tbldevices.open;
end;


procedure Tfrmcaddevice.FormShow(Sender: TObject);
begin

end;

end.

