unit caddevice;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, ExtCtrls, ComCtrls,
  DBCtrls, DBGrids, StdCtrls, base, setmain, DB;

type

  { Tfrmcaddevice }

  Tfrmcaddevice = class(TForm)
    btSalvar: TButton;
    btCancelar: TButton;
    Button1: TButton;
    cbPesquisaTipo: TComboBox;
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
    procedure btSalvarClick(Sender: TObject);
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

procedure Tfrmcaddevice.btCadastrarChange(Sender: TObject);
begin
  tbEdit.Enabled := true;
  dmBase.editarDevice();
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

procedure Tfrmcaddevice.FormShow(Sender: TObject);
begin
   dmBase.DevicesOpenAll; // carrega na abertura
   dmBase.tbltipos.open;
end;

end.

