; md-editor Windows 安装器（NSIS）
; 用法（在仓库根目录运行）: makensis /DVERSION=x.y.z scripts\md-editor.nsi

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

Name "md-editor ${VERSION}"
OutFile "md-editor-${VERSION}-win64.exe"
InstallDir "$PROGRAMFILES64\md-editor"
RequestExecutionLevel admin
Unicode true
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "安装"
  SetOutPath "$INSTDIR"
  File /r "dist\md-editor\*.*"

  ; 卸载程序
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; 开始菜单 + 桌面快捷方式
  CreateDirectory "$SMPROGRAMS\md-editor"
  CreateShortcut "$SMPROGRAMS\md-editor\md-editor.lnk" "$INSTDIR\md-editor.exe"
  CreateShortcut "$DESKTOP\md-editor.lnk" "$INSTDIR\md-editor.exe"

  ; 卸载信息注册
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\md-editor" \
    "DisplayName" "md-editor"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\md-editor" \
    "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\md-editor" \
    "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\md-editor" \
    "Publisher" "md-editor"
SectionEnd

Section "卸载"
  Delete "$INSTDIR\uninstall.exe"
  Delete "$INSTDIR\md-editor.exe"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\md-editor\md-editor.lnk"
  RMDir "$SMPROGRAMS\md-editor"
  Delete "$DESKTOP\md-editor.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\md-editor"
SectionEnd
