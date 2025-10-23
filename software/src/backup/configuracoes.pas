unit configuracoes;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, ExtCtrls, ComCtrls,
  StdCtrls;

type

  { TfrmConfiguracoes }

  TfrmConfiguracoes = class(TForm)
    edPorta: TEdit;
    Label1: TLabel;
    PageControl1: TPageControl;
    Panel1: TPanel;
    tsGeral: TTabSheet;
    tsSerial: TTabSheet;
  private

  public

  end;

var
  frmConfiguracoes: TfrmConfiguracoes;

implementation

{$R *.lfm}

end.

