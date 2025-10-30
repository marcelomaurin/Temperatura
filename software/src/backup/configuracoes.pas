unit configuracoes;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, ExtCtrls, ComCtrls,
  StdCtrls, Buttons, setmain;

type

  { TfrmConfiguracoes }

  TfrmConfiguracoes = class(TForm)
    ckvarrendo: TCheckBox;
    edPorta: TEdit;
    Image1: TImage;
    Image2: TImage;
    Label1: TLabel;
    PageControl1: TPageControl;
    Panel1: TPanel;
    btSalvar: TSpeedButton;
    btCancelar: TSpeedButton;
    tsGeral: TTabSheet;
    tsSerial: TTabSheet;
    procedure btCancelarClick(Sender: TObject);
    procedure btSalvarClick(Sender: TObject);
    procedure FormCreate(Sender: TObject);
  private

  public
    procedure Salvar();
  end;

var
  frmConfiguracoes: TfrmConfiguracoes;

implementation

{$R *.lfm}

{ TfrmConfiguracoes }

procedure TfrmConfiguracoes.btCancelarClick(Sender: TObject);
begin
  Close;
end;

procedure TfrmConfiguracoes.btSalvarClick(Sender: TObject);
begin
  Salvar();
  Close;
end;

procedure TfrmConfiguracoes.FormCreate(Sender: TObject);
begin
  ckvarrendo.Checked := fsetmain.Varrendo;
  edPorta.Text := fsetmain.COMPORT;
end;

procedure TfrmConfiguracoes.Salvar();
begin
   fsetmain.Varrendo := ckvarrendo.Checked;
   fsetmain.COMPORT :=  edPorta.Text;
end;

end.

