unit peso;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls, LedNumber;

type

  { TfrmPeso }

  TfrmPeso = class(TForm)
    Label2: TLabel;
    Label3: TLabel;
    lbTemperatura: TLEDNumber;
    lbHumidade: TLEDNumber;
  private

  public
    procedure Temperatura(info: string);

  end;

var
  frmPeso: TfrmPeso;

implementation

{$R *.lfm}

{ TfrmPeso }

procedure TfrmPeso.Temperatura(info: string);
begin
  lbTemperatura.Caption:= info;
  Application.ProcessMessages;
end;

end.

