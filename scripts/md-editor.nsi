; md-editor Windows 安装器（NSIS）
; 用法（在仓库根目录运行）: makensis /DVERSION=x.y.z scripts\md-editor.nsi

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

Name "md-editor ${VERSION}"
OutFile "dist\md-editor-${VERSION}-win64.exe"
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

  ; Markdown 文件类型关联（.md + .markdown）：默认打开为本应用，DefaultIcon 指向
  ; 文档专属图标 text-markdown.ico，与桌面应用图标（exe 内嵌）区分。安装到 Program
  ; Files 需要 admin（上方 RequestExecutionLevel），写 HKCR 即 HKLM\Software\Classes
  ; 对所有用户生效。卸载时仅在关联仍指向本应用才删除，避免误伤用户改绑的其他编辑器。
  WriteRegStr HKCR ".md" "" "md-editor.markdown"
  WriteRegStr HKCR ".markdown" "" "md-editor.markdown"
  WriteRegStr HKCR "md-editor.markdown" "" "Markdown 文档"
  WriteRegStr HKCR "md-editor.markdown\DefaultIcon" "" '"$INSTDIR\text-markdown.ico",0'
  WriteRegStr HKCR "md-editor.markdown\shell\open\command" "" '"$INSTDIR\md-editor.exe" "%1"'
  ; SHCNE_ASSOCCHANGED：通知 shell 刷新文件图标/关联缓存，图标立即生效
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'

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

Section "Uninstall"
  Delete "$INSTDIR\uninstall.exe"
  Delete "$INSTDIR\md-editor.exe"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\md-editor\md-editor.lnk"
  RMDir "$SMPROGRAMS\md-editor"
  Delete "$DESKTOP\md-editor.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\md-editor"

  ; 文件关联清理：仅当 .md/.markdown 当前仍指向本应用才删，防止卸载误伤用户改绑的程序
  ReadRegStr $0 HKCR ".md" ""
  StrCmp $0 "md-editor.markdown" 0 skipMd
  DeleteRegKey HKCR ".md"
  skipMd:
  ReadRegStr $0 HKCR ".markdown" ""
  StrCmp $0 "md-editor.markdown" 0 skipMarkdown
  DeleteRegKey HKCR ".markdown"
  skipMarkdown:
  DeleteRegKey HKCR "md-editor.markdown"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
