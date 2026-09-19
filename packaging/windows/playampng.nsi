; EN-09 — instalador Windows.
;
; Deliberadamente simples: instala, associa formatos, cria atalhos e desinstala.
; Nao mexe em servico, nao pede administrador se o usuario nao quiser, e nao
; instala nada fora do proprio diretorio.
;
; Compilar:  makensis -DVERSION=0.7.0 -DSOURCE_DIR=..\..\dist\windows playampng.nsi

Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef SOURCE_DIR
  !define SOURCE_DIR "..\..\dist\windows"
!endif

Name "PlayAmpNG ${VERSION}"
OutFile "PlayAmpNG-${VERSION}-setup.exe"
InstallDir "$LOCALAPPDATA\PlayAmpNG"
InstallDirRegKey HKCU "Software\PlayAmpNG" "InstallDir"

; Sem elevacao: instala no perfil do usuario. Um player de audio nao tem razao
; para pedir administrador, e pedir treina o usuario a aceitar o que nao devia.
RequestExecutionLevel user
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"

!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\playampng.exe"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "PortugueseBR"
!insertmacro MUI_LANGUAGE "English"

Section "PlayAmpNG" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${SOURCE_DIR}\*.*"

  WriteRegStr HKCU "Software\PlayAmpNG" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Entrada em Programas e Recursos.
  !define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlayAmpNG"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayName"     "PlayAmpNG"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr   HKCU "${UNINST_KEY}" "Publisher"       "PlayAmpNG"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\playampng.exe"
  WriteRegStr   HKCU "${UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr   HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Atalho no Menu Iniciar" SecStartMenu
  CreateDirectory "$SMPROGRAMS\PlayAmpNG"
  CreateShortcut "$SMPROGRAMS\PlayAmpNG\PlayAmpNG.lnk" "$INSTDIR\playampng.exe"
  CreateShortcut "$SMPROGRAMS\PlayAmpNG\Desinstalar.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section /o "Associar formatos de audio" SecAssoc
  ; Opcional e desmarcada por padrao: tomar as associacoes sem perguntar e
  ; hostil, e o usuario pode ja ter um player preferido.
  WriteRegStr HKCU "Software\Classes\PlayAmpNG.Audio" "" "Arquivo de audio"
  WriteRegStr HKCU "Software\Classes\PlayAmpNG.Audio\DefaultIcon" "" "$INSTDIR\playampng.exe,0"
  WriteRegStr HKCU "Software\Classes\PlayAmpNG.Audio\shell\open\command" "" '"$INSTDIR\playampng.exe" "%1"'

  StrCpy $0 0
  ${Do}
    ${Select} $0
      ${Case} 0
        StrCpy $1 ".mp3"
      ${Case} 1
        StrCpy $1 ".flac"
      ${Case} 2
        StrCpy $1 ".ogg"
      ${Case} 3
        StrCpy $1 ".opus"
      ${Case} 4
        StrCpy $1 ".wav"
      ${Case} 5
        StrCpy $1 ".m4a"
      ${CaseElse}
        ${Break}
    ${EndSelect}
    WriteRegStr HKCU "Software\Classes\$1" "" "PlayAmpNG.Audio"
    IntOp $0 $0 + 1
  ${Loop}
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain}      "O player e as bibliotecas de que ele depende."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Atalhos no Menu Iniciar."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc}     "Abrir MP3, FLAC, OGG, Opus, WAV e M4A com o PlayAmpNG."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  Delete "$SMPROGRAMS\PlayAmpNG\PlayAmpNG.lnk"
  Delete "$SMPROGRAMS\PlayAmpNG\Desinstalar.lnk"
  RMDir  "$SMPROGRAMS\PlayAmpNG"

  RMDir /r "$INSTDIR"

  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlayAmpNG"
  DeleteRegKey HKCU "Software\PlayAmpNG"
  DeleteRegKey HKCU "Software\Classes\PlayAmpNG.Audio"

  ; A CONFIGURACAO DO USUARIO NAO E APAGADA. Desinstalar nao e pedir para
  ; esquecer as preferencias, e reinstalar depois deve encontrar tudo como
  ; estava. Quem quiser zerar apaga %APPDATA%\PlayAmpNG na mao.
SectionEnd
