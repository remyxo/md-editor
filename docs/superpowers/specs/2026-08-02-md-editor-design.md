# Markdown 所见即所得编辑器 —— 设计规格

**日期**: 2026-08-02
**状态**: 草案

---

## 1. 概述

一款跨平台原生桌面 Markdown 阅读器及所见即所得(WYSIWYG)编辑器。一阶段支持标准 Markdown 语法；二阶段加入行内 HTML 及更丰富的渲染能力。

### 1.1 目标

- 原生桌面应用，界面现代化
- 所见即所得编辑，支持源码/预览切换
- 文件树、大纲面板、多标签页编辑
- 图标式工具栏（标题下拉、列表、表格、加粗、链接、插入图片等）
- 导出 PDF
- 一阶段部署于 Debian 13 Linux；后续跨平台

### 1.2 暂不包含

- 协作/实时编辑
- 云端同步
- 移动端支持
- 插件系统（二阶段）

---

## 2. 技术栈

| 层次 | 选型 | 理由 |
|------|------|------|
| 编程语言 | C++17 | 原生性能，与 Qt 无缝集成 |
| GUI 框架 | Qt 6.5+ | 最成熟的跨平台桌面框架 |
| 渲染引擎 | QWebEngine (Chromium) | 完整 CSS 渲染，WYSIWYG 保真度最高 |
| Web 编辑器 | TipTap (ProseMirror) | 成熟、MIT 协议、插件生态丰富、纯 JS 可用 |
| 构建系统 | CMake 3.20+ | Qt 标准构建工具，跨平台 |
| Markdown 解析 | cmark-gfm | C 语言库，符合 CommonMark 规范，GitHub 同款 |

### 2.1 Debian 13 依赖

```
qt6-base-dev
qt6-webengine-dev
qt6-webchannel-dev
cmake >= 3.20
gcc >= 12
```

全部可从 Debian 13 官方源直接安装。

---

## 3. 架构设计

### 3.1 总体结构

```
MainWindow (主窗口)
├── MenuBar          (文件 / 编辑 / 视图 / 插入 / 格式 / 帮助)
├── ToolBar          (图标按钮：标题下拉、加粗、斜体、列表、表格、链接、图片等)
├── SideBar (左侧)
│   ├── FileTree     (QTreeView + QFileSystemModel，过滤显示 .md 文件)
│   └── OutlineTree  (QTreeView，实时解析当前文档标题)
├── TabWidget        (QTabWidget 子类，多标签页 + 脏标记)
│   └── EditorTab    (每个标签页封装一个 QWebEngineView)
├── StatusBar        (行号/列号、字数统计、文件路径)
└── SearchBar        (查找/替换，下滑弹出面板)
```

### 3.2 数据流

```
工具栏/菜单点击
    → C++ 槽函数
    → EditorBridge → QWebChannel
    → JS (TipTap API) → DOM 更新

JS 编辑器状态变化 (光标、内容)
    → QWebChannel → EditorBridge 信号
    → C++ 槽函数 → 状态栏 / 标签页 / 大纲面板更新
```

### 3.3 组件目录树

```
src/
├── main.cpp                       # 程序入口
├── app/
│   ├── Application.{h,cpp}        # QApplication 子类，全局配置
│   └── ThemeManager.{h,cpp}       # 主题管理：亮色/暗色切换，QSS 加载
├── ui/
│   ├── MainWindow.{h,cpp}         # 主窗口：布局编排，信号路由
│   ├── MenuBar.{h,cpp}            # 菜单栏
│   ├── ToolBar.{h,cpp}            # 图标工具栏
│   ├── SideBar.{h,cpp}            # 左侧边栏容器（可折叠）
│   │   ├── FileTree.{h,cpp}       #   文件树面板
│   │   └── OutlineTree.{h,cpp}    #   大纲/目录面板
│   ├── TabWidget.{h,cpp}          # 多标签页容器
│   │   └── EditorTab.{h,cpp}      #   单个标签页 = QWebEngineView 封装
│   ├── StatusBar.{h,cpp}          # 状态栏
│   └── SearchBar.{h,cpp}          # 查找/替换面板
├── core/
│   ├── EditorBridge.{h,cpp}       # C++ ↔ JS 双向通信核心
│   ├── FileManager.{h,cpp}        # 文件读写、编码检测
│   ├── MarkdownHandler.{h,cpp}    # cmark-gfm 封装，MD→HTML 转换
│   └── PdfExporter.{h,cpp}        # PDF 导出（Chromium Print-to-PDF）
├── web/                            # 内嵌 Web 编辑器前端资源
│   ├── editor.html                #   编辑器入口页面
│   ├── editor.js                  #   TipTap 初始化 + Bridge 通信
│   ├── editor.css                 #   编辑器样式（跟随 Qt 主题）
│   └── qwebchannel.js             #   Qt 官方 QWebChannel JS 端
└── resources/
    ├── icons/                      # SVG 图标（Feather Icons 风格）
    ├── themes/
    │   ├── light.qss              #   亮色主题
    │   └── dark.qss               #   暗色主题
    └── app.qrc                    # Qt 资源文件
```

---

## 4. 通信协议（C++ ↔ JS）

### 4.1 EditorBridge（C++ 侧，暴露给 JS 调用）

```cpp
class EditorBridge : public QObject {
    Q_OBJECT
public slots:
    // 文件操作
    void fileOpen(const QString &path);        // 打开文件
    void fileSave(const QString &path,         // 保存文件
                  const QString &markdown);
    void fileNew();                            // 新建文档

    // 剪贴板
    void clipboardCopy(const QString &html,    // 复制（带格式）
                       const QString &text);
    void clipboardPaste();                     // 粘贴

    // 图片插入
    QString imageInsert();                     // 弹出文件选择对话框，返回路径

    // 导出
    void exportPdf(const QString &path);       // 导出为 PDF

    // 状态同步到 C++ 侧
    void cursorChanged(int line, int col);     // 光标位置变更 → 状态栏
    void contentChanged(bool dirty);           // 内容脏标记 → 标签页 *
    void headingsChanged(const QString &json); // 标题结构 JSON：{level, text, id}
};
```

### 4.2 JS 侧接口（C++ 通过 runJavaScript 调用）

| C++ 触发源 | JS 调用 | 说明 |
|---|---|---|
| 工具栏：加粗 | `editor.commands.toggleBold()` | 切换加粗 |
| 工具栏：斜体 | `editor.commands.toggleItalic()` | 切换斜体 |
| 工具栏：标题级别 N | `editor.commands.setHeading(N)` | 设置标题级别 |
| 工具栏：表格 | `editor.commands.insertTable(rows, cols)` | 插入表格 |
| 工具栏：链接 | `editor.commands.insertLink()` | 插入链接 |
| 工具栏：图片 | `editor.commands.insertImage(path)` | 插入图片 |
| 视图：源码切换 | `editor.commands.toggleSourceMode()` | 源码/WYSIWYG 切换 |
| 加载文件 | `editor.setContent(markdown)` | 设置编辑器内容 |
| 保存文件 | `editor.getContent()` → 返回 markdown | 获取编辑器内容 |

### 4.3 协议小结

```
方向：C++ → JS
  方式：QWebEnginePage::runJavaScript(script, callback)
  时机：工具栏/菜单操作、文件加载、主题变更

方向：JS → C++
  方式：QWebChannel (Qt WebChannel JavaScript API)
  时机：光标移动、内容变更、标题结构更新
```

---

## 5. 功能详述

### 5.1 工具栏

| 分组 | 按钮 | 功能 |
|------|------|------|
| 标题 | 正文 / H1–H6 下拉选择 | 设置段落标题级别 |
| 文本 | **B** / *I* / ~~S~~ / `<>` | 加粗、斜体、删除线、行内代码 |
| 结构 | 无序列表 / 有序列表 / 任务列表 / 引用 | 列表和引用块 |
| 插入 | 链接 / 图片 / 表格 / 分割线 | 插入各类元素 |
| 视图 | 源码切换 | 视图控制 |
| 导出 | PDF 导出 | 触发 PDF 导出流程 |

图标使用 SVG 线条风格（Feather Icons），颜色跟随主题自适应。

### 5.2 文件树

- `QTreeView` + `QFileSystemModel`，过滤显示 `.md`/`.markdown` 文件
- 根节点：用户打开的文件夹
- 双击文件 → 新建标签页或切换到已有标签页
- 右键菜单：新建文件、重命名、删除、在文件管理器中打开
- 拖拽支持：从文件管理器拖入 `.md` 文件直接打开
- 面板底部："打开文件夹"按钮

### 5.3 大纲面板

- 实时显示当前活动标签页的标题树结构
- JS 侧解析标题结构 → QWebChannel → C++ 更新 QTreeView 数据模型
- 点击标题 → 编辑器滚动到对应位置
- 根据光标位置自动高亮当前所在标题

### 5.4 标签页管理

- QTabWidget 子类化定制
- 标签标题：显示文件名 + 脏标记（未保存时显示 ●）
- 支持拖拽调整标签顺序
- 关闭未保存标签 → 弹窗提示：保存 / 不保存 / 取消
- 每个标签页独立维护撤销/重做栈（TipTap 内置）

### 5.5 PDF 导出

```
Markdown 源码
  → TipTap 渲染为 HTML
  → QWebChannel 将 HTML 传给 C++ 侧
  → 在隐藏的 QWebEngineView 中加载 HTML + 打印样式 CSS
  → QWebEngineView::printToPdf() 输出 PDF 文件
```

- 使用独立隐藏 WebView 专门用于 PDF 渲染，不影响编辑区域
- 自定义打印 CSS：分页控制、页边距、页眉页脚、页码
- 支持配置纸张大小（A4/Letter）和方向（纵向/横向）

### 5.6 文件监控与会话恢复

- `QFileSystemWatcher` 监控已打开文件的外部变更
- 检测到外部修改 → 弹窗提示"文件已在外部被修改，是否重新加载？"
- 会话恢复：关闭时保存标签页列表 + 光标位置 + 未保存内容备份

### 5.7 设置系统

通过 `QSettings` 存储，Linux 下路径为 `~/.config/md-editor/`。

| 分类 | 设置项 |
|------|--------|
| 通用 | 启动时恢复会话：[启用] |
| 编辑器 | 字体；字号 [14px]；显示行号：[启用]；自动换行：[启用]；Tab 宽度 [4空格] |
| 外观 | 主题：跟随系统 / 亮色 / 暗色；界面语言：简体中文 / English |
| 导出 | 默认纸张：A4 / Letter；默认方向：纵向 / 横向 |

---

## 6. 主题系统

### 6.1 设计语言

- **风格**：扁平、极简、现代化（类似 VS Code / Sublime Text）
- **基础**：Qt6 Fusion 风格，通过 QSS 深度定制
- **主题**：亮色 + 暗色双主题，运行时通过 ThemeManager 切换
- **图标**：SVG 线条风格，颜色自适应主题
- **动效**：侧边栏折叠、面板尺寸调整时使用 QPropertyAnimation 过渡

### 6.2 QSS 方案

- 每个主题一个 `.qss` 文件
- ThemeManager 加载后通过 `qApp->setStyleSheet()` 全局应用
- WebView 编辑器主题通过 JS 通道同步切换

---

## 7. 分阶段计划

### 一阶段（标准 Markdown）

- 完整 CommonMark + GFM 语法：
  标题、加粗、斜体、删除线、链接、图片、代码（行内 + 代码块含语法高亮）、引用、有序/无序列表、任务列表、表格、分割线
- 数学公式（LaTeX）—— 通过 TipTap math 扩展
- 流程图/时序图（Mermaid）—— 通过 TipTap mermaid 扩展
- 源码/WYSIWYG 切换
- 文件树 + 大纲面板
- 多标签页编辑
- 图标工具栏
- PDF 导出
- 亮色/暗色主题
- 会话恢复
- 设置对话框

### 二阶段（HTML + 更丰富展现）

- Markdown 中嵌入行内 HTML（`<div style="...">`、`<span>` 等）
- 自定义 CSS 样式
- HTML 导出
- Word (docx) 导出
- 插件系统

---

## 8. 测试策略

| 层级 | 范围 | 工具 |
|------|------|------|
| C++ 单元测试 | FileManager、MarkdownHandler、EditorBridge、PdfExporter | Qt Test（QTestLib） |
| JS 编辑器测试 | TipTap 插件行为、Markdown 解析往返验证 | Vitest |
| 集成测试 | C++ ↔ JS 通信协议、加载-编辑-保存完整流程 | 自定义脚本 |
| 手工验收 | UI 交互体验、打包部署、PDF 输出质量 | 手工测试 |

---

## 9. 构建与部署

### 9.1 构建步骤

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

输出：单个 `md-editor` 可执行文件 + 内嵌的 Web 资源（通过 qrc 打包）。

### 9.2 Debian 打包（后续）

- 使用 CPack 生成 `.deb` 安装包
- 运行时依赖：`qt6-base`、`qt6-webengine`、`qt6-webchannel`（均来自 Debian 官方源）
- 备选方案：通过 `linuxdeployqt` 打包为 AppImage

---

## 10. 待实施阶段确定的事项

- 具体图标集选择（Feather Icons vs Material Symbols）
- 各平台默认字体
- 是否加入"专注模式"—— 根据一阶段用户反馈决定
- 国际化框架（Qt Linguist）—— 一阶段仅支持简体中文与英语
