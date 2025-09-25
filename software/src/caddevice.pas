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
    DBComboBox1: TDBComboBox;
    dbnome: TDBEdit;
    dbnome1: TDBEdit;
    dscaddevices: TDataSource;
    dsdevices: TDataSource;
    DBGrid1: TDBGrid;
    DBNavigator1: TDBNavigator;
    Label1: TLabel;
    Label2: TLabel;
    Label3: TLabel;
    PageControl1: TPageControl;
    Panel1: TPanel;
    Panel2: TPanel;
    Panel3: TPanel;
    Splitter1: TSplitter;
    TabSheet1: TTabSheet;
    TabSheet2: TTabSheet;
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

procedure Tfrmcaddevice.btSalvarClick(Sender: TObject);
var
  novoId: Int64;
begin
  // supondo: dbnome.Text = nome; dbnome1.Text = porta; DBComboBox1.ItemIndex = tipo
  novoId := dmBase.DeviceInsertNow(dbnome.Text, dbnome1.Text, DBComboBox1.ItemIndex);
  if novoId > 0 then
  begin
    ShowMessage('Device inserido. ID='+IntToStr(novoId));
    dmBase.DevicesOpenAll; // atualiza grid
  end
  else
    ShowMessage('Falha ao inserir device.');
end;

procedure Tfrmcaddevice.FormShow(Sender: TObject);
begin
       dmBase.DevicesOpenAll; // carrega na abertura
end;

end.

