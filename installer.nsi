Unicode true
!define APP "Nitro OSD"
!define EXE "NitroOSD.exe"
!define VER "1.1.0"
!define UNREG "Software\Microsoft\Windows\CurrentVersion\Uninstall\NitroOSD"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

Name "${APP}"
OutFile "NitroOSD_Setup_${VER}.exe"
InstallDir "$PROGRAMFILES64\${APP}"
InstallDirRegKey HKLM "Software\NitroOSD" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "NITRO OSD // ${VER}"
ShowInstDetails show

VIProductVersion "1.1.0.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "${APP} Setup"
VIAddVersionKey "FileVersion" "${VER}"
VIAddVersionKey "LegalCopyright" "Nitro PM"

!define MUI_ICON "assets\app.ico"
!define MUI_UNICON "assets\app.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "assets\header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "assets\header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "assets\welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "assets\welcome.bmp"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Jalankan Nitro OSD"
!define MUI_WELCOMEPAGE_TITLE "NITRO OSD"
!define MUI_WELCOMEPAGE_TEXT "OSD untuk Acer Nitro Lite 16: Caps Lock, Num Lock, mikrofon, touchpad, dan backlight keyboard.$\r$\n$\r$\nKlik Selanjutnya untuk melanjutkan."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "Indonesian"

Function .onInit
  SetRegView 64
FunctionEnd
Function un.onInit
  SetRegView 64
FunctionEnd

Section "Nitro OSD (wajib)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  nsExec::Exec 'taskkill /F /IM ${EXE}'
  Sleep 600
  File "NitroOSD.exe"
  File "osd_probe.ps1"
  File "kb_probe.ps1"
  File "touchpad_probe.ps1"
  File "README.txt"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\NitroOSD" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNREG}" "DisplayName" "${APP}"
  WriteRegStr HKLM "${UNREG}" "DisplayVersion" "${VER}"
  WriteRegStr HKLM "${UNREG}" "Publisher" "Nitro PM"
  WriteRegStr HKLM "${UNREG}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKLM "${UNREG}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNREG}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNREG}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNREG}" "NoModify" 1
  WriteRegDWORD HKLM "${UNREG}" "NoRepair" 1
  SetShellVarContext all
  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortcut "$SMPROGRAMS\${APP}\${APP}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
  CreateShortcut "$SMPROGRAMS\${APP}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Pintasan Desktop" SecDesktop
  SetShellVarContext all
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
SectionEnd

Section "Jalankan otomatis saat login (Admin, tanpa UAC)" SecTask
  DetailPrint "Membuat tugas Task Scheduler..."
  ExecWait '"$INSTDIR\${EXE}" --task-on'
SectionEnd

LangString DESC_Main ${LANG_INDONESIAN} "Program utama, skrip probe tombol/WMI, dan panduan."
LangString DESC_Desk ${LANG_INDONESIAN} "Tambahkan ikon di Desktop."
LangString DESC_Task ${LANG_INDONESIAN} "Agar OSD aktif sejak Windows menyala (tanpa pertanyaan UAC)."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_Main)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desk)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecTask} $(DESC_Task)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  nsExec::Exec 'taskkill /F /IM ${EXE}'
  Sleep 600
  DetailPrint "Mengaktifkan kembali touchpad dan mikrofon..."
  ExecWait '"$INSTDIR\${EXE}" --restore'
  ExecWait '"$INSTDIR\${EXE}" --task-off'
  SetShellVarContext all
  Delete "$DESKTOP\${APP}.lnk"
  Delete "$SMPROGRAMS\${APP}\${APP}.lnk"
  Delete "$SMPROGRAMS\${APP}\Uninstall.lnk"
  RMDir "$SMPROGRAMS\${APP}"
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\osd_probe.ps1"
  Delete "$INSTDIR\kb_probe.ps1"
  Delete "$INSTDIR\touchpad_probe.ps1"
  Delete "$INSTDIR\kb_probe_result.txt"
  Delete "$INSTDIR\osd_probe_result.txt"
    Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNREG}"
  DeleteRegKey HKLM "Software\NitroOSD"
  IfSilent +3
  MessageBox MB_YESNO|MB_ICONQUESTION "Hapus juga setelan (%APPDATA%\NitroOSD)?" IDNO +2
  RMDir /r "$APPDATA\NitroOSD"
SectionEnd
