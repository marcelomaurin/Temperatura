unit caddevice;

{$mode ObjFPC}{$H+}

interface

uses
  Classes, SysUtils, Variants, Forms, Controls, Graphics, Dialogs, ExtCtrls, ComCtrls,
  DBCtrls, DBGrids, StdCtrls, rxlookup, rxdbcomb, base, setmain, DB, csvdataset;

type
  { Tfrmcaddevice }
  Tfrmcaddevice = class(TForm)
    btEditar: TButton;
    btSalvar: TButton;
    btCancelar: TButton;
    Button1: TButton;
    Button2: TButton;
    btNovo: TButton;
    btAtivar: TButton;
    cbPesquisaTipo: TComboBox;
    CSVDataset1: TCSVDataset;
    dscaddevices: TDataSource;
    dstipos: TDataSource;
    dsdevices: TDataSource;
    DBGrid1: TDBGrid;
    DBNavigator1: TDBNavigator;
    edNome: TEdit;
    edPorta: TEdit;
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
    lkTipos: TRxLookupEdit;
    Splitter1: TSplitter;
    tbPesquisa: TTabSheet;
    tbEdit: TTabSheet;
    btCadastrar: TToggleBox;
    procedure btAtivarClick(Sender: TObject);
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
    procedure AtivarEdicaoUI(const AAtivo: Boolean);
    procedure LimparEdits;
    procedure CarregarEditsDoDataset;  // <- joga para edNome, lkTipos e edPorta
    function  GetTipoSelecionadoId: Integer;
    function  ValidarCampos(out Msg: string): Boolean;
  public
  end;

var
  frmcaddevice: Tfrmcaddevice;

implementation

{$R *.lfm}

procedure Tfrmcaddevice.AtivarEdicaoUI(const AAtivo: Boolean);
begin
  tbEdit.Enabled := AAtivo;
  if AAtivo then PageControl1.ActivePage := tbEdit
            else PageControl1.ActivePage := tbPesquisa;
end;

procedure Tfrmcaddevice.LimparEdits;
begin
  edNome.Clear;
  edPorta.Clear;
  lkTipos.Text := '';
end;

procedure Tfrmcaddevice.CarregarEditsDoDataset;
var
  temTipo: Boolean;
  tipoId : Integer;
begin
  if (dmBase.tbldevices.Active) and (not dmBase.tbldevices.IsEmpty) then
  begin
    edNome.Text  := dmBase.tbldevices.FieldByName('nome').AsString;
    edPorta.Text := dmBase.tbldevices.FieldByName('porta').AsString;

    temTipo := dmBase.tbldevices.FindField('tipo') <> nil;
    if temTipo and (dstipos.DataSet <> nil) and dstipos.DataSet.Active then
    begin
      tipoId := dmBase.tbldevices.FieldByName('tipo').AsInteger;
      if (tipoId > 0) and dstipos.DataSet.Locate('id_tipo', tipoId, []) then
        lkTipos.Text := dstipos.DataSet.FieldByName('descricao').AsString
      else
        lkTipos.Text := '';
    end
    else
      lkTipos.Text := ''; // se não existir o campo 'tipo' na tabela devices
  end
  else
    LimparEdits;
end;

function Tfrmcaddevice.GetTipoSelecionadoId: Integer;
var
  v: Variant;
begin
  Result := 0;
  v := lkTipos.Text;
  if not VarIsEmpty(v) and not VarIsNull(v) then
    Result := StrToIntDef(VarToStr(v), 0);
  if (Result = 0) and (lkTipos.Text <> '') and (dstipos.DataSet <> nil) and dstipos.DataSet.Active then
    if dstipos.DataSet.Locate('descricao', lkTipos.Text, []) then
      Result := dstipos.DataSet.FieldByName('id_tipo').AsInteger;
end;

function Tfrmcaddevice.ValidarCampos(out Msg: string): Boolean;
begin
  Msg := '';
  if Trim(edNome.Text) = '' then Msg := 'Informe o nome.';
  Result := (Msg = '');
end;

{ === Botões === }

procedure Tfrmcaddevice.btCancelarClick(Sender: TObject);
begin
  if dmBase.tbldevices.Active and (dmBase.tbldevices.State in [dsInsert, dsEdit]) then
    dmBase.tbldevices.Cancel;
  AtivarEdicaoUI(False);
  btCadastrar.Checked := False;
end;

procedure Tfrmcaddevice.btEditarClick(Sender: TObject);
begin
  if (dsdevices.DataSet = nil) or dsdevices.DataSet.IsEmpty then
  begin
    ShowMessage('Selecione um registro para editar.');
    Exit;
  end;

  dmBase.EditDevices;          // coloca dataset em edição
  CarregarEditsDoDataset;      // joga valores atuais para edNome/lkTipos/edPorta
  AtivarEdicaoUI(True);
  btCadastrar.Checked := False;
end;

procedure Tfrmcaddevice.btNovoClick(Sender: TObject);
begin
  dmBase.InsertDevice;         // inicia inserção no dataset
  LimparEdits;                 // limpa campos visuais
  AtivarEdicaoUI(True);
  btCadastrar.Checked := True; // modo cadastro
end;

procedure Tfrmcaddevice.btCadastrarChange(Sender: TObject);
begin
  if btCadastrar.Checked then
  begin
    dmBase.InsertDevice;
    LimparEdits;
    AtivarEdicaoUI(True);
  end
  else
  begin
    if dmBase.tbldevices.Active and (dmBase.tbldevices.State = dsInsert) then
      dmBase.tbldevices.Cancel;
    AtivarEdicaoUI(False);
  end;
end;

procedure Tfrmcaddevice.btAtivarClick(Sender: TObject);
var
  ds   : TDataSet;
  idDev: Int64;
  ok   : Boolean;
begin
  ds := dsdevices.DataSet;

  if (ds = nil) or ds.IsEmpty then
  begin
    ShowMessage('Selecione um dispositivo na lista.');
    Exit;
  end;

  // Garante que o registro atual está salvo antes de atualizar
  if ds.State in [dsEdit, dsInsert] then
    ds.Post;

  // Tenta identificar o ID do device
  if ds.FindField('id_device') <> nil then
    idDev := ds.FieldByName('id_device').AsLargeInt
  else if ds.FindField('id') <> nil then
    idDev := ds.FieldByName('id').AsLargeInt
  else
  begin
    ShowMessage('Campo de identificação do dispositivo não encontrado (id_device/id).');
    Exit;
  end;

  if idDev <= 0 then
  begin
    ShowMessage('ID de dispositivo inválido.');
    Exit;
  end;

  // Ativa o dispositivo via UPDATE direto no banco
  ok := dmBase.AtivarDevice(idDev);
  if ok then
  begin
    dmBase.DevicesOpenAll;
    ShowMessage('Dispositivo ativado com sucesso (status = 1).');
  end
  else
    ShowMessage('Falha ao ativar o dispositivo.');
end;



procedure Tfrmcaddevice.btSalvarClick(Sender: TObject);
var
  msg: string;
  tipoId: Integer;
  novoId: Int64;
  idSel : Int64;
  ok    : Boolean;
begin
  if not ValidarCampos(msg) then
  begin
    ShowMessage(msg);
    Exit;
  end;

  tipoId := GetTipoSelecionadoId;

  if btCadastrar.Checked or (dmBase.tbldevices.State = dsInsert) then
  begin
    // INSERT
    novoId := dmBase.CreateDevice(Trim(edNome.Text), Trim(edPorta.Text), tipoId);
    if novoId > 0 then
      ShowMessage('Device inserido. ID=' + IntToStr(novoId))
    else
    begin
      ShowMessage('Falha ao inserir device.');
      Exit;
    end;
  end
  else
  begin
    // UPDATE do selecionado
    if (dsdevices.DataSet = nil) or dsdevices.DataSet.IsEmpty then
    begin
      ShowMessage('Nenhum registro selecionado para alterar.');
      Exit;
    end;
    idSel := dsdevices.DataSet.FieldByName('id_device').AsLargeInt;
    ok := dmBase.UpdateDevice(idSel, Trim(edNome.Text), Trim(edPorta.Text), tipoId);
    if ok then
      ShowMessage('Device alterado. ID=' + IntToStr(idSel))
    else
    begin
      ShowMessage('Falha ao alterar device.');
      Exit;
    end;
  end;

  dmBase.DevicesOpenAll;   // atualiza grid
  AtivarEdicaoUI(False);
  btCadastrar.Checked := False;
end;

procedure Tfrmcaddevice.dstiposDataChange(Sender: TObject; Field: TField);
begin
end;

procedure Tfrmcaddevice.dstiposStateChange(Sender: TObject);
begin
end;

procedure Tfrmcaddevice.FormCreate(Sender: TObject);
begin
  dmBase.DevicesOpenAll;
  dmBase.tbltipos.Open;
  dmBase.tbldevices.Open;

  // Se necessário, garanta no .lfm do lkTipos:
  // LookupSource=dstipos, LookupField=id_tipo, DisplayField=descricao
end;

procedure Tfrmcaddevice.FormShow(Sender: TObject);
begin
end;

end.

