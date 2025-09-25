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
    function NormalizeNumber(const S: string): string;
  public
    // Temperatura
    procedure Temperatura(const info: string); overload;
    procedure Temperatura(const value: Double); overload;

    // Umidade
    procedure Umidade(const info: string); overload;
    procedure Umidade(const value: Double); overload;
  end;

var
  frmPeso: TfrmPeso;

implementation

{$R *.lfm}

{ TfrmPeso }

function TfrmPeso.NormalizeNumber(const S: string): string;
begin
  // remove espaços e troca vírgula por ponto
  Result := StringReplace(Trim(S), ' ', '', [rfReplaceAll]);
  Result := StringReplace(Result, ',', '.', [rfReplaceAll]);
end;

procedure TfrmPeso.Temperatura(const info: string);
begin
  // recebe string já formatada (ex.: '28.00')
  lbTemperatura.Caption := info;
  Application.ProcessMessages;
end;

procedure TfrmPeso.Temperatura(const value: Double);
begin
  // formata com 2 casas
  lbTemperatura.Caption := FormatFloat('0.00', value);
  Application.ProcessMessages;
end;

procedure TfrmPeso.Umidade(const info: string);
begin
  // recebe string já formatada (ex.: '53.00')
  lbHumidade.Caption := info;
  Application.ProcessMessages;
end;

procedure TfrmPeso.Umidade(const value: Double);
begin
  // formata com 2 casas
  lbHumidade.Caption := FormatFloat('0.00', value);
  Application.ProcessMessages;
end;

end.

