unit medidas;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, DB, Forms, Controls, Graphics, Dialogs, ExtCtrls, StdCtrls,
  DBGrids;

type

  { Tfrmmedidas }

  Tfrmmedidas = class(TForm)
    DBGrid1: TDBGrid;
    dsmedidas: TDataSource;
    ListBox1: TListBox;
    Panel1: TPanel;
    Panel2: TPanel;
  private

  public

  end;

var
  frmmedidas: Tfrmmedidas;

implementation

{$R *.lfm}

end.

