; Vial Armed installer: makensis /DVERSION=x.y.z /DSRC=<windeployqt folder> /DOUT=<setup.exe> installer.nsi
; Installs for all users when run elevated, otherwise into the user's local app data.

!include MUI2.nsh
!include FileFunc.nsh

!define APP "Vial Armed"
!define EXE "vial-armed.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\vial-armed"

!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!include MultiUser.nsh
!include LogicLib.nsh

Name "${APP} ${VERSION}"
OutFile "${OUT}"
Unicode true
!define MUI_ICON "..\..\resources\icons\Icon.ico"
!define MUI_UNICON "..\..\resources\icons\Icon.ico"

Function .onInit
  !insertmacro MULTIUSER_INIT
  ${If} $InstDir == ""
    ${If} $MultiUser.InstallMode == "AllUsers"
      StrCpy $InstDir "$PROGRAMFILES64\${APP}"
    ${Else}
      StrCpy $InstDir "$LOCALAPPDATA\${APP}"
    ${EndIf}
  ${EndIf}
FunctionEnd

Function un.onInit
  !insertmacro MULTIUSER_UNINIT
FunctionEnd

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$InstDir\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APP}"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section
  ; a running copy (often in the tray) would keep its files locked
  nsExec::Exec 'taskkill /IM ${EXE} /F'
  SetOutPath "$InstDir"
  File /r "${SRC}\*"
  WriteUninstaller "$InstDir\uninstall.exe"
  CreateShortCut "$SMPROGRAMS\${APP}.lnk" "$InstDir\${EXE}"
  WriteRegStr SHCTX "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr SHCTX "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr SHCTX "${UNINST_KEY}" "DisplayIcon" "$InstDir\${EXE}"
  WriteRegStr SHCTX "${UNINST_KEY}" "Publisher" "backflipinc"
  WriteRegStr SHCTX "${UNINST_KEY}" "UninstallString" '"$InstDir\uninstall.exe" /$MultiUser.InstallMode'
  WriteRegStr SHCTX "${UNINST_KEY}" "QuietUninstallString" '"$InstDir\uninstall.exe" /$MultiUser.InstallMode /S'
  ${GetSize} "$InstDir" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD SHCTX "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM ${EXE} /F'
  RMDir /r "$InstDir"
  Delete "$SMPROGRAMS\${APP}.lnk"
  ; the HUD agent's login entry, if it was turned on
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "vial-armed-agent"
  DeleteRegKey SHCTX "${UNINST_KEY}"
SectionEnd
