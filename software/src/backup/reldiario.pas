unit reldiario;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, DB, Forms, Controls, Graphics, Dialogs, StdCtrls, ExtCtrls,
  DBGrids, rxtooledit, base,  DateUtils, Math, LCLIntf;

type

  { TfrmRelDiario }

  TfrmRelDiario = class(TForm)
    btPesquisar: TButton;
    btPesquisar1: TButton;
    dsRel: TDataSource;
    DBGrid1: TDBGrid;
    dtfim: TRxDateEdit;
    dtInicia: TRxDateEdit;
    Label1: TLabel;
    Label2: TLabel;
    Panel1: TPanel;
    Panel2: TPanel;
    procedure btPesquisar1Click(Sender: TObject);
    procedure btPesquisarClick(Sender: TObject);
    procedure FormShow(Sender: TObject);
  private

  public
    procedure GeraRel();
    function HTMLEscape(const S: string): string;
    procedure ExportaRelatorioHTMLAtual;
    function MontaRelatorioHTML(const Titulo: string; ADataSet: TDataSet;
  const DataIni, DataFim: TDateTime): string;

  end;

var
  frmRelDiario: TfrmRelDiario;

implementation

{$R *.lfm}

{ TfrmRelDiario }

procedure TfrmRelDiario.btPesquisarClick(Sender: TObject);
begin
  GeraRel();
end;

procedure TfrmRelDiario.btPesquisar1Click(Sender: TObject);
begin
  ExportaRelatorioHTMLAtual;

end;

procedure TfrmRelDiario.FormShow(Sender: TObject);
begin
   dtInicia.Date:= now;
   dtfim.Date:= now;
end;

procedure TfrmRelDiario.GeraRel();
var
  DataIni   : TDateTime;
  DataFim   : TDateTime;
begin
    DataIni := dtInicia.Date;
    DataFim := EndOfTheDay(dtfim.Date);

    if dmbase.GeraRelatorioDiario(DataIni, DataFim) then
    begin
      ShowMessage(Format('Encontradas %d medidas.',
        [dmbase.zqrymedidas.RecordCount ]));

    end;

end;


function TfrmRelDiario.MontaRelatorioHTML(const Titulo: string; ADataSet: TDataSet;
  const DataIni, DataFim: TDateTime): string;
var
  sl: TStringList;
  i: Integer;
  fn: string;
begin
  if (ADataSet = nil) or ADataSet.IsEmpty then
    raise Exception.Create('Não há dados para gerar o relatório.');

  fn := IncludeTrailingPathDelimiter(GetTempDir(False)) +
        Format('rel_diario_%s.html', [FormatDateTime('yyyymmddhhnnss', Now)]);
  sl := TStringList.Create;
  try
    sl.Add('<!doctype html><html lang="pt-br"><head>');
    sl.Add('<meta charset="utf-8">');
    sl.Add('<title>'+HTMLEscape(Titulo)+'</title>');
    sl.Add('<style>');
    sl.Add('body{font-family:Arial,Helvetica,sans-serif;margin:20px}');
    sl.Add('table{border-collapse:collapse;width:100%}');
    sl.Add('th,td{border:1px solid #ccc;padding:6px 8px}');
    sl.Add('th{background:#f2f2f2;text-align:left}');
    sl.Add('tr:nth-child(even){background:#fafafa}');
    sl.Add('.right{text-align:right}');
    sl.Add('</style></head><body>');

    sl.Add(Format('<h2>%s</h2>', [HTMLEscape(Titulo)]));
    sl.Add(Format('<p>Período: %s a %s</p>',
      [FormatDateTime('dd/mm/yyyy', DataIni), FormatDateTime('dd/mm/yyyy', DataFim)]));

    sl.Add('<table><thead><tr>');
    for i := 0 to ADataSet.FieldCount-1 do
      sl.Add('<th>' + HTMLEscape(ADataSet.Fields[i].DisplayLabel) + '</th>');
    sl.Add('</tr></thead><tbody>');

    ADataSet.DisableControls;
    try
      ADataSet.First;
      while not ADataSet.EOF do
      begin
        sl.Add('<tr>');
        for i := 0 to ADataSet.FieldCount-1 do
        begin
          if ADataSet.Fields[i].DataType in
             [ftFloat, ftCurrency, ftBCD, ftFMTBcd, ftSmallint, ftInteger, ftLargeint] then
            sl.Add('<td class="right">' + FieldAsHTML(ADataSet.Fields[i]) + '</td>')
          else
            sl.Add('<td>' + FieldAsHTML(ADataSet.Fields[i]) + '</td>');
        end;
        sl.Add('</tr>');
        ADataSet.Next;
      end;
    finally
      ADataSet.EnableControls;
    end;

    sl.Add('</tbody></table>');
    sl.Add(Format('<p>Registros: %d</p>', [ADataSet.RecordCount]));
    sl.Add('</body></html>');
    sl.SaveToFile(fn);
  finally
    sl.Free;
  end;
  Result := fn;
end;

function TfrmRelDiario.HTMLEscape(const S: string): string;
begin
  Result := StringReplace(S, '&','&amp;',[rfReplaceAll]);
  Result := StringReplace(Result, '<','&lt;',[rfReplaceAll]);
  Result := StringReplace(Result, '>','&gt;',[rfReplaceAll]);
end;

procedure TfrmRelDiario.ExportaRelatorioHTMLAtual;
var
  ds: TDataSet;
  fn: string;


  function FieldAsHTML(F: TField): string;
  begin
    if (F = nil) or F.IsNull then Exit('');
    case F.DataType of
      ftDate:      Result := FormatDateTime('dd/mm/yyyy', F.AsDateTime);
      ftTime:      Result := FormatDateTime('hh:nn:ss',  F.AsDateTime);
      ftDateTime,
      ftTimeStamp: Result := FormatDateTime('dd/mm/yyyy hh:nn:ss', F.AsDateTime);
      ftFloat, ftCurrency, ftBCD, ftFMTBcd:
        Result := StringReplace(FormatFloat('0.###', F.AsFloat), ',', '.', []);
    else
      Result := F.AsString;
    end;
    Result := HTMLEscape(Result);
  end;


begin
  ds := dsRel.DataSet;
  if (ds = nil) or ds.IsEmpty then
  begin
    ShowMessage('Sem dados em dsRel para exportar.');
    Exit;
  end;
  fn := MontaRelatorioHTML('Relatório Diário', ds, dtInicia.Date, dtfim.Date);
  OpenURL('file://' + fn);
end;

end.

