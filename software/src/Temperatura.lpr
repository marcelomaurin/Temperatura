program Temperatura;

{$mode objfpc}{$H+}

uses
  {$IFDEF UNIX}
  cthreads,
  {$ENDIF}
  {$IFDEF HASAMIGA}
  athreads,
  {$ENDIF}
  Interfaces, // this includes the LCL widgetset
  Forms, rxnew, LazSerialPort, zcomponent, anchordockpkg, tachartlazaruspkg,
  main, funcoes, setmain, base, caddevice, medidas, hint, configuracoes, 
reldiario
  { you can add units after this };

{$R *.res}

begin
  RequireDerivedFormResource:=True;
  Application.Scaled:=True;
  {$PUSH}{$WARN 5044 OFF}
  Application.MainFormOnTaskbar:=True;
  {$POP}
  Application.Initialize;
  Application.CreateForm(Tfrmmain, frmmain);
  Application.Run;
end.

