unit medidas;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, DB, csvdataset, Forms, Controls, Graphics, Dialogs,
  ExtCtrls, StdCtrls, DBGrids, DBCtrls, ComCtrls, TAGraph, rxtooledit,
  NiceChart, base, DateUtils, setmain;

type

  { Tfrmmedidas }

  Tfrmmedidas = class(TForm)
    btFechar: TButton;
    Button1: TButton;
    btExportar: TButton;
    CSVDataset1: TCSVDataset;
    DBGrid1: TDBGrid;
    dsdevices: TDataSource;
    lbdevices: TDBLookupListBox;
    dsmedidas: TDataSource;
    Label1: TLabel;
    Label2: TLabel;
    NiceChart1: TNiceChart;
    NiceChart2: TNiceChart;
    PageControl1: TPageControl;
    Panel1: TPanel;
    Panel2: TPanel;
    dtInicia: TRxDateEdit;
    dtfim: TRxDateEdit;
    Panel3: TPanel;
    TabSheet1: TTabSheet;
    TabSheet2: TTabSheet;
    TabSheet3: TTabSheet;
    procedure btExportarClick(Sender: TObject);
    procedure btFecharClick(Sender: TObject);
    procedure Button1Click(Sender: TObject);
    procedure FormCreate(Sender: TObject);
  private

  public
     FAxisXIsDateTime: Boolean;
     FAxisXDateFormat: string;
     procedure GeraGrafico();
     procedure GeraGraficoHumidade();
     property AxisXIsDateTime: Boolean read FAxisXIsDateTime write FAxisXIsDateTime default False;
     property AxisXDateFormat: string read FAxisXDateFormat write FAxisXDateFormat;

  end;

var
  frmmedidas: Tfrmmedidas;

implementation

{$R *.lfm}

{ Tfrmmedidas }

procedure Tfrmmedidas.btFecharClick(Sender: TObject);
begin
  close;
  frmmedidas.free;
  frmmedidas := nil;
end;

procedure Tfrmmedidas.btExportarClick(Sender: TObject);
var
  ds: TDataSet;
  dlg: TSaveDialog;
  sl: TStringList;
  bmk: TBookmark;
  i: Integer;
  line, cell, defaultName, nomeDevice: string;
  fmt: TFormatSettings;
  fld: TField;
begin
  ds := dsmedidas.DataSet;
  if (ds = nil) or ds.IsEmpty then
  begin
    ShowMessage('Não há dados para exportar.');
    Exit;
  end;

  if (lbdevices.ItemIndex <> -1) then
    nomeDevice := Trim(lbdevices.GetSelectedText)
  else
    nomeDevice := 'medidas';

  defaultName := Format('%s_%s.csv',
    [StringReplace(nomeDevice, ' ', '_', [rfReplaceAll]),
     FormatDateTime('yyyymmdd_hhnnss', Now)]);

  dlg := TSaveDialog.Create(Self);
  try
    dlg.Title := 'Exportar medidas';
    dlg.Filter := 'Arquivo CSV (*.csv)|*.csv|Texto (*.txt)|*.txt|Todos (*.*)|*.*';
    dlg.DefaultExt := 'csv';
    dlg.FileName := defaultName;

    if not dlg.Execute then Exit;

    sl := TStringList.Create;
    try
      // Cabeçalho
      line := '';
      for i := 0 to ds.FieldCount - 1 do
      begin
        if i > 0 then line := line + ';';
        line := line + '"' + ds.Fields[i].FieldName + '"';
      end;
      sl.Add(line);

      // Formatação BR (vírgula decimal)
      fmt := DefaultFormatSettings;
      fmt.DecimalSeparator := ',';

      // Dados
      bmk := ds.GetBookmark;
      try
        ds.DisableControls;
        ds.First;
        while not ds.EOF do
        begin
          line := '';
          for i := 0 to ds.FieldCount - 1 do
          begin
            fld := ds.Fields[i];

            if not fld.IsNull then
            begin
              case fld.DataType of
                ftDate:
                  cell := FormatDateTime('yyyy-mm-dd', fld.AsDateTime);
                ftTime:
                  cell := FormatDateTime('hh:nn:ss', fld.AsDateTime);
                ftDateTime, ftTimeStamp:
                  cell := FormatDateTime('yyyy-mm-dd hh:nn:ss', fld.AsDateTime);
                // <- sem ftSingle e sem ftTimeStampTZ
                ftFloat,  ftCurrency, ftBCD, ftFMTBcd:
                  cell := FloatToStr(fld.AsFloat, fmt);
              else
                cell := fld.AsString; // inteiro, string, etc.
              end;
            end
            else
              cell := '';

            // Escapa e envolve em aspas
            cell := StringReplace(cell, '"', '""', [rfReplaceAll]);
            cell := '"' + cell + '"';

            if i > 0 then line := line + ';';
            line := line + cell;
          end;

          sl.Add(line);
          ds.Next;
        end;
      finally
        if ds.BookmarkValid(bmk) then ds.GotoBookmark(bmk);
        ds.FreeBookmark(bmk);
        ds.EnableControls;
      end;

      sl.SaveToFile(dlg.FileName);
      ShowMessage('Exportado para: ' + dlg.FileName);
    finally
      sl.Free;
    end;
  finally
    dlg.Free;
  end;
end;




procedure Tfrmmedidas.Button1Click(Sender: TObject);
var
  AIdDevice : Int64;
  Nome      : string;
  DataIni   : TDateTime;
  DataFim   : TDateTime;
begin
  if lbdevices.KeyValue <> Null then
  begin
    // Pega direto o valor da chave e o texto visível

    Nome      := lbdevices.GetSelectedText;

    AIdDevice := dmbase.BuscaDeviceIdPorNome(nome);

    DataIni := dtInicia.Date;
    DataFim := EndOfTheDay(dtfim.Date);

    if dmbase.BuscaMedidas(AIdDevice, DataIni, DataFim) then
    begin
      ShowMessage(Format('Encontradas %d medidas para "%s".',
        [dmbase.zqrymedidas.RecordCount, Nome]));
      GeraGrafico();
      GeraGraficoHumidade();
    end
    else
      ShowMessage('Nenhuma medida encontrada para este período.');
  end
  else
    ShowMessage('Selecione pelo menos um device.');
end;


procedure Tfrmmedidas.FormCreate(Sender: TObject);
begin
  dtInicia.Date:= now;
  dtfim.date := now;
  FAxisXIsDateTime := False;
  FAxisXDateFormat := 'dd/mm/yy hh:nn';
  if(not dmbase.tbldevices.Active) then
  begin
       dmbase.tbldevices.open;
  end;
end;



procedure Tfrmmedidas.GeraGrafico();
var
  SerieTemp : TNiceSeries;
  ds        : TDataSet;
  bmk       : TBookmark;
  dt        : TDateTime;
  dtBase    : TDateTime;
  val       : Double;
  tipo      : Integer;
  XHora     : Double;
  temBase   : Boolean;
begin
  ds := dmbase.zqrymedidas;
  if (ds = nil) or ds.IsEmpty then
  begin
    ShowMessage('Não há medidas carregadas para gerar o gráfico.');
    Exit;
  end;

  // Acha a primeira data (base de cálculo)
  temBase := False;
  bmk := ds.GetBookmark;
  try
    ds.DisableControls;
    ds.First;
    while not ds.EOF do
    begin
      tipo := ds.FieldByName('tipomedida').AsInteger;
      if tipo =0 then
      begin
        dtBase := ds.FieldByName('dthrcad').AsDateTime;
        temBase := True;
        Break;
      end;
      ds.Next;
    end;
  finally
    if ds.BookmarkValid(bmk) then ds.GotoBookmark(bmk);
    ds.FreeBookmark(bmk);
    ds.EnableControls;
  end;

  if not temBase then
  begin
    ShowMessage('Nenhuma medida de temperatura encontrada (tipomedida=1).');
    Exit;
  end;

  NiceChart1.BeginUpdate;
  try
    NiceChart1.Clear;

    NiceChart1.Title := 'Temperatura x Tempo';
    NiceChart1.AxisXTitle := 'Tempo (dd/mm/yy hh:mm)';
    NiceChart1.AxisYTitle := 'Temperatura (°C)';
    NiceChart1.AxisXOnePerValue := True; // cada ponto será uma posição sequencial
    NiceChart1.ShowLegend := True;

    SerieTemp := NiceChart1.AddSeries(skLine);
    SerieTemp.Caption := 'Temperatura (°C)';
    SerieTemp.LineWidth := 2;

    // Adiciona os pontos
    bmk := ds.GetBookmark;
    try
      ds.DisableControls;
      ds.First;
      while not ds.EOF do
      begin
        tipo := ds.FieldByName('tipomedida').AsInteger;
        if tipo = 0 then
        begin
          dt := ds.FieldByName('dthrcad').AsDateTime;
          val := ds.FieldByName('valor').AsFloat;

          // Calcula a hora relativa (1, 2, 3...)
          XHora := (dt - dtBase) * 24.0; // diferença em horas
          SerieTemp.AddXY(XHora + 1, val, FormatDateTime('dd/mm/yy hh:nn', dt));
        end;
        ds.Next;
      end;
    finally
      if ds.BookmarkValid(bmk) then ds.GotoBookmark(bmk);
      ds.FreeBookmark(bmk);
      ds.EnableControls;
    end;

  finally
    NiceChart1.EndUpdate;
  end;
end;


procedure Tfrmmedidas.GeraGraficoHumidade();
var
  Serie   : TNiceSeries;
  ds      : TDataSet;
  bmk     : TBookmark;
  dt      : TDateTime;
  dtBase  : TDateTime;
  val     : Double;
  tipo    : Integer;
  xHourIx : Integer;  // 1, 2, 3... (cada unidade = 1h)
  temBase : Boolean;
begin
  ds := dmbase.zqrymedidas;
  if (ds = nil) or ds.IsEmpty then
  begin
    ShowMessage('Não há medidas carregadas para gerar o gráfico.');
    Exit;
  end;

  // 1) Base: primeira leitura com tipomedida = 1 (umidade), truncada para a hora
  temBase := False;
  bmk := ds.GetBookmark;
  try
    ds.DisableControls;
    ds.First;
    while not ds.EOF do
    begin
      tipo := ds.FieldByName('tipomedida').AsInteger;
      if tipo = 1 then
      begin
        dtBase := Int(ds.FieldByName('dthrcad').AsDateTime) +
                  (HourOf(ds.FieldByName('dthrcad').AsDateTime) / 24.0);
        temBase := True;
        Break;
      end;
      ds.Next;
    end;
  finally
    if ds.BookmarkValid(bmk) then ds.GotoBookmark(bmk);
    ds.FreeBookmark(bmk);
    ds.EnableControls;
  end;

  if not temBase then
  begin
    ShowMessage('Nenhuma medida encontrada com tipomedida = 1 (umidade).');
    Exit;
  end;

  // 2) Monta o gráfico (substitui o conteúdo do NiceChart1)
  NiceChart2.BeginUpdate;
  try
    NiceChart2.Clear;

    NiceChart2.Title            := 'Umidade x Tempo (tipomedida=1)';
    NiceChart2.AxisXTitle       := 'Horas desde o início (1, 2, 3, …)';
    NiceChart2.AxisYTitle       := 'Umidade (%)';
    NiceChart2.ShowLegend       := True;
    NiceChart2.ShowXGrid        := True;
    NiceChart2.ShowYGrid        := True;
    NiceChart2.AxisXOnePerValue := True;   // eixo categórico por ponto

    Serie := NiceChart2.AddSeries(skLine);
    Serie.Caption   := 'Umidade (%)';
    Serie.LineWidth := 2;

    // 3) Alimenta apenas tipomedida = 1
    bmk := ds.GetBookmark;
    try
      ds.DisableControls;
      ds.First;
      while not ds.EOF do
      begin
        tipo := ds.FieldByName('tipomedida').AsInteger;
        if tipo = 1 then
        begin
          dt  := ds.FieldByName('dthrcad').AsDateTime;
          val := ds.FieldByName('valor').AsFloat;

          // Índice de hora: 1 para a hora-base, 2 para a próxima, etc.
          xHourIx := Trunc(((dt - dtBase) * 24.0)) + 1;

          // Hint com a data/hora real
          Serie.AddXY(xHourIx, val, FormatDateTime('dd/mm/yy hh:nn', dt));
        end;
        ds.Next;
      end;
    finally
      if ds.BookmarkValid(bmk) then ds.GotoBookmark(bmk);
      ds.FreeBookmark(bmk);
      ds.EnableControls;
    end;

  finally
    NiceChart2.EndUpdate;
  end;
end;





end.

