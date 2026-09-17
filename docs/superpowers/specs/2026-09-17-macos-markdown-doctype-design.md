# macOS 声明支持 Markdown 文档类型 — 设计文档

日期：2026-09-17
状态：已确认（用户已批准方案与详细设计）

## 背景与问题

用户在 macOS 上右键 .md 文件 →「打开方式」时，md-editor 在**推荐的应用列表中呈灰色**，
需切换到「全部应用」才能选中。

根因分析（v0.13.0 起 `scripts/package-macos.sh` 已有文档类型声明，但存在两处缺陷）：

1. **声明只按扩展名匹配，未按 UTI 匹配**：`CFBundleDocumentTypes` 只声明了
   `CFBundleTypeExtensions`（md/markdown），没有 `LSItemContentTypes`。现代 macOS
   （11+）把 .md 解析为系统 UTI `net.daringfireball.markdown`，「打开方式」推荐列表
   按 **UTI** 匹配过滤应用 → 纯扩展名匹配的应用被灰掉。
2. **自定义 UTI 与系统冲突**：注入的 `UTImportedTypeDeclarations`
   （`com.md-editor.markdown`）与系统内建的 `net.daringfireball.markdown`
   争抢同一扩展名，类型解析与图标绑定行为不稳定。

另有一个连带缺口：**C++ 侧没有处理 `QFileOpenEvent`**。macOS 上 Finder 双击 /
「打开方式」启动应用时文件路径不走 argv（`main.cpp` 的 argv 解析只覆盖
Linux .desktop `%f` 场景），而是走 Apple Event → Qt 转成 `QEvent::FileOpen`。
当前无人处理 → 应用启动后是空窗口。若只修声明不修事件，用户双击后体验断链。

## 方案（已选定 A）

**A. UTI 对齐系统声明 + 补齐 QFileOpenEvent（本设计）**
- `LSItemContentTypes` 指向系统 UTI `net.daringfireball.markdown`，删除自定义
  imported UTI；Typora/MacDown/Obsidian 均采用此方式
- `Application` 捕获 `QEvent::FileOpen` 转发信号，MainWindow 复用既有打开逻辑

否决的备选：
- **B. 只修 plist 声明**：修完灰色后双击 → 应用启动、窗口空白，体验断链
- **C. 自定义 UTI 导出（UTExportedTypeDeclarations）**：与系统 UTI 正面冲突，
  可能劫持其他编辑器的绑定、引发图标错乱

## 一、plist 注入最终形态（`scripts/package-macos.sh`）

注入后的 `Info.plist` 关键段（PlistBuddy 命令调整，保持先删后加的幂等性）：

```
CFBundleDocumentTypes:
  [0]:
    CFBundleTypeName:       Markdown 文档                     （不变）
    CFBundleTypeRole:       Editor                            （不变）
    LSHandlerRank:          Owner                             （不变）
    LSItemContentTypes:     [net.daringfireball.markdown]     （新增，解决「灰色」）
    CFBundleTypeExtensions: [md, markdown]                    （保留，老系统兜底）
    CFBundleTypeIconFile:   text-markdown                      （不变）

UTImportedTypeDeclarations: （整段删除——自定义 UTI 与系统 UTI 冲突）
```

要点：
- `LSItemContentTypes` 按系统 UTI 匹配，是推荐列表过滤的依据 → 解决灰色
- `CFBundleTypeExtensions` 保留：macOS 11 及更早系统无内建 md UTI 时按扩展名兜底
- 删除 `UTImportedTypeDeclarations` 后，同扩展名只剩系统 UTI 一个声明者，解析稳定
- `LSHandlerRank: Owner` 表达「希望成为默认」的意图；最终默认与否仍由用户
  「打开方式」决定（macOS 机制，应用无法自封默认）
- `CFBundleTypeIconFile` 只在 md-editor 是该文件默认打开应用时生效，不是默认时
  Finder 用系统图标——macOS 规则，本设计不改变该行为

注入完成后追加断言（脚本已有 `set -euo pipefail`，失败即阻断打包）：

```bash
"$PLBUDDY" -c "Print :CFBundleDocumentTypes:0:LSItemContentTypes" "$PLIST"
```

key 缺失时 PlistBuddy 非零退出。

## 二、C++ 事件流（3 个文件，改动都很小）

```
Finder 双击/「打开方式」
  → Launch Services → Apple Event → Qt 转为 QEvent::FileOpen
  → Application::event() 捕获，取 QFileOpenEvent::file()
  → 发射 Application::fileOpenRequested(path) 信号（新增）
  → MainWindow::connectSignals() 内连接到现有 private slot onFileDoubleClicked
  → 走既有打开逻辑（存在性检查 → 开标签 → 大文件警告等全部复用）
```

| 文件 | 改动 |
|------|------|
| `src/app/Application.h` | 新增信号 `fileOpenRequested(const QString &path)`；声明 `event()` 重写 |
| `src/app/Application.cpp` | 实现 `event()`：`QEvent::FileOpen` 且路径非空 → 发信号并返回 true；其余交基类 |
| `src/ui/MainWindow.cpp` | `connectSignals()` 加一行：连接 `Application::instance()` 的信号到 `onFileDoubleClicked`（private slot，内部连接，不改访问级别，符合「MainWindow 是信号编排中枢」约定） |

`main.cpp` 不动（argv 解析保留，服务 Linux/Windows）。

时序说明：连接在 `main()` 的窗口构造阶段建立、`exec()` 之前完成；`QFileOpenEvent`
在事件循环启动后投递 → 冷启动双击与运行中再双击都覆盖。macOS 上 argv 通常不含
文件路径，与 argv 解析无重叠。

## 三、测试与验证

- **单元测试**（新增 `tests/test_application.cpp`，`QTEST_APPLESS_MAIN` + 自建
  `Application` 实例，仿 `test_pdfexporter.cpp` 的自建 app 模式）：
  - `postEvent` 一个 `QFileOpenEvent("/tmp/xx.md")` → `sendPostedEvents` →
    `QSignalSpy` 断言信号携带正确路径
  - 空路径 `QFileOpenEvent` 不发信号
  - 可行性已核实：`ThemeManager::loadThemeFile` 对 qrc 缺失静默容错（open 失败
    即跳过），测试目标无需链接资源文件，直接链 `Application.cpp` +
    `ThemeManager.cpp` + `LanguageManager.cpp` 即可
- **CI 断言**：上述 PlistBuddy Print 断言随每次 macOS 打包生效
- **真机验证**（macOS，用户执行）：装新 DMG 后
  1. 右键 .md →「打开方式」→ 推荐列表应出现 md-editor 且**不灰**
  2. 选中打开 → 文件正常加载到编辑器
  3. 「始终以此方式打开」后双击 .md → 直接打开并加载
  4. 应用运行中再双击另一 .md → 应开新标签
  5. 旧版本（v0.16.0）注册记录干扰时：
     `/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f <应用路径>` 强刷
- ad-hoc 签名 + 未公证的现状不变：用户首次仍需「右键 → 打开」（与本设计无关）

## 影响面与版本

- 改动文件：`scripts/package-macos.sh`、`src/app/Application.{h,cpp}`、
  `src/ui/MainWindow.cpp`、`tests/test_application.cpp`（新增）、
  `tests/CMakeLists.txt`
- 含 C++ 改动 → 推送前预检（容器 clang + win11 VM MSVC）自动生效
- 版本推进：改动小（bug 修复性质）→ 按 CLAUDE.md 规则预计 `+0.0.1`（0.16.1），
  以实现完成后的实际增量为准确认；推进时同步 CHANGELOG.md 与 tag
