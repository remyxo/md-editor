# Markdown 所见即所得编辑器 —— 实施计划

> **面向实施人员：** 请使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 按任务逐步实施。步骤使用复选框（`- [ ]`）语法跟踪进度。

**目标：** 构建一个基于 Qt6 + QWebEngine + TipTap 的原生桌面 Markdown WYSIWYG 编辑器，支持标准 Markdown 语法的编辑、预览、文件管理和 PDF 导出。

**架构：** Qt6 C++ 负责桌面外壳（窗口、菜单、工具栏、侧边栏、标签页），每个标签页内嵌 QWebEngineView 加载 TipTap Web 编辑器，C++ 与 JS 通过 QWebChannel 双向通信。

**技术栈：** C++17、Qt 6.5+（Widgets、WebEngineWidgets、WebChannel）、TipTap（ProseMirror）、cmark-gfm、CMake 3.20+

## 全局约束

- 所有注释和使用者可见的字符串使用中文
- 代码标识符（类名、方法名、变量名）使用英文
- 一阶段部署于 Debian 13，依赖均来自官方 apt 源
- 跟随已有设计规格文档 `docs/superpowers/specs/2026-08-02-md-editor-design.md`
- 文档输出使用中文

---

### 任务 1：项目脚手架与构建系统

**文件：**
- 创建：`CMakeLists.txt`
- 创建：`src/main.cpp`
- 创建：`src/app/Application.h`、`src/app/Application.cpp`
- 创建：`src/resources/app.qrc`
- 创建：`src/resources/icons/.gitkeep`
- 创建：`src/resources/themes/.gitkeep`
- 创建：`src/resources/web/.gitkeep`

**接口：**
- 产出：可编译运行的最小 Qt6 应用，显示空白窗口
- 产出：`Application` 类提供全局配置访问（单例模式）
- 产出：`app.qrc` Qt 资源文件骨架

- [ ] **步骤 1：检查 Debian 13 上的 Qt6 依赖**

```bash
apt list --installed 2>/dev/null | grep -E 'qt6-base-dev|qt6-webengine-dev|qt6-webchannel-dev|cmake'
```

如缺失则安装：
```bash
sudo apt install -y qt6-base-dev qt6-webengine-dev qt6-webchannel-dev cmake g++
```

- [ ] **步骤 2：编写顶层 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(md-editor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets WebEngineWidgets WebChannel)

add_subdirectory(src)
```

- [ ] **步骤 3：编写 src/CMakeLists.txt**

```cmake
set(SOURCES
    main.cpp
    app/Application.cpp
)

set(HEADERS
    app/Application.h
)

qt_add_resources(RESOURCES resources/app.qrc)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebChannel
)
```

- [ ] **步骤 4：编写 Application 类**

`src/app/Application.h`:
```cpp
#ifndef MD_EDITOR_APPLICATION_H
#define MD_EDITOR_APPLICATION_H

#include <QApplication>

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    static Application *instance();
};

#endif // MD_EDITOR_APPLICATION_H
```

`src/app/Application.cpp`:
```cpp
#include "Application.h"

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    setApplicationName("md-editor");
    setApplicationDisplayName("Markdown Editor");
    setApplicationVersion("0.1.0");
    setOrganizationName("md-editor");
}

Application *Application::instance()
{
    return qobject_cast<Application *>(QCoreApplication::instance());
}
```

- [ ] **步骤 5：编写 main.cpp**

```cpp
#include <QMainWindow>
#include "app/Application.h"

int main(int argc, char *argv[])
{
    Application app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("Markdown Editor");
    window.resize(1280, 800);
    window.show();

    return app.exec();
}
```

- [ ] **步骤 6：创建资源文件骨架**

`src/resources/app.qrc`:
```xml
<RCC>
    <qresource prefix="/">
    </qresource>
</RCC>
```

- [ ] **步骤 7：创建 .gitkeep 占位文件**

```bash
touch src/resources/icons/.gitkeep
touch src/resources/themes/.gitkeep
touch src/resources/web/.gitkeep
```

- [ ] **步骤 8：构建并验证**

```bash
cd /home/daijd/claude-workspace/md-editor
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

预期：编译成功，生成 `md-editor` 可执行文件。

- [ ] **步骤 9：运行验证窗口显示**

```bash
./md-editor
```

预期：显示标题为"Markdown Editor"的空白窗口，尺寸约 1280×800，可正常关闭。

- [ ] **步骤 10：提交**

```bash
cd /home/daijd/claude-workspace/md-editor
git init
git add CMakeLists.txt src/ .gitkeep
git commit -m "feat: 初始化项目脚手架，CMake + Qt6 + Application 骨架"
```

---

### 任务 2：主题系统

**文件：**
- 创建：`src/app/ThemeManager.h`、`src/app/ThemeManager.cpp`
- 创建：`src/resources/themes/light.qss`
- 创建：`src/resources/themes/dark.qss`
- 修改：`src/resources/app.qrc`（添加主题文件）
- 修改：`src/app/Application.h`、`src/app/Application.cpp`（集成 ThemeManager）
- 修改：`src/CMakeLists.txt`（添加 ThemeManager 源文件）

**接口：**
- 产出：`ThemeManager` 类，提供 `switchTheme(name)` 和 `currentTheme()` 方法
- 产出：`themeChanged(QString name)` 信号，供其他组件响应主题切换
- 支持 `"light"` 和 `"dark"` 两种主题，默认 `"light"`

- [ ] **步骤 1：编写亮色主题 QSS**

`src/resources/themes/light.qss`:
```css
/* 亮色主题 — 现代化扁平风格 */
QMainWindow {
    background-color: #f5f5f5;
}

QMenuBar {
    background-color: #ffffff;
    border-bottom: 1px solid #e0e0e0;
    padding: 2px 0;
    font-size: 13px;
}

QMenuBar::item {
    padding: 4px 12px;
    border-radius: 4px;
    margin: 2px 2px;
}

QMenuBar::item:selected {
    background-color: #e8e8e8;
}

QMenu {
    background-color: #ffffff;
    border: 1px solid #d0d0d0;
    border-radius: 6px;
    padding: 4px 0;
}

QMenu::item {
    padding: 6px 32px 6px 16px;
    font-size: 13px;
}

QMenu::item:selected {
    background-color: #0078d4;
    color: #ffffff;
}

QMenu::separator {
    height: 1px;
    background: #e0e0e0;
    margin: 4px 8px;
}

QToolBar {
    background-color: #ffffff;
    border-bottom: 1px solid #e0e0e0;
    padding: 4px 8px;
    spacing: 4px;
}

QToolButton {
    border: none;
    border-radius: 4px;
    padding: 6px 8px;
    font-size: 14px;
}

QToolButton:hover {
    background-color: #e8e8e8;
}

QToolButton:pressed, QToolButton:checked {
    background-color: #d0d0d0;
}

QTreeView {
    background-color: #ffffff;
    border: none;
    font-size: 13px;
}

QTreeView::item {
    padding: 4px 8px;
    border-radius: 4px;
}

QTreeView::item:selected {
    background-color: #0078d4;
    color: #ffffff;
}

QTreeView::item:hover:!selected {
    background-color: #e8e8e8;
}

QTabWidget::pane {
    border: none;
    background-color: #f5f5f5;
}

QTabBar::tab {
    background-color: #e8e8e8;
    border: none;
    border-radius: 4px;
    padding: 6px 16px;
    margin: 2px 2px;
    font-size: 13px;
}

QTabBar::tab:selected {
    background-color: #ffffff;
}

QTabBar::tab:hover:!selected {
    background-color: #dcdcdc;
}

QStatusBar {
    background-color: #ffffff;
    border-top: 1px solid #e0e0e0;
    font-size: 12px;
    color: #666666;
    padding: 2px 8px;
}

QSplitter::handle {
    background-color: #e0e0e0;
    width: 1px;
}

QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background: #c0c0c0;
    border-radius: 4px;
    min-height: 30px;
}

QScrollBar::handle:vertical:hover {
    background: #a0a0a0;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
```

- [ ] **步骤 2：编写暗色主题 QSS**

`src/resources/themes/dark.qss`:
```css
/* 暗色主题 — 现代化扁平风格 */
QMainWindow {
    background-color: #1e1e1e;
}

QMenuBar {
    background-color: #2d2d2d;
    border-bottom: 1px solid #3d3d3d;
    padding: 2px 0;
    font-size: 13px;
    color: #cccccc;
}

QMenuBar::item {
    padding: 4px 12px;
    border-radius: 4px;
    margin: 2px 2px;
}

QMenuBar::item:selected {
    background-color: #3d3d3d;
}

QMenu {
    background-color: #2d2d2d;
    border: 1px solid #3d3d3d;
    border-radius: 6px;
    padding: 4px 0;
    color: #cccccc;
}

QMenu::item {
    padding: 6px 32px 6px 16px;
    font-size: 13px;
}

QMenu::item:selected {
    background-color: #0078d4;
    color: #ffffff;
}

QMenu::separator {
    height: 1px;
    background: #3d3d3d;
    margin: 4px 8px;
}

QToolBar {
    background-color: #2d2d2d;
    border-bottom: 1px solid #3d3d3d;
    padding: 4px 8px;
    spacing: 4px;
}

QToolButton {
    border: none;
    border-radius: 4px;
    padding: 6px 8px;
    color: #cccccc;
    font-size: 14px;
}

QToolButton:hover {
    background-color: #3d3d3d;
}

QToolButton:pressed, QToolButton:checked {
    background-color: #4d4d4d;
}

QTreeView {
    background-color: #252526;
    border: none;
    font-size: 13px;
    color: #cccccc;
}

QTreeView::item {
    padding: 4px 8px;
    border-radius: 4px;
}

QTreeView::item:selected {
    background-color: #094771;
    color: #ffffff;
}

QTreeView::item:hover:!selected {
    background-color: #2a2d2e;
}

QTabWidget::pane {
    border: none;
    background-color: #1e1e1e;
}

QTabBar::tab {
    background-color: #2d2d2d;
    border: none;
    border-radius: 4px;
    padding: 6px 16px;
    margin: 2px 2px;
    font-size: 13px;
    color: #999999;
}

QTabBar::tab:selected {
    background-color: #1e1e1e;
    color: #ffffff;
}

QTabBar::tab:hover:!selected {
    background-color: #3d3d3d;
}

QStatusBar {
    background-color: #2d2d2d;
    border-top: 1px solid #3d3d3d;
    font-size: 12px;
    color: #999999;
    padding: 2px 8px;
}

QSplitter::handle {
    background-color: #3d3d3d;
    width: 1px;
}

QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background: #424242;
    border-radius: 4px;
    min-height: 30px;
}

QScrollBar::handle:vertical:hover {
    background: #555555;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
```

- [ ] **步骤 3：编写 ThemeManager 类**

`src/app/ThemeManager.h`:
```cpp
#ifndef MD_EDITOR_THEMEMANAGER_H
#define MD_EDITOR_THEMEMANAGER_H

#include <QObject>
#include <QString>

class ThemeManager : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(QObject *parent = nullptr);

    QString currentTheme() const;
    QStringList availableThemes() const;

public slots:
    void switchTheme(const QString &name);

signals:
    void themeChanged(const QString &name);

private:
    void loadThemeFile(const QString &name);
    QString m_currentTheme;
};

#endif // MD_EDITOR_THEMEMANAGER_H
```

`src/app/ThemeManager.cpp`:
```cpp
#include "ThemeManager.h"
#include <QFile>
#include <QApplication>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_currentTheme("light")
{
}

QString ThemeManager::currentTheme() const
{
    return m_currentTheme;
}

QStringList ThemeManager::availableThemes() const
{
    return {"light", "dark"};
}

void ThemeManager::switchTheme(const QString &name)
{
    if (name == m_currentTheme)
        return;

    loadThemeFile(name);
    m_currentTheme = name;
    emit themeChanged(name);
}

void ThemeManager::loadThemeFile(const QString &name)
{
    QString path = QString(":/themes/%1.qss").arg(name);
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString styleSheet = QString::fromUtf8(file.readAll());
        qApp->setStyleSheet(styleSheet);
        file.close();
    }
}
```

- [ ] **步骤 4：更新 Application 类集成 ThemeManager**

修改 `src/app/Application.h`，添加：
```cpp
// 在类声明中添加
public:
    ThemeManager *themeManager() const;

private:
    ThemeManager *m_themeManager;
```

修改 `src/app/Application.cpp`，在构造函数中添加：
```cpp
#include "ThemeManager.h"

// 构造函数末尾添加：
m_themeManager = new ThemeManager(this);
m_themeManager->switchTheme("light");
```

添加方法：
```cpp
ThemeManager *Application::themeManager() const
{
    return m_themeManager;
}
```

- [ ] **步骤 5：更新 app.qrc 添加主题资源**

修改 `src/resources/app.qrc`:
```xml
<RCC>
    <qresource prefix="/">
        <file>themes/light.qss</file>
        <file>themes/dark.qss</file>
    </qresource>
</RCC>
```

- [ ] **步骤 6：更新 CMakeLists.txt 添加 ThemeManager**

修改 `src/CMakeLists.txt`，在 SOURCES 和 HEADERS 中添加 `app/ThemeManager.cpp` 和 `app/ThemeManager.h`。

- [ ] **步骤 7：更新 main.cpp 使用 Application 类**

修改 `src/main.cpp`，将 `QApplication` 替换为 `Application`，`QMainWindow` 保持不变。

- [ ] **步骤 8：构建并验证主题加载正常**

```bash
cd build && cmake .. && make -j$(nproc) && ./md-editor
```

预期：窗口背景色为亮色主题的浅灰（#f5f5f5），无 QSS 解析错误。

- [ ] **步骤 9：提交**

```bash
git add src/app/ThemeManager.* src/resources/themes/ src/resources/app.qrc src/app/Application.* src/main.cpp src/CMakeLists.txt
git commit -m "feat: 添加主题系统，支持亮色/暗色双主题"
```

---

### 任务 3：Web 编辑器前端页面

**文件：**
- 创建：`src/resources/web/editor.html`
- 创建：`src/resources/web/editor.js`
- 创建：`src/resources/web/editor.css`
- 创建：`src/resources/web/qwebchannel.js`（从 Qt6 安装目录复制）
- 修改：`src/resources/app.qrc`（添加 web 资源）

**接口：**
- 产出：完整的 TipTap 编辑器 Web 页面，可独立在浏览器中打开测试
- 产出：JS 侧暴露 `window.editorApi` 对象，供 C++ 通过 QWebChannel 调用
- 消耗：Qt QWebChannel JavaScript API（`qwebchannel.js`）

- [ ] **步骤 1：复制 Qt 官方的 qwebchannel.js**

```bash
QT_JS_PATH=$(find /usr -path "*/qt6/examples/webchannel/shared/qwebchannel.js" 2>/dev/null | head -1)
if [ -z "$QT_JS_PATH" ]; then
    # 备选：从 Qt6 WebChannel 包中查找
    QT_JS_PATH=$(find /usr -name "qwebchannel.js" -path "*qt6*" 2>/dev/null | head -1)
fi
cp "$QT_JS_PATH" src/resources/web/qwebchannel.js
```

如找不到，从以下 URL 下载：
```
https://code.qt.io/cgit/qt/qtwebchannel.git/plain/examples/webchannel/shared/qwebchannel.js
```

- [ ] **步骤 2：下载 TipTap 及其扩展的 UMD 包**

由于使用 QWebEngine 内嵌，不需要 npm 构建流程。从 CDN 下载预编译的 UMD 包（使用 jsDelivr）：

```bash
WEB_DIR=src/resources/web

# TipTap 核心
wget -O $WEB_DIR/tiptap-core.js https://cdn.jsdelivr.net/npm/@tiptap/core@2.6.0/dist/tiptap-core.umd.min.js
wget -O $WEB_DIR/tiptap-starter-kit.js https://cdn.jsdelivr.net/npm/@tiptap/starter-kit@2.6.0/dist/tiptap-starter-kit.umd.min.js
wget -O $WEB_DIR/tiptap-extension-table.js https://cdn.jsdelivr.net/npm/@tiptap/extension-table@2.6.0/dist/tiptap-extension-table.umd.min.js
wget -O $WEB_DIR/tiptap-extension-task-list.js https://cdn.jsdelivr.net/npm/@tiptap/extension-task-list@2.6.0/dist/tiptap-extension-task-list.umd.min.js
wget -O $WEB_DIR/tiptap-extension-task-item.js https://cdn.jsdelivr.net/npm/@tiptap/extension-task-item@2.6.0/dist/tiptap-extension-task-item.umd.min.js
wget -O $WEB_DIR/tiptap-extension-highlight.js https://cdn.jsdelivr.net/npm/@tiptap/extension-highlight@2.6.0/dist/tiptap-extension-highlight.umd.min.js
wget -O $WEB_DIR/tiptap-extension-image.js https://cdn.jsdelivr.net/npm/@tiptap/extension-image@2.6.0/dist/tiptap-extension-image.umd.min.js
wget -O $WEB_DIR/tiptap-extension-link.js https://cdn.jsdelivr.net/npm/@tiptap/extension-link@2.6.0/dist/tiptap-extension-link.umd.min.js
wget -O $WEB_DIR/tiptap-extension-placeholder.js https://cdn.jsdelivr.net/npm/@tiptap/extension-placeholder@2.6.0/dist/tiptap-extension-placeholder.umd.min.js
wget -O $WEB_DIR/tiptap-extension-underline.js https://cdn.jsdelivr.net/npm/@tiptap/extension-underline@2.6.0/dist/tiptap-extension-underline.umd.min.js
wget -O $WEB_DIR/tiptap-extension-code-block-lowlight.js https://cdn.jsdelivr.net/npm/@tiptap/extension-code-block-lowlight@2.6.0/dist/tiptap-extension-code-block-lowlight.umd.min.js

# ProseMirror 依赖（TipTap 底层）
wget -O $WEB_DIR/prosemirror-state.js https://cdn.jsdelivr.net/npm/prosemirror-state@1.4.3/dist/index.umd.min.js
wget -O $WEB_DIR/prosemirror-view.js https://cdn.jsdelivr.net/npm/prosemirror-view@1.33.0/dist/index.umd.min.js
wget -O $WEB_DIR/prosemirror-model.js https://cdn.jsdelivr.net/npm/prosemirror-model@1.21.0/dist/index.umd.min.js
wget -O $WEB_DIR/prosemirror-transform.js https://cdn.jsdelivr.net/npm/prosemirror-transform@1.9.0/dist/index.umd.min.js

# 语法高亮：lowlight + highlight.js
wget -O $WEB_DIR/lowlight.js https://cdn.jsdelivr.net/npm/lowlight@3.1.0/lib/common.js
wget -O $WEB_DIR/highlight.min.js https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js
```

- [ ] **步骤 3：编写编辑器 HTML 页面**

`src/resources/web/editor.html`:
```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Markdown Editor</title>
<link rel="stylesheet" href="editor.css">
</head>
<body>
<div id="editor"></div>

<!-- ProseMirror 底层依赖 -->
<script src="prosemirror-state.js"></script>
<script src="prosemirror-view.js"></script>
<script src="prosemirror-model.js"></script>
<script src="prosemirror-transform.js"></script>

<!-- TipTap 核心与扩展 -->
<script src="tiptap-core.js"></script>
<script src="tiptap-starter-kit.js"></script>
<script src="tiptap-extension-table.js"></script>
<script src="tiptap-extension-task-list.js"></script>
<script src="tiptap-extension-task-item.js"></script>
<script src="tiptap-extension-highlight.js"></script>
<script src="tiptap-extension-image.js"></script>
<script src="tiptap-extension-link.js"></script>
<script src="tiptap-extension-placeholder.js"></script>
<script src="tiptap-extension-underline.js"></script>
<script src="tiptap-extension-code-block-lowlight.js"></script>

<!-- 语法高亮 -->
<script src="highlight.min.js"></script>
<script src="lowlight.js"></script>

<!-- Qt WebChannel -->
<script src="qwebchannel.js"></script>

<!-- 编辑器初始化 -->
<script src="editor.js"></script>
</body>
</html>
```

- [ ] **步骤 4：编写编辑器 CSS**

`src/resources/web/editor.css`:
```css
*, *::before, *::after {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
}

body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
                 "Helvetica Neue", Arial, "Noto Sans SC", sans-serif;
    font-size: 16px;
    line-height: 1.7;
    color: #333;
    background-color: #ffffff;
    overflow: hidden;
}

body.dark {
    color: #d4d4d4;
    background-color: #1e1e1e;
}

#editor {
    max-width: 860px;
    margin: 0 auto;
    padding: 40px 60px;
    min-height: 100vh;
    outline: none;
}

/* TipTap 编辑器样式 */
.ProseMirror {
    outline: none;
    min-height: 100vh;
}

.ProseMirror > * + * {
    margin-top: 0.75em;
}

/* 标题 */
.ProseMirror h1 { font-size: 2em; font-weight: 700; margin-top: 1.5em; margin-bottom: 0.5em; }
.ProseMirror h2 { font-size: 1.5em; font-weight: 700; margin-top: 1.4em; margin-bottom: 0.4em; }
.ProseMirror h3 { font-size: 1.25em; font-weight: 600; margin-top: 1.3em; margin-bottom: 0.3em; }
.ProseMirror h4 { font-size: 1.1em; font-weight: 600; }
.ProseMirror h5 { font-size: 1em; font-weight: 600; }
.ProseMirror h6 { font-size: 0.9em; font-weight: 600; color: #666; }

body.dark .ProseMirror h6 { color: #999; }

/* 代码 */
.ProseMirror code {
    background-color: #f0f0f0;
    border-radius: 3px;
    padding: 2px 6px;
    font-family: "Cascadia Code", "Fira Code", "JetBrains Mono", monospace;
    font-size: 0.9em;
}

body.dark .ProseMirror code {
    background-color: #2d2d2d;
    color: #e0e0e0;
}

.ProseMirror pre {
    background-color: #f7f7f7;
    border-radius: 8px;
    padding: 16px;
    overflow-x: auto;
    margin: 1em 0;
}

body.dark .ProseMirror pre {
    background-color: #2d2d2d;
}

.ProseMirror pre code {
    background: none;
    padding: 0;
    font-size: 0.9em;
}

/* 引用块 */
.ProseMirror blockquote {
    border-left: 3px solid #0078d4;
    padding-left: 16px;
    color: #666;
    font-style: italic;
    margin: 1em 0;
}

body.dark .ProseMirror blockquote {
    color: #999;
    border-left-color: #0078d4;
}

/* 表格 */
.ProseMirror table {
    border-collapse: collapse;
    width: 100%;
    margin: 1em 0;
}

.ProseMirror th, .ProseMirror td {
    border: 1px solid #ddd;
    padding: 8px 12px;
    text-align: left;
}

body.dark .ProseMirror th, body.dark .ProseMirror td {
    border-color: #444;
}

.ProseMirror th {
    background-color: #f5f5f5;
    font-weight: 600;
}

body.dark .ProseMirror th {
    background-color: #333;
}

/* 链接 */
.ProseMirror a {
    color: #0078d4;
    text-decoration: underline;
    cursor: pointer;
}

/* 图片 */
.ProseMirror img {
    max-width: 100%;
    height: auto;
    border-radius: 4px;
}

/* 任务列表 */
.ProseMirror ul[data-type="taskList"] {
    list-style: none;
    padding-left: 0;
}

.ProseMirror ul[data-type="taskList"] li {
    display: flex;
    align-items: flex-start;
    gap: 8px;
}

.ProseMirror ul[data-type="taskList"] li label {
    margin-top: 3px;
}

/* 分割线 */
.ProseMirror hr {
    border: none;
    border-top: 2px solid #e0e0e0;
    margin: 2em 0;
}

body.dark .ProseMirror hr {
    border-top-color: #3d3d3d;
}

/* 删除线 */
.ProseMirror s {
    color: #999;
}

/* 高亮 */
.ProseMirror mark {
    background-color: #fff3cd;
    border-radius: 2px;
    padding: 1px 3px;
}

body.dark .ProseMirror mark {
    background-color: #5a4a00;
    color: #ffe066;
}

/* 占位文本 */
.ProseMirror p.is-editor-empty:first-child::before {
    content: attr(data-placeholder);
    color: #aaa;
    pointer-events: none;
    float: left;
    height: 0;
}

/* 光标所在行高亮 */
.ProseMirror .ProseMirror-gapcursor:after {
    border-top: 2px solid #0078d4;
}
```

- [ ] **步骤 5：编写编辑器 JS 初始化代码**

`src/resources/web/editor.js`:
```javascript
// 全局编辑器 API 对象，供 C++ 通过 QWebChannel 或 runJavaScript 调用
window.editorApi = {
    editor: null,
    isSourceMode: false,
    sourceContent: '',
    bridge: null,

    // 初始化编辑器
    init: function() {
        const { Editor } = window.TipTap;
        const StarterKit = window.TipTapStarterKit;
        const Table = window.TipTapExtensionTable;
        const TaskList = window.TipTapExtensionTaskList;
        const TaskItem = window.TipTapExtensionTaskItem;
        const Highlight = window.TipTapExtensionHighlight;
        const Image = window.TipTapExtensionImage;
        const Link = window.TipTapExtensionLink;
        const Placeholder = window.TipTapExtensionPlaceholder;
        const Underline = window.TipTapExtensionUnderline;

        this.editor = new Editor({
            element: document.querySelector('#editor'),
            extensions: [
                StarterKit.configure({
                    codeBlock: false, // 用 lowlight 版本替代
                    heading: { levels: [1, 2, 3, 4, 5, 6] }
                }),
                Table.configure({ resizable: true }),
                TaskList,
                TaskItem.configure({ nested: true }),
                Highlight,
                Image.configure({ allowBase64: true }),
                Link.configure({ openOnClick: false }),
                Placeholder.configure({ placeholder: '开始编写 Markdown...' }),
                Underline,
            ],
            content: '',
            onUpdate: ({ editor }) => {
                this.notifyContentChanged(editor);
            },
            onSelectionUpdate: ({ editor }) => {
                this.notifyCursorChanged(editor);
            },
        });

        this.notifyHeadingsChanged();
        this.editor.on('update', () => { this.notifyHeadingsChanged(); });
    },

    // 通知 C++ 侧内容变更
    notifyContentChanged: function(editor) {
        if (this.bridge) {
            this.bridge.contentChanged(editor.isDirty);
        }
    },

    // 通知 C++ 侧光标位置变更
    notifyCursorChanged: function(editor) {
        if (this.bridge) {
            const { doc, selection } = editor.state;
            const pos = selection.head;
            let line = 1, col = 1;
            doc.nodesBetween(0, pos, (node, start) => {
                if (start + node.nodeSize <= pos) {
                    const lines = node.textContent.slice(0, pos - start).split('\n');
                    line += lines.length - 1;
                    col = lines[lines.length - 1].length + 1;
                }
            });
            this.bridge.cursorChanged(line, col);
        }
    },

    // 通知 C++ 侧标题结构变更
    notifyHeadingsChanged: function() {
        if (!this.editor || !this.bridge) return;
        const headings = [];
        this.editor.state.doc.descendants((node, pos) => {
            if (node.type.name === 'heading') {
                headings.push({
                    level: node.attrs.level,
                    text: node.textContent,
                    pos: pos
                });
            }
        });
        this.bridge.headingsChanged(JSON.stringify(headings));
    },

    // === C++ 侧调用的命令 ===

    getContent: function() {
        return this.editor ? this.editor.getHTML() : '';
    },

    getMarkdown: function() {
        // TipTap 不直接支持 Markdown 导出，后续安装 Markdown 扩展
        // 暂用 HTML
        return this.editor ? this.editor.getHTML() : '';
    },

    setContent: function(html) {
        if (this.editor) {
            this.editor.commands.setContent(html);
        }
    },

    setMarkdown: function(md) {
        // 后续安装 Markdown 扩展后实现真正的 MD→HTML 解析
        // 现阶段先设置原始内容
        if (this.editor) {
            this.editor.commands.setContent(md);
        }
    },

    toggleBold: function() {
        if (this.editor) this.editor.commands.toggleBold();
    },

    toggleItalic: function() {
        if (this.editor) this.editor.commands.toggleItalic();
    },

    toggleStrike: function() {
        if (this.editor) this.editor.commands.toggleStrike();
    },

    toggleCode: function() {
        if (this.editor) this.editor.commands.toggleCode();
    },

    toggleUnderline: function() {
        if (this.editor) this.editor.commands.toggleUnderline();
    },

    toggleHighlight: function() {
        if (this.editor) this.editor.commands.toggleHighlight();
    },

    setHeading: function(level) {
        if (this.editor) {
            if (level === 0) {
                this.editor.commands.setParagraph();
            } else {
                this.editor.commands.toggleHeading({ level: level });
            }
        }
    },

    toggleBulletList: function() {
        if (this.editor) this.editor.commands.toggleBulletList();
    },

    toggleOrderedList: function() {
        if (this.editor) this.editor.commands.toggleOrderedList();
    },

    toggleTaskList: function() {
        if (this.editor) this.editor.commands.toggleTaskList();
    },

    toggleBlockquote: function() {
        if (this.editor) this.editor.commands.toggleBlockquote();
    },

    insertTable: function(rows, cols) {
        if (this.editor) {
            this.editor.commands.insertTable({
                rows: rows || 3,
                cols: cols || 3,
                withHeaderRow: true
            });
        }
    },

    insertLink: function(url, text) {
        if (this.editor) {
            if (url) {
                this.editor.commands.setLink({ href: url });
            } else {
                // 如果未提供 URL，提示用户
                const u = prompt('输入链接地址:');
                if (u) {
                    this.editor.commands.setLink({ href: u });
                }
            }
        }
    },

    insertImage: function(path) {
        if (this.editor) {
            this.editor.commands.setImage({ src: path });
        }
    },

    insertHorizontalRule: function() {
        if (this.editor) this.editor.commands.setHorizontalRule();
    },

    toggleSourceMode: function() {
        // TODO: 后续集成 Markdown 扩展后实现真正的源码切换
        // 现阶段简单实现
        if (this.editor) {
            const content = this.editor.getHTML();
            this.sourceContent = content;
            console.log('Source mode toggle - content:', content);
        }
    },

    scrollToPos: function(pos) {
        if (this.editor) {
            this.editor.commands.setTextSelection(pos);
        }
    },

    undo: function() {
        if (this.editor) this.editor.commands.undo();
    },

    redo: function() {
        if (this.editor) this.editor.commands.redo();
    },

    setTheme: function(theme) {
        if (theme === 'dark') {
            document.body.classList.add('dark');
        } else {
            document.body.classList.remove('dark');
        }
    },
};

// QWebChannel 初始化
document.addEventListener('DOMContentLoaded', function() {
    window.editorApi.init();

    // 尝试通过 QWebChannel 连接 C++ 后端
    if (typeof QWebChannel !== 'undefined') {
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.editorApi.bridge = channel.objects.bridge;
        });
    }
});
```

- [ ] **步骤 6：更新 app.qrc 添加 Web 资源**

修改 `src/resources/app.qrc`，在 `<qresource prefix="/">` 内添加所有 web 文件引用：
```xml
<file>web/editor.html</file>
<file>web/editor.css</file>
<file>web/editor.js</file>
<file>web/qwebchannel.js</file>
<file>web/tiptap-core.js</file>
<file>web/tiptap-starter-kit.js</file>
<file>web/tiptap-extension-table.js</file>
<file>web/tiptap-extension-task-list.js</file>
<file>web/tiptap-extension-task-item.js</file>
<file>web/tiptap-extension-highlight.js</file>
<file>web/tiptap-extension-image.js</file>
<file>web/tiptap-extension-link.js</file>
<file>web/tiptap-extension-placeholder.js</file>
<file>web/tiptap-extension-underline.js</file>
<file>web/tiptap-extension-code-block-lowlight.js</file>
<file>web/prosemirror-state.js</file>
<file>web/prosemirror-view.js</file>
<file>web/prosemirror-model.js</file>
<file>web/prosemirror-transform.js</file>
<file>web/lowlight.js</file>
<file>web/highlight.min.js</file>
```

- [ ] **步骤 7：构建并验证资源打包**

```bash
cd build && cmake .. && make -j$(nproc)
```

预期：编译成功，qrc 资源被正确打包。可能因文件缺失编译失败，根据报错调整实际下载的文件名。

- [ ] **步骤 8：使用独立浏览器测试编辑器页面**

用 Chromium 或 Firefox 打开 `src/resources/web/editor.html`，确认：
- 页面正常渲染
- 可以在编辑区输入文字
- 加粗、斜体等快捷键（Ctrl+B/I）可用
- 控制台无 JS 错误（除 QWebChannel 未连接的预期警告外）

- [ ] **步骤 9：提交**

```bash
git add src/resources/web/ src/resources/app.qrc
git commit -m "feat: 添加 TipTap Web 编辑器前端页面"
```

---

### 任务 4：EditorBridge — C++/JS 通信核心

**文件：**
- 创建：`src/core/EditorBridge.h`、`src/core/EditorBridge.cpp`
- 修改：`src/CMakeLists.txt`（添加源文件）

**接口：**
- 产出：`EditorBridge` 类，继承 `QObject`，通过 `QWebChannel` 暴露给 JS
- 产出：以下 QWebChannel 槽方法供 JS 调用：
  - `cursorChanged(int line, int col)`
  - `contentChanged(bool dirty)`
  - `headingsChanged(const QString &json)`
  - `saveFileRequested(const QString &markdown)`
- 产出：以下信号供 C++ UI 组件使用：
  - `cursorPositionChanged(int line, int col)`
  - `dirtyStateChanged(bool dirty, int tabId)`
  - `headingsUpdated(QJsonArray headings)`
  - `saveRequested(const QString &markdown, int tabId)`
- 消耗：无（独立核心组件）

- [ ] **步骤 1：编写 EditorBridge 头文件**

`src/core/EditorBridge.h`:
```cpp
#ifndef MD_EDITOR_EDITORBRIDGE_H
#define MD_EDITOR_EDITORBRIDGE_H

#include <QObject>
#include <QJsonArray>

class EditorBridge : public QObject {
    Q_OBJECT
public:
    explicit EditorBridge(int tabId, QObject *parent = nullptr);
    int tabId() const;

public slots:
    // JS → C++：状态同步
    void cursorChanged(int line, int col);
    void contentChanged(bool dirty);
    void headingsChanged(const QString &json);

signals:
    // 通知 C++ UI 组件
    void cursorPositionChanged(int line, int col, int tabId);
    void dirtyStateChanged(bool dirty, int tabId);
    void headingsUpdated(const QJsonArray &headings, int tabId);
    void saveRequested(const QString &markdown, int tabId);

private:
    int m_tabId;
};

#endif // MD_EDITOR_EDITORBRIDGE_H
```

- [ ] **步骤 2：编写 EditorBridge 实现**

`src/core/EditorBridge.cpp`:
```cpp
#include "EditorBridge.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

EditorBridge::EditorBridge(int tabId, QObject *parent)
    : QObject(parent)
    , m_tabId(tabId)
{
}

int EditorBridge::tabId() const
{
    return m_tabId;
}

void EditorBridge::cursorChanged(int line, int col)
{
    emit cursorPositionChanged(line, col, m_tabId);
}

void EditorBridge::contentChanged(bool dirty)
{
    emit dirtyStateChanged(dirty, m_tabId);
}

void EditorBridge::headingsChanged(const QString &json)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && doc.isArray()) {
        emit headingsUpdated(doc.array(), m_tabId);
    }
}
```

- [ ] **步骤 3：更新 CMakeLists.txt**

修改 `src/CMakeLists.txt`，在 SOURCES 中添加 `core/EditorBridge.cpp`，在 HEADERS 中添加 `core/EditorBridge.h`。

- [ ] **步骤 4：编写单元测试**

`tests/test_editorbridge.cpp`:
```cpp
#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include "../src/core/EditorBridge.h"

class TestEditorBridge : public QObject {
    Q_OBJECT
private slots:
    void testCursorChanged() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::cursorPositionChanged);

        bridge.cursorChanged(10, 5);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toInt(), 10);  // line
        QCOMPARE(spy[0][1].toInt(), 5);   // col
        QCOMPARE(spy[0][2].toInt(), 1);   // tabId
    }

    void testContentChanged() {
        EditorBridge bridge(2);
        QSignalSpy spy(&bridge, &EditorBridge::dirtyStateChanged);

        bridge.contentChanged(true);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toBool(), true);
        QCOMPARE(spy[0][1].toInt(), 2);
    }

    void testHeadingsChanged() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::headingsUpdated);

        QString json = R"([{"level":1,"text":"标题一","pos":0},{"level":2,"text":"小节","pos":10}])";
        bridge.headingsChanged(json);

        QCOMPARE(spy.count(), 1);
        QJsonArray arr = spy[0][0].value<QJsonArray>();
        QCOMPARE(arr.size(), 2);
        QCOMPARE(arr[0].toObject()["text"].toString(), QString("标题一"));
    }

    void testInvalidJson() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::headingsUpdated);

        bridge.headingsChanged("not valid json");

        QCOMPARE(spy.count(), 0);  // 无效 JSON 不触发信号
    }
};

QTEST_MAIN(TestEditorBridge)
#include "test_editorbridge.moc"
```

- [ ] **步骤 5：创建 tests/CMakeLists.txt**

```cmake
find_package(Qt6 REQUIRED COMPONENTS Test)

enable_testing()

add_executable(test_editorbridge test_editorbridge.cpp ../src/core/EditorBridge.cpp)
target_include_directories(test_editorbridge PRIVATE ../src)
target_link_libraries(test_editorbridge PRIVATE Qt6::Test)
add_test(NAME test_editorbridge COMMAND test_editorbridge)
```

- [ ] **步骤 6：修改顶层 CMakeLists.txt 添加 tests 子目录**

在顶层 `CMakeLists.txt` 末尾添加：
```cmake
option(BUILD_TESTS "构建测试" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **步骤 7：运行测试验证通过**

```bash
cd build && cmake .. && make -j$(nproc) && ctest --output-on-failure
```

预期：所有测试通过。

- [ ] **步骤 8：提交**

```bash
git add src/core/EditorBridge.* src/CMakeLists.txt tests/ CMakeLists.txt
git commit -m "feat: 添加 EditorBridge C++/JS 通信核心及单元测试"
```

---

### 任务 5：EditorTab — WebEngineView 编辑器标签页

**文件：**
- 创建：`src/ui/EditorTab.h`、`src/ui/EditorTab.cpp`
- 修改：`src/CMakeLists.txt`

**接口：**
- 产出：`EditorTab` 类，继承 `QWidget`，封装 `QWebEngineView` 并加载编辑器页面
- 产出：`bridge()` 方法返回该标签页的 `EditorBridge`
- 产出：`runJsCommand(command, args)` 方法封装 JS 调用
- 消耗：`EditorBridge`、Web 编辑器页面（qrc 资源）

- [ ] **步骤 1：编写 EditorTab 头文件**

`src/ui/EditorTab.h`:
```cpp
#ifndef MD_EDITOR_EDITORTAB_H
#define MD_EDITOR_EDITORTAB_H

#include <QWidget>
#include <QWebEngineView>
#include <QWebChannel>
#include "../core/EditorBridge.h"

class EditorTab : public QWidget {
    Q_OBJECT
public:
    explicit EditorTab(int tabId, QWidget *parent = nullptr);

    EditorBridge *bridge() const;
    QWebEngineView *webView() const;

    void loadFile(const QString &path, const QString &content);
    void setEditorTheme(const QString &theme);

    // 便捷 JS 命令调用
    void runJsCommand(const QString &js);

signals:
    void pageLoadFinished(bool ok);

private slots:
    void onPageLoadFinished(bool ok);

private:
    void setupWebChannel();
    int m_tabId;
    QWebEngineView *m_webView;
    QWebChannel *m_webChannel;
    EditorBridge *m_bridge;
    bool m_pageLoaded;
};

#endif // MD_EDITOR_EDITORTAB_H
```

- [ ] **步骤 2：编写 EditorTab 实现**

`src/ui/EditorTab.cpp`:
```cpp
#include "EditorTab.h"
#include <QVBoxLayout>
#include <QWebEngineSettings>
#include <QWebEnginePage>
#include <QUrl>

EditorTab::EditorTab(int tabId, QWidget *parent)
    : QWidget(parent)
    , m_tabId(tabId)
    , m_webView(new QWebEngineView(this))
    , m_webChannel(new QWebChannel(this))
    , m_bridge(new EditorBridge(tabId, this))
    , m_pageLoaded(false)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_webView);

    // 配置 WebEngine 设置
    QWebEngineSettings *settings = m_webView->page()->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);

    setupWebChannel();

    connect(m_webView, &QWebEngineView::loadFinished,
            this, &EditorTab::onPageLoadFinished);

    // 加载编辑器页面
    m_webView->load(QUrl("qrc:/web/editor.html"));
}

EditorBridge *EditorTab::bridge() const
{
    return m_bridge;
}

QWebEngineView *EditorTab::webView() const
{
    return m_webView;
}

void EditorTab::loadFile(const QString &path, const QString &content)
{
    Q_UNUSED(path)
    if (m_pageLoaded) {
        // 转义内容中的特殊字符，避免 JS 语法错误
        QString escaped = content;
        escaped.replace("\\", "\\\\");
        escaped.replace("`", "\\`");
        escaped.replace("$", "\\$");
        QString js = QString("window.editorApi.setContent(`%1`);").arg(escaped);
        runJsCommand(js);
    }
}

void EditorTab::setEditorTheme(const QString &theme)
{
    if (m_pageLoaded) {
        QString js = QString("window.editorApi.setTheme('%1');").arg(theme);
        runJsCommand(js);
    }
}

void EditorTab::runJsCommand(const QString &js)
{
    m_webView->page()->runJavaScript(js);
}

void EditorTab::setupWebChannel()
{
    m_webChannel->registerObject("bridge", m_bridge);
    m_webView->page()->setWebChannel(m_webChannel);
}

void EditorTab::onPageLoadFinished(bool ok)
{
    m_pageLoaded = ok;
    if (ok) {
        // 页面加载完成后初始化 QWebChannel 连接
        runJsCommand(R"(
            if (typeof QWebChannel !== 'undefined') {
                new QWebChannel(qt.webChannelTransport, function(channel) {
                    window.editorApi.bridge = channel.objects.bridge;
                });
            }
        )");
    }
    emit pageLoadFinished(ok);
}
```

- [ ] **步骤 3：更新 main.cpp 测试 EditorTab**

临时修改 `src/main.cpp`：
```cpp
#include "app/Application.h"
#include "ui/EditorTab.h"

int main(int argc, char *argv[])
{
    Application app(argc, argv);

    EditorTab *tab = new EditorTab(1);
    tab->setWindowTitle("EditorTab 测试");
    tab->resize(1280, 800);
    tab->show();

    return app.exec();
}
```

- [ ] **步骤 4：更新 CMakeLists.txt**

在 `src/CMakeLists.txt` 的 SOURCES 和 HEADERS 中添加 `ui/EditorTab.cpp` 和 `ui/EditorTab.h`。

- [ ] **步骤 5：构建并测试**

```bash
cd build && cmake .. && make -j$(nproc) && ./md-editor
```

预期：窗口显示 TipTap 编辑器，可以在其中输入文字、使用加粗/斜体快捷键，页面加载正常无 JS 错误。

- [ ] **步骤 6：还原 main.cpp 并提交**

将 `main.cpp` 还原为使用 `QMainWindow` 的版本。

```bash
git add src/ui/EditorTab.* src/CMakeLists.txt src/main.cpp
git commit -m "feat: 添加 EditorTab 封装 QWebEngineView 加载 TipTap 编辑器"
```

---

### 任务 6：TabWidget — 多标签页管理

**文件：**
- 创建：`src/ui/TabWidget.h`、`src/ui/TabWidget.cpp`
- 修改：`src/CMakeLists.txt`

**接口：**
- 产出：`TabWidget` 类，继承 `QTabWidget`，管理多个 `EditorTab`
- 产出方法：
  - `EditorTab *openNewTab()` — 新建空白标签
  - `EditorTab *openFileTab(const QString &path, const QString &content)` — 打开文件
  - `EditorTab *currentEditorTab()` — 获取当前活动标签
  - `void closeTab(int index)` — 关闭标签
- 产出信号：
  - `currentTabChanged(int tabId)` — 当前标签切换
  - `tabCloseRequested(int index, bool hasUnsaved)` — 关闭请求（含未保存状态）
- 消耗：`EditorTab`、`EditorBridge`

- [ ] **步骤 1：编写 TabWidget 头文件**

`src/ui/TabWidget.h`:
```cpp
#ifndef MD_EDITOR_TABWIDGET_H
#define MD_EDITOR_TABWIDGET_H

#include <QTabWidget>
#include <QMap>

class EditorTab;
class EditorBridge;

class TabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit TabWidget(QWidget *parent = nullptr);

    EditorTab *openNewTab();
    EditorTab *openFileTab(const QString &path, const QString &content);
    EditorTab *currentEditorTab() const;
    EditorTab *tabAt(int index) const;
    int tabCount() const;

    void setTabTitle(int index, const QString &title);
    void setTabDirty(int index, bool dirty);
    void markTabSaved(int index, const QString &filePath);

signals:
    void currentTabChanged(int tabId);
    void editorBridgeCreated(EditorBridge *bridge);

private slots:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);

private:
    int m_nextTabId;
    QMap<int, EditorTab *> m_tabs; // tabId → EditorTab
    QString generateUntitledName();
};

#endif // MD_EDITOR_TABWIDGET_H
```

- [ ] **步骤 2：编写 TabWidget 实现**

`src/ui/TabWidget.cpp`:
```cpp
#include "TabWidget.h"
#include "EditorTab.h"
#include "../core/EditorBridge.h"
#include <QTabBar>
#include <QMessageBox>

TabWidget::TabWidget(QWidget *parent)
    : QTabWidget(parent)
    , m_nextTabId(1)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    connect(this, &QTabWidget::tabCloseRequested,
            this, &TabWidget::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged,
            this, &TabWidget::onCurrentChanged);
}

EditorTab *TabWidget::openNewTab()
{
    int tabId = m_nextTabId++;
    EditorTab *tab = new EditorTab(tabId, this);

    m_tabs.insert(tabId, tab);

    QString title = generateUntitledName();
    int index = addTab(tab, title);
    setCurrentIndex(index);

    // 监听脏状态变化
    connect(tab->bridge(), &EditorBridge::dirtyStateChanged, this,
            [this, tab](bool dirty, int /*tabId*/) {
                int idx = indexOf(tab);
                if (idx >= 0) {
                    setTabDirty(idx, dirty);
                }
            });

    emit editorBridgeCreated(tab->bridge());
    return tab;
}

EditorTab *TabWidget::openFileTab(const QString &path, const QString &content)
{
    int tabId = m_nextTabId++;
    EditorTab *tab = new EditorTab(tabId, this);

    m_tabs.insert(tabId, tab);

    QFileInfo fileInfo(path);
    int index = addTab(tab, fileInfo.fileName());
    setCurrentIndex(index);

    // 加载文件内容（延迟到页面加载完成后）
    connect(tab, &EditorTab::pageLoadFinished, this,
            [tab, path, content](bool ok) {
                if (ok) {
                    tab->loadFile(path, content);
                }
            });

    connect(tab->bridge(), &EditorBridge::dirtyStateChanged, this,
            [this, tab](bool dirty, int /*tabId*/) {
                int idx = indexOf(tab);
                if (idx >= 0) {
                    setTabDirty(idx, dirty);
                }
            });

    emit editorBridgeCreated(tab->bridge());
    return tab;
}

EditorTab *TabWidget::currentEditorTab() const
{
    return qobject_cast<EditorTab *>(currentWidget());
}

EditorTab *TabWidget::tabAt(int index) const
{
    return qobject_cast<EditorTab *>(widget(index));
}

void TabWidget::setTabTitle(int index, const QString &title)
{
    setTabText(index, title);
}

void TabWidget::setTabDirty(int index, bool dirty)
{
    QString title = tabText(index);
    if (dirty && !title.startsWith("● ")) {
        setTabText(index, "● " + title);
    } else if (!dirty && title.startsWith("● ")) {
        setTabText(index, title.mid(2));
    }
}

void TabWidget::markTabSaved(int index, const QString &filePath)
{
    QFileInfo fi(filePath);
    setTabText(index, fi.fileName());
    setTabToolTip(index, filePath);
}

void TabWidget::onTabCloseRequested(int index)
{
    EditorTab *tab = tabAt(index);
    if (!tab) return;

    // 检查是否有未保存内容
    bool isDirty = tabText(index).startsWith("● ");

    if (isDirty) {
        QMessageBox::StandardButton result = QMessageBox::question(
            this,
            "未保存的更改",
            QString("文件 '%1' 有未保存的更改。是否保存？").arg(tabText(index)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
        );

        if (result == QMessageBox::Cancel) return;
        if (result == QMessageBox::Save) {
            // TODO: 触发保存流程（后续集成 FileManager 后实现）
        }
    }

    // 移除标签页
    m_tabs.remove(tab->bridge()->tabId());
    removeTab(index);
    tab->deleteLater();
}

void TabWidget::onCurrentChanged(int index)
{
    if (index >= 0) {
        EditorTab *tab = tabAt(index);
        if (tab) {
            emit currentTabChanged(tab->bridge()->tabId());
        }
    }
}

QString TabWidget::generateUntitledName()
{
    int count = 1;
    QString name;
    do {
        name = QString("未命名-%1").arg(count++);
    } while (false); // 简化：总是生成新编号
    return name;
}
```

- [ ] **步骤 3：更新 CMakeLists.txt**

添加 `ui/TabWidget.cpp` 和 `ui/TabWidget.h`。

- [ ] **步骤 4：更新 main.cpp 测试 TabWidget**

临时修改 main.cpp 测试多标签功能。

- [ ] **步骤 5：构建并验证**

```bash
cd build && cmake .. && make -j$(nproc) && ./md-editor
```

预期：可创建多个标签页，标签可关闭和切换。

- [ ] **步骤 6：提交**

```bash
git add src/ui/TabWidget.* src/CMakeLists.txt
git commit -m "feat: 添加 TabWidget 多标签页管理"
```

---

### 任务 7：ToolBar — 图标工具栏

**文件：**
- 创建：`src/ui/ToolBar.h`、`src/ui/ToolBar.cpp`
- 修改：`src/CMakeLists.txt`

**接口：**
- 产出：`ToolBar` 类，继承 `QToolBar`，包含所有编辑按钮
- 产出信号：每个按钮操作对应一个信号（如 `boldClicked()`、`headingSelected(int level)` 等）
- 消耗：无（纯 UI 组件，信号连接到 EditorBridge 的 JS 调用由 MainWindow 编排）

- [ ] **步骤 1：编写 ToolBar 头文件**

`src/ui/ToolBar.h`:
```cpp
#ifndef MD_EDITOR_TOOLBAR_H
#define MD_EDITOR_TOOLBAR_H

#include <QToolBar>
#include <QComboBox>

class ToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);

signals:
    void boldClicked();
    void italicClicked();
    void strikethroughClicked();
    void codeClicked();
    void underlineClicked();
    void highlightClicked();
    void headingSelected(int level);
    void bulletListClicked();
    void orderedListClicked();
    void taskListClicked();
    void blockquoteClicked();
    void linkClicked();
    void imageClicked();
    void tableClicked();
    void horizontalRuleClicked();
    void sourceToggleClicked();
    void pdfExportClicked();

private:
    void setupUi();
    QComboBox *m_headingCombo;
};

#endif // MD_EDITOR_TOOLBAR_H
```

- [ ] **步骤 2：编写 ToolBar 实现**

`src/ui/ToolBar.cpp`:
```cpp
#include "ToolBar.h"
#include <QAction>
#include <QLabel>

ToolBar::ToolBar(QWidget *parent)
    : QToolBar(parent)
{
    setObjectName("mainToolBar");
    setMovable(false);
    setupUi();
}

void ToolBar::setupUi()
{
    // 标题下拉选择器
    addWidget(new QLabel(" 标题 "));
    m_headingCombo = new QComboBox(this);
    m_headingCombo->addItem("正文", 0);
    m_headingCombo->addItem("标题 1", 1);
    m_headingCombo->addItem("标题 2", 2);
    m_headingCombo->addItem("标题 3", 3);
    m_headingCombo->addItem("标题 4", 4);
    m_headingCombo->addItem("标题 5", 5);
    m_headingCombo->addItem("标题 6", 6);
    m_headingCombo->setFixedWidth(100);
    addWidget(m_headingCombo);

    connect(m_headingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                int level = m_headingCombo->itemData(index).toInt();
                emit headingSelected(level);
            });

    addSeparator();

    // 文本格式按钮
    QAction *boldAction = addAction("B");
    boldAction->setToolTip("加粗 (Ctrl+B)");
    boldAction->setCheckable(true);
    QFont boldFont = boldAction->font();
    boldFont.setBold(true);
    boldAction->setFont(boldFont);
    connect(boldAction, &QAction::triggered, this, &ToolBar::boldClicked);

    QAction *italicAction = addAction("I");
    italicAction->setToolTip("斜体 (Ctrl+I)");
    QFont italicFont = italicAction->font();
    italicFont.setItalic(true);
    italicAction->setFont(italicFont);
    connect(italicAction, &QAction::triggered, this, &ToolBar::italicClicked);

    QAction *strikeAction = addAction("S");
    strikeAction->setToolTip("删除线");
    QFont strikeFont = strikeAction->font();
    strikeFont.setStrikeOut(true);
    strikeAction->setFont(strikeFont);
    connect(strikeAction, &QAction::triggered, this, &ToolBar::strikethroughClicked);

    QAction *codeAction = addAction("<>");
    codeAction->setToolTip("行内代码");
    connect(codeAction, &QAction::triggered, this, &ToolBar::codeClicked);

    QAction *underlineAction = addAction("U");
    underlineAction->setToolTip("下划线");
    QFont ulFont = underlineAction->font();
    ulFont.setUnderline(true);
    underlineAction->setFont(ulFont);
    connect(underlineAction, &QAction::triggered, this, &ToolBar::underlineClicked);

    QAction *highlightAction = addAction("马克");
    highlightAction->setToolTip("高亮");
    connect(highlightAction, &QAction::triggered, this, &ToolBar::highlightClicked);

    addSeparator();

    // 结构按钮
    QAction *ulAction = addAction("☰");
    ulAction->setToolTip("无序列表");
    connect(ulAction, &QAction::triggered, this, &ToolBar::bulletListClicked);

    QAction *olAction = addAction("1.");
    olAction->setToolTip("有序列表");
    connect(olAction, &QAction::triggered, this, &ToolBar::orderedListClicked);

    QAction *taskAction = addAction("☑");
    taskAction->setToolTip("任务列表");
    connect(taskAction, &QAction::triggered, this, &ToolBar::taskListClicked);

    QAction *quoteAction = addAction("\"");
    quoteAction->setToolTip("引用");
    connect(quoteAction, &QAction::triggered, this, &ToolBar::blockquoteClicked);

    addSeparator();

    // 插入按钮
    QAction *linkAction = addAction("🔗");
    linkAction->setToolTip("插入链接");
    connect(linkAction, &QAction::triggered, this, &ToolBar::linkClicked);

    QAction *imageAction = addAction("🖼");
    imageAction->setToolTip("插入图片");
    connect(imageAction, &QAction::triggered, this, &ToolBar::imageClicked);

    QAction *tableAction = addAction("⊞");
    tableAction->setToolTip("插入表格");
    connect(tableAction, &QAction::triggered, this, &ToolBar::tableClicked);

    QAction *hrAction = addAction("—");
    hrAction->setToolTip("分割线");
    connect(hrAction, &QAction::triggered, this, &ToolBar::horizontalRuleClicked);

    addSeparator();

    // 视图按钮
    QAction *sourceAction = addAction("<>");
    sourceAction->setToolTip("切换源码模式");
    connect(sourceAction, &QAction::triggered, this, &ToolBar::sourceToggleClicked);

    addSeparator();

    // 导出按钮
    QAction *pdfAction = addAction("📄");
    pdfAction->setToolTip("导出 PDF");
    connect(pdfAction, &QAction::triggered, this, &ToolBar::pdfExportClicked);
}
```

- [ ] **步骤 3：更新 CMakeLists.txt**

添加 `ui/ToolBar.cpp` 和 `ui/ToolBar.h`。

- [ ] **步骤 4：构建验证编译通过**

```bash
cd build && cmake .. && make -j$(nproc)
```

- [ ] **步骤 5：提交**

```bash
git add src/ui/ToolBar.* src/CMakeLists.txt
git commit -m "feat: 添加图标工具栏，支持标题/文本/结构/插入/视图/导出按钮"
```

---

### 任务 8：MenuBar — 菜单栏

**文件：**
- 创建：`src/ui/MenuBar.h`、`src/ui/MenuBar.cpp`
- 修改：`src/CMakeLists.txt`

**接口：**
- 产出：`MenuBar` 类，继承 `QMenuBar`，创建完整的菜单结构
- 产出信号：与 ToolBar 模式相同（文件/编辑/视图/插入/格式 各组信号）
- 消耗：无（纯 UI 组件）

- [ ] **步骤 1：编写 MenuBar 头文件**

`src/ui/MenuBar.h`:
```cpp
#ifndef MD_EDITOR_MENUBAR_H
#define MD_EDITOR_MENUBAR_H

#include <QMenuBar>

class MenuBar : public QMenuBar {
    Q_OBJECT
public:
    explicit MenuBar(QWidget *parent = nullptr);

signals:
    void newFile();
    void openFile();
    void openFolder();
    void saveFile();
    void saveFileAs();
    void closeTab();
    void exportPdf();
    void quit();

    void undo();
    void redo();

    void toggleSidebar();
    void toggleOutline();
    void toggleSourceMode();
    void toggleFullscreen();

    void insertLink();
    void insertImage();
    void insertTable();
    void insertHorizontalRule();

    void switchTheme(const QString &name);

    void showAbout();

private:
    void setupUi();
    QMenu *m_fileMenu;
    QMenu *m_editMenu;
    QMenu *m_viewMenu;
    QMenu *m_insertMenu;
    QMenu *m_formatMenu;
    QMenu *m_helpMenu;
};

#endif // MD_EDITOR_MENUBAR_H
```

- [ ] **步骤 2：编写 MenuBar 实现**

`src/ui/MenuBar.cpp`:
```cpp
#include "MenuBar.h"
#include <QAction>
#include <QKeySequence>

MenuBar::MenuBar(QWidget *parent)
    : QMenuBar(parent)
{
    setupUi();
}

void MenuBar::setupUi()
{
    // === 文件菜单 ===
    m_fileMenu = addMenu("文件(&F)");

    QAction *newAct = m_fileMenu->addAction("新建(&N)");
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &MenuBar::newFile);

    QAction *openAct = m_fileMenu->addAction("打开文件(&O)...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MenuBar::openFile);

    QAction *openFolderAct = m_fileMenu->addAction("打开文件夹...");
    openFolderAct->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(openFolderAct, &QAction::triggered, this, &MenuBar::openFolder);

    m_fileMenu->addSeparator();

    QAction *saveAct = m_fileMenu->addAction("保存(&S)");
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MenuBar::saveFile);

    QAction *saveAsAct = m_fileMenu->addAction("另存为...");
    saveAsAct->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveAsAct, &QAction::triggered, this, &MenuBar::saveFileAs);

    m_fileMenu->addSeparator();

    QAction *exportAct = m_fileMenu->addAction("导出 PDF...");
    exportAct->setShortcut(QKeySequence("Ctrl+P"));
    connect(exportAct, &QAction::triggered, this, &MenuBar::exportPdf);

    m_fileMenu->addSeparator();

    QAction *quitAct = m_fileMenu->addAction("退出(&Q)");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &MenuBar::quit);

    // === 编辑菜单 ===
    m_editMenu = addMenu("编辑(&E)");

    QAction *undoAct = m_editMenu->addAction("撤销(&U)");
    undoAct->setShortcut(QKeySequence::Undo);
    connect(undoAct, &QAction::triggered, this, &MenuBar::undo);

    QAction *redoAct = m_editMenu->addAction("重做(&R)");
    redoAct->setShortcut(QKeySequence::Redo);
    connect(redoAct, &QAction::triggered, this, &MenuBar::redo);

    // === 视图菜单 ===
    m_viewMenu = addMenu("视图(&V)");

    QAction *sidebarAct = m_viewMenu->addAction("侧边栏");
    sidebarAct->setShortcut(QKeySequence("Ctrl+B"));
    sidebarAct->setCheckable(true);
    sidebarAct->setChecked(true);
    connect(sidebarAct, &QAction::triggered, this, &MenuBar::toggleSidebar);

    QAction *outlineAct = m_viewMenu->addAction("大纲");
    outlineAct->setCheckable(true);
    outlineAct->setChecked(true);
    connect(outlineAct, &QAction::triggered, this, &MenuBar::toggleOutline);

    m_viewMenu->addSeparator();

    QAction *sourceAct = m_viewMenu->addAction("源码模式");
    sourceAct->setShortcut(QKeySequence("Ctrl+\\"));
    connect(sourceAct, &QAction::triggered, this, &MenuBar::toggleSourceMode);

    QAction *fullscreenAct = m_viewMenu->addAction("全屏");
    fullscreenAct->setShortcut(QKeySequence("F11"));
    connect(fullscreenAct, &QAction::triggered, this, &MenuBar::toggleFullscreen);

    m_viewMenu->addSeparator();

    QMenu *themeMenu = m_viewMenu->addMenu("主题");
    QAction *lightAct = themeMenu->addAction("亮色");
    connect(lightAct, &QAction::triggered, this, [this]() { emit switchTheme("light"); });
    QAction *darkAct = themeMenu->addAction("暗色");
    connect(darkAct, &QAction::triggered, this, [this]() { emit switchTheme("dark"); });

    // === 插入菜单 ===
    m_insertMenu = addMenu("插入(&I)");

    QAction *linkAct = m_insertMenu->addAction("链接...");
    linkAct->setShortcut(QKeySequence("Ctrl+K"));
    connect(linkAct, &QAction::triggered, this, &MenuBar::insertLink);

    QAction *imageAct = m_insertMenu->addAction("图片...");
    imageAct->setShortcut(QKeySequence("Ctrl+Shift+I"));
    connect(imageAct, &QAction::triggered, this, &MenuBar::insertImage);

    QAction *tableAct = m_insertMenu->addAction("表格...");
    connect(tableAct, &QAction::triggered, this, &MenuBar::insertTable);

    QAction *hrAct = m_insertMenu->addAction("分割线");
    connect(hrAct, &QAction::triggered, this, &MenuBar::insertHorizontalRule);

    // === 帮助菜单 ===
    m_helpMenu = addMenu("帮助(&H)");

    QAction *aboutAct = m_helpMenu->addAction("关于...");
    connect(aboutAct, &QAction::triggered, this, &MenuBar::showAbout);
}
```

- [ ] **步骤 3：更新 CMakeLists.txt**

添加 `ui/MenuBar.cpp` 和 `ui/MenuBar.h`。

- [ ] **步骤 4：构建验证编译通过**

```bash
cd build && cmake .. && make -j$(nproc)
```

- [ ] **步骤 5：提交**

```bash
git add src/ui/MenuBar.* src/CMakeLists.txt
git commit -m "feat: 添加菜单栏，覆盖文件/编辑/视图/插入/帮助菜单"
```

---

### 任务 9：侧边栏 — FileTree + OutlineTree

**文件：**
- 创建：`src/ui/SideBar.h`、`src/ui/SideBar.cpp`
- 创建：`src/ui/FileTree.h`、`src/ui/FileTree.cpp`
- 创建：`src/ui/OutlineTree.h`、`src/ui/OutlineTree.cpp`
- 修改：`src/CMakeLists.txt`

**接口：**
- 产出：`SideBar` — 左侧边栏容器，含 `QTabWidget` 切换文件树和大纲
- 产出：`FileTree` — 基于 `QTreeView + QFileSystemModel` 的文件浏览器
- 产出：`OutlineTree` — 基于 `QTreeView + QStandardItemModel` 的文档大纲
- 消耗：`FileTree` 独立；`OutlineTree` 消耗 `EditorBridge::headingsUpdated` 信号

- [ ] **步骤 1：编写 FileTree 头文件**

`src/ui/FileTree.h`:
```cpp
#ifndef MD_EDITOR_FILETREE_H
#define MD_EDITOR_FILETREE_H

#include <QTreeView>
#include <QFileSystemModel>

class FileTree : public QTreeView {
    Q_OBJECT
public:
    explicit FileTree(QWidget *parent = nullptr);
    void setRootPath(const QString &path);
    QString rootPath() const;

signals:
    void fileDoubleClicked(const QString &filePath);

private slots:
    void onDoubleClicked(const QModelIndex &index);
    void showContextMenu(const QPoint &pos);

private:
    QFileSystemModel *m_model;
    QString m_rootPath;
};

#endif // MD_EDITOR_FILETREE_H
```

- [ ] **步骤 2：编写 FileTree 实现**

`src/ui/FileTree.cpp`:
```cpp
#include "FileTree.h"
#include <QMenu>
#include <QProcess>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>

FileTree::FileTree(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QFileSystemModel(this))
{
    m_model->setNameFilters({"*.md", "*.markdown"});
    m_model->setNameFilterDisables(false); // 隐藏不匹配的文件
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    setModel(m_model);

    // 隐藏大小、类型等列，只显示名称
    setHeaderHidden(true);
    for (int i = 1; i < m_model->columnCount(); ++i) {
        hideColumn(i);
    }

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTreeView::doubleClicked, this, &FileTree::onDoubleClicked);
    connect(this, &QTreeView::customContextMenuRequested, this, &FileTree::showContextMenu);
}

void FileTree::setRootPath(const QString &path)
{
    m_rootPath = path;
    setRootIndex(m_model->setRootPath(path));
}

QString FileTree::rootPath() const
{
    return m_rootPath;
}

void FileTree::onDoubleClicked(const QModelIndex &index)
{
    QFileInfo fileInfo = m_model->fileInfo(index);
    if (fileInfo.isFile()) {
        emit fileDoubleClicked(fileInfo.absoluteFilePath());
    }
}

void FileTree::showContextMenu(const QPoint &pos)
{
    QModelIndex index = indexAt(pos);
    QMenu menu;

    QAction *newFileAct = menu.addAction("新建 Markdown 文件");
    menu.addSeparator();

    QAction *renameAct = nullptr;
    QAction *deleteAct = nullptr;
    if (index.isValid()) {
        renameAct = menu.addAction("重命名");
        deleteAct = menu.addAction("删除");
        menu.addSeparator();
    }

    QAction *openFolderAct = menu.addAction("在文件管理器中打开");

    QAction *selected = menu.exec(viewport()->mapToGlobal(pos));

    if (selected == newFileAct) {
        QString parentPath = m_rootPath;
        if (index.isValid()) {
            QFileInfo fi = m_model->fileInfo(index);
            parentPath = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        }
        QString name = QInputDialog::getText(this, "新建文件", "文件名:", QLineEdit::Normal, "untitled.md");
        if (!name.isEmpty()) {
            QString newPath = parentPath + "/" + name;
            QFile file(newPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
            }
        }
    } else if (selected == renameAct && renameAct) {
        QFileInfo fi = m_model->fileInfo(index);
        QString newName = QInputDialog::getText(this, "重命名", "新文件名:", QLineEdit::Normal, fi.fileName());
        if (!newName.isEmpty() && newName != fi.fileName()) {
            QString newPath = fi.absolutePath() + "/" + newName;
            QFile::rename(fi.absoluteFilePath(), newPath);
        }
    } else if (selected == deleteAct && deleteAct) {
        QFileInfo fi = m_model->fileInfo(index);
        QMessageBox::StandardButton r = QMessageBox::question(
            this, "确认删除",
            QString("确定要删除 '%1' 吗？").arg(fi.fileName())
        );
        if (r == QMessageBox::Yes) {
            if (fi.isDir()) {
                QDir(fi.absoluteFilePath()).removeRecursively();
            } else {
                QFile::remove(fi.absoluteFilePath());
            }
        }
    } else if (selected == openFolderAct) {
        QString path = m_rootPath;
        if (index.isValid()) {
            QFileInfo fi = m_model->fileInfo(index);
            path = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        }
        QProcess::startDetached("xdg-open", {path});
    }
}
```

- [ ] **步骤 3：编写 OutlineTree**

`src/ui/OutlineTree.h`:
```cpp
#ifndef MD_EDITOR_OUTLINETREE_H
#define MD_EDITOR_OUTLINETREE_H

#include <QTreeView>
#include <QStandardItemModel>
#include <QJsonArray>

class OutlineTree : public QTreeView {
    Q_OBJECT
public:
    explicit OutlineTree(QWidget *parent = nullptr);

public slots:
    void updateHeadings(const QJsonArray &headings);

signals:
    void headingClicked(int pos);

private slots:
    void onItemClicked(const QModelIndex &index);

private:
    QStandardItemModel *m_model;
};

#endif // MD_EDITOR_OUTLINETREE_H
```

`src/ui/OutlineTree.cpp`:
```cpp
#include "OutlineTree.h"
#include <QJsonObject>

OutlineTree::OutlineTree(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QStandardItemModel(this))
{
    m_model->setHorizontalHeaderLabels({"大纲"});
    setModel(m_model);
    setHeaderHidden(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(this, &QTreeView::clicked, this, &OutlineTree::onItemClicked);
}

void OutlineTree::updateHeadings(const QJsonArray &headings)
{
    m_model->clear();

    if (headings.isEmpty()) return;

    QStandardItem *root = m_model->invisibleRootItem();
    // 用栈来构建层级关系
    QList<QStandardItem *> levelStack; // levelStack[i] = 最近的第 (i+1) 级标题的 item

    for (const QJsonValue &val : headings) {
        QJsonObject obj = val.toObject();
        int level = obj["level"].toInt();
        QString text = obj["text"].toString();
        int pos = obj["pos"].toInt();

        auto *item = new QStandardItem(text);
        item->setData(pos, Qt::UserRole);

        // 找到父节点：向上查找最近的比当前级别小的标题
        while (levelStack.size() >= level) {
            levelStack.removeLast();
        }

        QStandardItem *parent = levelStack.isEmpty() ? root : levelStack.last();
        parent->appendRow(item);

        levelStack.append(item);
    }

    expandAll();
}

void OutlineTree::onItemClicked(const QModelIndex &index)
{
    int pos = index.data(Qt::UserRole).toInt();
    emit headingClicked(pos);
}
```

- [ ] **步骤 4：编写 SideBar 容器**

`src/ui/SideBar.h`:
```cpp
#ifndef MD_EDITOR_SIDEBAR_H
#define MD_EDITOR_SIDEBAR_H

#include <QWidget>
#include <QPushButton>

class FileTree;
class OutlineTree;

class SideBar : public QWidget {
    Q_OBJECT
public:
    explicit SideBar(QWidget *parent = nullptr);

    FileTree *fileTree() const;
    OutlineTree *outlineTree() const;

    void setCollapsed(bool collapsed);
    bool isCollapsed() const;

private:
    void setupUi();
    FileTree *m_fileTree;
    OutlineTree *m_outlineTree;
    QPushButton *m_openFolderBtn;
    bool m_collapsed;
};

#endif // MD_EDITOR_SIDEBAR_H
```

`src/ui/SideBar.cpp`:
```cpp
#include "SideBar.h"
#include "FileTree.h"
#include "OutlineTree.h"
#include <QVBoxLayout>
#include <QTabWidget>

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
    , m_fileTree(new FileTree(this))
    , m_outlineTree(new OutlineTree(this))
    , m_collapsed(false)
{
    setupUi();
}

void SideBar::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QTabWidget *tabWidget = new QTabWidget(this);
    tabWidget->addTab(m_fileTree, "文件");
    tabWidget->addTab(m_outlineTree, "大纲");
    layout->addWidget(tabWidget);

    m_openFolderBtn = new QPushButton("打开文件夹...", this);
    layout->addWidget(m_openFolderBtn);

    setFixedWidth(260);
}

FileTree *SideBar::fileTree() const { return m_fileTree; }
OutlineTree *SideBar::outlineTree() const { return m_outlineTree; }

void SideBar::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    setVisible(!collapsed);
}

bool SideBar::isCollapsed() const { return m_collapsed; }
```

- [ ] **步骤 5：更新 CMakeLists.txt**

添加所有新建的 `.cpp` 和 `.h` 文件。

- [ ] **步骤 6：构建验证编译通过**

```bash
cd build && cmake .. && make -j$(nproc)
```

- [ ] **步骤 7：提交**

```bash
git add src/ui/SideBar.* src/ui/FileTree.* src/ui/OutlineTree.* src/CMakeLists.txt
git commit -m "feat: 添加侧边栏（文件树 + 大纲面板）"
```

---

### 任务 10：StatusBar + SearchBar + MainWindow 集成

**文件：**
- 创建：`src/ui/StatusBar.h`、`src/ui/StatusBar.cpp`
- 创建：`src/ui/SearchBar.h`、`src/ui/SearchBar.cpp`
- 创建：`src/ui/MainWindow.h`、`src/ui/MainWindow.cpp`
- 修改：`src/main.cpp`（使用 MainWindow）
- 修改：`src/CMakeLists.txt`

**接口：**
- 产出：`StatusBar` — 显示行号、列号、字数统计、文件路径
- 产出：`SearchBar` — 查找/替换下滑面板
- 产出：`MainWindow` — 集成所有 UI 组件，编排所有信号-槽连接
- 消耗：所有已完成的任务组件

- [ ] **步骤 1：编写 StatusBar**

`src/ui/StatusBar.h`:
```cpp
#ifndef MD_EDITOR_STATUSBAR_H
#define MD_EDITOR_STATUSBAR_H

#include <QStatusBar>
#include <QLabel>

class StatusBar : public QStatusBar {
    Q_OBJECT
public:
    explicit StatusBar(QWidget *parent = nullptr);

public slots:
    void updateCursorPosition(int line, int col, int /*tabId*/);
    void updateFilePath(const QString &path);
    void updateWordCount(const QString &markdown);

private:
    QLabel *m_cursorLabel;
    QLabel *m_fileLabel;
    QLabel *m_wordLabel;
};

#endif // MD_EDITOR_STATUSBAR_H
```

`src/ui/StatusBar.cpp`:
```cpp
#include "StatusBar.h"

StatusBar::StatusBar(QWidget *parent)
    : QStatusBar(parent)
{
    m_cursorLabel = new QLabel("行 1, 列 1", this);
    m_fileLabel = new QLabel("", this);
    m_wordLabel = new QLabel("字数: 0", this);

    addWidget(m_cursorLabel, 1);
    addPermanentWidget(m_wordLabel);
    addPermanentWidget(m_fileLabel);
}

void StatusBar::updateCursorPosition(int line, int col, int /*tabId*/)
{
    m_cursorLabel->setText(QString("行 %1, 列 %2").arg(line).arg(col));
}

void StatusBar::updateFilePath(const QString &path)
{
    m_fileLabel->setText(path.isEmpty() ? "" : path);
}

void StatusBar::updateWordCount(const QString &markdown)
{
    int count = markdown.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).count();
    m_wordLabel->setText(QString("字数: %1").arg(count));
}
```

- [ ] **步骤 2：编写 SearchBar**

`src/ui/SearchBar.h`:
```cpp
#ifndef MD_EDITOR_SEARCHBAR_H
#define MD_EDITOR_SEARCHBAR_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr);

signals:
    void findRequested(const QString &text, bool caseSensitive);
    void replaceRequested(const QString &find, const QString &replace);
    void replaceAllRequested(const QString &find, const QString &replace);
    void closeRequested();

private:
    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QPushButton *m_caseBtn;
};

#endif // MD_EDITOR_SEARCHBAR_H
```

`src/ui/SearchBar.cpp`:
```cpp
#include "SearchBar.h"
#include <QHBoxLayout>
#include <QKeyEvent>

SearchBar::SearchBar(QWidget *parent)
    : QWidget(parent)
    , m_findEdit(new QLineEdit(this))
    , m_replaceEdit(new QLineEdit(this))
    , m_caseBtn(new QPushButton("Aa", this))
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_findEdit->setPlaceholderText("查找...");
    m_findEdit->setMaximumWidth(250);
    layout->addWidget(m_findEdit);

    m_replaceEdit->setPlaceholderText("替换为...");
    m_replaceEdit->setMaximumWidth(250);
    layout->addWidget(m_replaceEdit);

    m_caseBtn->setCheckable(true);
    m_caseBtn->setFixedWidth(40);
    layout->addWidget(m_caseBtn);

    QPushButton *findBtn = new QPushButton("查找", this);
    connect(findBtn, &QPushButton::clicked, this, [this]() {
        emit findRequested(m_findEdit->text(), m_caseBtn->isChecked());
    });
    layout->addWidget(findBtn);

    QPushButton *replaceBtn = new QPushButton("替换", this);
    connect(replaceBtn, &QPushButton::clicked, this, [this]() {
        emit replaceRequested(m_findEdit->text(), m_replaceEdit->text());
    });
    layout->addWidget(replaceBtn);

    QPushButton *replaceAllBtn = new QPushButton("全部替换", this);
    connect(replaceAllBtn, &QPushButton::clicked, this, [this]() {
        emit replaceAllRequested(m_findEdit->text(), m_replaceEdit->text());
    });
    layout->addWidget(replaceAllBtn);

    QPushButton *closeBtn = new QPushButton("✕", this);
    connect(closeBtn, &QPushButton::clicked, this, &SearchBar::closeRequested);
    layout->addWidget(closeBtn);

    layout->addStretch();

    setVisible(false);
}
```

- [ ] **步骤 3：编写 MainWindow — 集中所有组件**

`src/ui/MainWindow.h`:
```cpp
#ifndef MD_EDITOR_MAINWINDOW_H
#define MD_EDITOR_MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>

class MenuBar;
class ToolBar;
class SideBar;
class TabWidget;
class StatusBar;
class SearchBar;
class EditorBridge;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onEditorBridgeCreated(EditorBridge *bridge);
    void onTabChanged(int tabId);

    // 工具栏响应
    void onBoldClicked();
    void onItalicClicked();
    void onHeadingSelected(int level);
    void onInsertTable();
    void onInsertLink();
    void onInsertImage();
    void onToggleSourceMode();
    void onExportPdf();
    void onThemeSwitch(const QString &theme);

    // 文件操作
    void onOpenFolder();
    void onFileDoubleClicked(const QString &path);
    void onNewFile();

private:
    void setupUi();
    void connectSignals();
    void runEditorCommand(const QString &js);
    EditorBridge *currentBridge() const;

    MenuBar *m_menuBar;
    ToolBar *m_toolBar;
    SideBar *m_sideBar;
    TabWidget *m_tabWidget;
    StatusBar *m_statusBar;
    SearchBar *m_searchBar;
    QSplitter *m_splitter;
};

#endif // MD_EDITOR_MAINWINDOW_H
```

`src/ui/MainWindow.cpp`:
```cpp
#include "MainWindow.h"
#include "MenuBar.h"
#include "ToolBar.h"
#include "SideBar.h"
#include "FileTree.h"
#include "OutlineTree.h"
#include "TabWidget.h"
#include "EditorTab.h"
#include "StatusBar.h"
#include "SearchBar.h"
#include "../core/EditorBridge.h"
#include "../app/Application.h"
#include "../app/ThemeManager.h"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    connectSignals();

    // 启动时新建一个空白标签页
    m_tabWidget->openNewTab();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi()
{
    setWindowTitle("Markdown Editor");
    resize(1280, 800);

    // 菜单栏
    m_menuBar = new MenuBar(this);
    setMenuBar(m_menuBar);

    // 工具栏
    m_toolBar = new ToolBar(this);
    addToolBar(m_toolBar);

    // 中央区域：侧边栏 + 编辑器（可拖拽分割）
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_sideBar = new SideBar(this);
    m_splitter->addWidget(m_sideBar);

    m_tabWidget = new TabWidget(this);
    m_splitter->addWidget(m_tabWidget);

    m_splitter->setStretchFactor(0, 0); // 侧边栏固定
    m_splitter->setStretchFactor(1, 1); // 编辑器伸展
    m_splitter->setSizes({260, 1020});

    setCentralWidget(m_splitter);

    // 搜索栏
    m_searchBar = new SearchBar(this);
    // 搜索栏默认隐藏，按 Ctrl+F 显示

    // 状态栏
    m_statusBar = new StatusBar(this);
    setStatusBar(m_statusBar);

    // 初始化亮色主题
    Application::instance()->themeManager()->switchTheme("light");
}

void MainWindow::connectSignals()
{
    // === 菜单栏信号 ===
    connect(m_menuBar, &MenuBar::newFile, this, &MainWindow::onNewFile);
    connect(m_menuBar, &MenuBar::openFolder, this, &MainWindow::onOpenFolder);
    connect(m_menuBar, &MenuBar::openFile, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "打开文件", "",
                                                     "Markdown 文件 (*.md *.markdown);;所有文件 (*)");
        if (!path.isEmpty()) onFileDoubleClicked(path);
    });
    connect(m_menuBar, &MenuBar::exportPdf, this, &MainWindow::onExportPdf);
    connect(m_menuBar, &MenuBar::switchTheme, this, &MainWindow::onThemeSwitch);
    connect(m_menuBar, &MenuBar::toggleSidebar, this, [this]() {
        m_sideBar->setCollapsed(!m_sideBar->isCollapsed());
    });
    connect(m_menuBar, &MenuBar::toggleSourceMode, this, &MainWindow::onToggleSourceMode);
    connect(m_menuBar, &MenuBar::insertLink, this, &MainWindow::onInsertLink);
    connect(m_menuBar, &MenuBar::insertImage, this, &MainWindow::onInsertImage);
    connect(m_menuBar, &MenuBar::insertTable, this, &MainWindow::onInsertTable);

    // === 工具栏信号 ===
    connect(m_toolBar, &ToolBar::boldClicked, this, &MainWindow::onBoldClicked);
    connect(m_toolBar, &ToolBar::italicClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleItalic();");
    });
    connect(m_toolBar, &ToolBar::strikethroughClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleStrike();");
    });
    connect(m_toolBar, &ToolBar::codeClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleCode();");
    });
    connect(m_toolBar, &ToolBar::underlineClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleUnderline();");
    });
    connect(m_toolBar, &ToolBar::highlightClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleHighlight();");
    });
    connect(m_toolBar, &ToolBar::headingSelected, this, &MainWindow::onHeadingSelected);
    connect(m_toolBar, &ToolBar::bulletListClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleBulletList();");
    });
    connect(m_toolBar, &ToolBar::orderedListClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleOrderedList();");
    });
    connect(m_toolBar, &ToolBar::taskListClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleTaskList();");
    });
    connect(m_toolBar, &ToolBar::blockquoteClicked, this, [this]() {
        runEditorCommand("window.editorApi.toggleBlockquote();");
    });
    connect(m_toolBar, &ToolBar::linkClicked, this, &MainWindow::onInsertLink);
    connect(m_toolBar, &ToolBar::imageClicked, this, &MainWindow::onInsertImage);
    connect(m_toolBar, &ToolBar::tableClicked, this, &MainWindow::onInsertTable);
    connect(m_toolBar, &ToolBar::horizontalRuleClicked, this, [this]() {
        runEditorCommand("window.editorApi.insertHorizontalRule();");
    });
    connect(m_toolBar, &ToolBar::sourceToggleClicked, this, &MainWindow::onToggleSourceMode);
    connect(m_toolBar, &ToolBar::pdfExportClicked, this, &MainWindow::onExportPdf);

    // === 标签页管理 ===
    connect(m_tabWidget, &TabWidget::editorBridgeCreated,
            this, &MainWindow::onEditorBridgeCreated);
    connect(m_tabWidget, &TabWidget::currentTabChanged,
            this, &MainWindow::onTabChanged);

    // === 文件树 ===
    connect(m_sideBar->fileTree(), &FileTree::fileDoubleClicked,
            this, &MainWindow::onFileDoubleClicked);
    connect(m_sideBar->fileTree(), &FileTree::fileDoubleClicked, this, [this](const QString &) {
        // 文件已在 onFileDoubleClicked 中打开，这里更新状态栏
    });

    // === 文件树打开文件夹按钮 ===
    // 通过 SideBar 中的按钮触发
}

void MainWindow::onEditorBridgeCreated(EditorBridge *bridge)
{
    // 连接光标位置更新
    connect(bridge, &EditorBridge::cursorPositionChanged,
            m_statusBar, &StatusBar::updateCursorPosition);

    // 连接大纲更新
    connect(bridge, &EditorBridge::headingsUpdated, this,
            [this](const QJsonArray &headings, int /*tabId*/) {
                m_sideBar->outlineTree()->updateHeadings(headings);
            });
}

void MainWindow::onTabChanged(int tabId)
{
    Q_UNUSED(tabId)
    // 更新当前标签页关联的状态栏和大纲
}

EditorBridge *MainWindow::currentBridge() const
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    return tab ? tab->bridge() : nullptr;
}

void MainWindow::runEditorCommand(const QString &js)
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (tab) {
        tab->runJsCommand(js);
    }
}

// === 工具栏响应实现 ===

void MainWindow::onBoldClicked()
{
    runEditorCommand("window.editorApi.toggleBold();");
}

void MainWindow::onItalicClicked()
{
    runEditorCommand("window.editorApi.toggleItalic();");
}

void MainWindow::onHeadingSelected(int level)
{
    QString js = QString("window.editorApi.setHeading(%1);").arg(level);
    runEditorCommand(js);
}

void MainWindow::onInsertTable()
{
    runEditorCommand("window.editorApi.insertTable(3, 3);");
}

void MainWindow::onInsertLink()
{
    runEditorCommand("window.editorApi.insertLink();");
}

void MainWindow::onInsertImage()
{
    QString path = QFileDialog::getOpenFileName(this, "插入图片", "",
                                                 "图片文件 (*.png *.jpg *.jpeg *.gif *.svg *.webp)");
    if (!path.isEmpty()) {
        QString js = QString("window.editorApi.insertImage('%1');").arg(path);
        runEditorCommand(js);
    }
}

void MainWindow::onToggleSourceMode()
{
    runEditorCommand("window.editorApi.toggleSourceMode();");
}

void MainWindow::onExportPdf()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString path = QFileDialog::getSaveFileName(this, "导出 PDF", "document.pdf",
                                                 "PDF 文件 (*.pdf)");
    if (path.isEmpty()) return;

    // 获取编辑器 HTML 内容，通过隐藏 WebView 导出
    tab->runJsCommand(
        "window.editorApi.getContent();"
    );
    // PDF 导出将在后续任务中完善
    m_statusBar->showMessage("PDF 导出功能将在后续版本中完善", 3000);
}

void MainWindow::onThemeSwitch(const QString &theme)
{
    Application::instance()->themeManager()->switchTheme(theme);
    // 同步到所有已打开的标签页
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->setEditorTheme(theme);
    }
}

// === 文件操作 ===

void MainWindow::onOpenFolder()
{
    QString path = QFileDialog::getExistingDirectory(this, "打开文件夹", "");
    if (!path.isEmpty()) {
        m_sideBar->fileTree()->setRootPath(path);
        m_statusBar->showMessage(QString("已打开文件夹: %1").arg(path), 3000);
    }
}

void MainWindow::onFileDoubleClicked(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "打开失败",
                             QString("无法打开文件: %1").arg(path));
        return;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");
    QString content = in.readAll();
    file.close();

    EditorTab *tab = m_tabWidget->openFileTab(path, content);
    Q_UNUSED(tab)
    m_statusBar->updateFilePath(path);
}

void MainWindow::onNewFile()
{
    m_tabWidget->openNewTab();
}
```

- [ ] **步骤 4：更新 main.cpp**

```cpp
#include "app/Application.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    Application app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
```

- [ ] **步骤 5：更新 CMakeLists.txt**

添加 `StatusBar`、`SearchBar`、`MainWindow` 的源文件和头文件。

- [ ] **步骤 6：构建并完整测试**

```bash
cd build && cmake .. && make -j$(nproc) && ./md-editor
```

预期：
- 完整窗口显示，包含菜单栏、工具栏、侧边栏、标签页编辑区、状态栏
- 工具栏按钮点击可触发编辑器对应操作
- 文件树可浏览本地文件系统
- 主题切换正常

- [ ] **步骤 7：提交**

```bash
git add src/ui/StatusBar.* src/ui/SearchBar.* src/ui/MainWindow.* src/main.cpp src/CMakeLists.txt
git commit -m "feat: 集成 MainWindow + StatusBar + SearchBar，完成 UI 框架"
```

---

### 任务 11：FileManager — 文件读写核心

**文件：**
- 创建：`src/core/FileManager.h`、`src/core/FileManager.cpp`
- 创建：`tests/test_filemanager.cpp`
- 修改：`tests/CMakeLists.txt`、`src/CMakeLists.txt`

**接口：**
- 产出：`FileManager` 类，封装文件读写、编码检测
- 产出方法：
  - `readFile(path)` → `QString` 内容
  - `writeFile(path, content)` → `bool` 成功

- [ ] **步骤 1：编写 FileManager**

`src/core/FileManager.h`:
```cpp
#ifndef MD_EDITOR_FILEMANAGER_H
#define MD_EDITOR_FILEMANAGER_H

#include <QObject>
#include <QFileSystemWatcher>

class FileManager : public QObject {
    Q_OBJECT
public:
    explicit FileManager(QObject *parent = nullptr);

    QString readFile(const QString &path);
    bool writeFile(const QString &path, const QString &content);

    void watchFile(const QString &path);
    void unwatchFile(const QString &path);

signals:
    void fileExternallyModified(const QString &path);
    void fileReadError(const QString &path, const QString &error);
    void fileWriteError(const QString &path, const QString &error);

private slots:
    void onFileChanged(const QString &path);

private:
    QFileSystemWatcher *m_watcher;
};

#endif // MD_EDITOR_FILEMANAGER_H
```

`src/core/FileManager.cpp`:
```cpp
#include "FileManager.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>

FileManager::FileManager(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &FileManager::onFileChanged);
}

QString FileManager::readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit fileReadError(path, file.errorString());
        return {};
    }
    QTextStream in(&file);
    // 尝试检测编码（简单方案：先尝试 UTF-8）
    QString content = in.readAll();
    file.close();
    return content;
}

bool FileManager::writeFile(const QString &path, const QString &content)
{
    // 先写入临时文件，成功后重命名（原子写入）
    QString tmpPath = path + ".tmp";
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit fileWriteError(path, file.errorString());
        return false;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << content;
    file.close();

    // 原子替换
    if (QFile::exists(path)) {
        QFile::remove(path);
    }
    if (!QFile::rename(tmpPath, path)) {
        QFile::remove(tmpPath);
        emit fileWriteError(path, "写入失败：无法替换原文件");
        return false;
    }

    // 保存成功后重新监控
    watchFile(path);
    return true;
}

void FileManager::watchFile(const QString &path)
{
    if (QFile::exists(path)) {
        m_watcher->addPath(path);
    }
}

void FileManager::unwatchFile(const QString &path)
{
    m_watcher->removePath(path);
}

void FileManager::onFileChanged(const QString &path)
{
    emit fileExternallyModified(path);
}
```

- [ ] **步骤 2：编写 FileManager 单元测试**

`tests/test_filemanager.cpp`:
```cpp
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "../src/core/FileManager.h"

class TestFileManager : public QObject {
    Q_OBJECT
private slots:
    void testWriteAndRead() {
        FileManager fm;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString path = dir.path() + "/test.md";
        QString content = "# 标题\n\n测试内容。";

        QVERIFY(fm.writeFile(path, content));
        QVERIFY(QFile::exists(path));

        QString read = fm.readFile(path);
        QCOMPARE(read, content);
    }

    void testReadNonExistent() {
        FileManager fm;
        QSignalSpy spy(&fm, &FileManager::fileReadError);
        QString result = fm.readFile("/nonexistent/path.md");
        QVERIFY(result.isEmpty());
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestFileManager)
#include "test_filemanager.moc"
```

- [ ] **步骤 3：更新 tests/CMakeLists.txt** 添加 test_filemanager。

- [ ] **步骤 4：运行测试**

```bash
cd build && cmake .. && make -j$(nproc) && ctest --output-on-failure
```

- [ ] **步骤 5：提交**

```bash
git add src/core/FileManager.* tests/test_filemanager.cpp tests/CMakeLists.txt src/CMakeLists.txt
git commit -m "feat: 添加 FileManager 文件读写核心及单元测试"
```

---

### 任务 12：MarkdownHandler + PdfExporter

**文件：**
- 创建：`src/core/MarkdownHandler.h`、`src/core/MarkdownHandler.cpp`
- 创建：`src/core/PdfExporter.h`、`src/core/PdfExporter.cpp`
- 创建：`tests/test_markdownhandler.cpp`
- 修改：`src/CMakeLists.txt`、`tests/CMakeLists.txt`

**接口：**
- 产出：`MarkdownHandler` — 封装 cmark-gfm，提供 MD→HTML 和 MD→纯文本转换
- 产出：`PdfExporter` — 使用隐藏 QWebEngineView 进行 HTML→PDF 转换
- 消耗：`MarkdownHandler` 依赖 libcmark-gfm（通过 CMake find_package 或直接链接）

- [ ] **步骤 1：安装 cmark-gfm 开发库**

```bash
sudo apt install -y libcmark-gfm-dev libcmark-gfm-extensions-dev
```

- [ ] **步骤 2：更新 CMakeLists.txt 添加 cmark-gfm 依赖**

顶层 `CMakeLists.txt`:
```cmake
find_package(PkgConfig)
pkg_check_modules(CMARK_GFM REQUIRED libcmark-gfm)
```

`src/CMakeLists.txt`:
```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebChannel
    ${CMARK_GFM_LIBRARIES}
)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMARK_GFM_INCLUDE_DIRS})
```

- [ ] **步骤 3：编写 MarkdownHandler**

`src/core/MarkdownHandler.h`:
```cpp
#ifndef MD_EDITOR_MARKDOWNHANDLER_H
#define MD_EDITOR_MARKDOWNHANDLER_H

#include <QString>

class MarkdownHandler {
public:
    static QString toHtml(const QString &markdown);
    static QString toPlainText(const QString &markdown);
    static QString extractTitle(const QString &markdown);
};

#endif // MD_EDITOR_MARKDOWNHANDLER_H
```

`src/core/MarkdownHandler.cpp`:
```cpp
#include "MarkdownHandler.h"
#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
#include <QTextDocument>

QString MarkdownHandler::toHtml(const QString &markdown)
{
    QByteArray input = markdown.toUtf8();
    char *html = cmark_markdown_to_html(
        input.constData(), input.size(),
        CMARK_OPT_DEFAULT | CMARK_OPT_GITHUB_PRE_LANG
        | CMARK_OPT_UNSAFE // 允许原始 HTML（二阶段需要）
    );
    QString result = QString::fromUtf8(html);
    free(html);
    return result;
}

QString MarkdownHandler::toPlainText(const QString &markdown)
{
    // 使用 QTextDocument 从 HTML 提取纯文本
    QString html = toHtml(markdown);
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText();
}

QString MarkdownHandler::extractTitle(const QString &markdown)
{
    QByteArray input = markdown.toUtf8();
    const char *title = cmark_markdown_extract_title(
        input.constData(), input.size(), 80
    );
    QString result = QString::fromUtf8(title);
    free((void *)title);
    return result;
}
```

- [ ] **步骤 4：编写 PdfExporter**

`src/core/PdfExporter.h`:
```cpp
#ifndef MD_EDITOR_PDFEXPORTER_H
#define MD_EDITOR_PDFEXPORTER_H

#include <QObject>
#include <QWebEngineView>
#include <QPageLayout>

class PdfExporter : public QObject {
    Q_OBJECT
public:
    explicit PdfExporter(QObject *parent = nullptr);

    void exportToPdf(const QString &html, const QString &outputPath,
                     QPageLayout::Orientation orientation = QPageLayout::Portrait,
                     QPageSize::PageSizeId paperSize = QPageSize::A4);

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);

private:
    QWebEngineView *m_renderView;
};

#endif // MD_EDITOR_PDFEXPORTER_H
```

`src/core/PdfExporter.cpp`:
```cpp
#include "PdfExporter.h"
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QFile>
#include <QPageSize>
#include <QPageLayout>

PdfExporter::PdfExporter(QObject *parent)
    : QObject(parent)
    , m_renderView(new QWebEngineView())
{
    m_renderView->setVisible(false); // 隐藏渲染视图
    m_renderView->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessFileUrls, true);
}

void PdfExporter::exportToPdf(const QString &html, const QString &outputPath,
                               QPageLayout::Orientation orientation,
                               QPageSize::PageSizeId paperSize)
{
    // 构建完整的 HTML 文档（含打印样式）
    QString fullHtml = R"(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<style>
  @media print {
    @page { margin: 20mm; }
    body { font-family: "Noto Sans SC", "DejaVu Sans", sans-serif;
           font-size: 12pt; line-height: 1.6; color: #333; }
    h1 { font-size: 1.8em; }
    h2 { font-size: 1.4em; }
    h3 { font-size: 1.2em; }
    pre { background: #f5f5f5; padding: 12px; border-radius: 4px;
          font-family: monospace; font-size: 10pt; overflow-x: auto; }
    code { background: #f5f5f5; padding: 1px 4px; border-radius: 2px; }
    table { border-collapse: collapse; width: 100%%; }
    th, td { border: 1px solid #ddd; padding: 6px 10px; }
    img { max-width: 100%%; }
    blockquote { border-left: 3px solid #0078d4; padding-left: 12px;
                 color: #666; margin: 1em 0; }
  }
</style>
</head>
<body>)" + html + R"(</body>
</html>)";

    // 使用 data URI 加载 HTML
    m_renderView->setHtml(fullHtml, QUrl("about:blank"));

    connect(m_renderView, &QWebEngineView::loadFinished, this,
            [this, outputPath, orientation, paperSize](bool ok) {
        if (!ok) {
            emit exportError("HTML 渲染失败");
            return;
        }

        QPageLayout layout(QPageSize(paperSize), orientation, QMarginsF(0, 0, 0, 0));
        m_renderView->page()->printToPdf(outputPath, layout);

        // printToPdf 是异步的，这里简化处理
        // 实际应用中应连接 pdfPrintingFinished 信号
        connect(m_renderView->page(), &QWebEnginePage::pdfPrintingFinished,
                this, [this, outputPath](const QString &filePath, bool success) {
            if (success) {
                emit exportFinished(true, filePath);
            } else {
                emit exportError("PDF 生成失败");
            }
        });
    });
}
```

- [ ] **步骤 5：编写 MarkdownHandler 测试**

`tests/test_markdownhandler.cpp`:
```cpp
#include <QTest>
#include "../src/core/MarkdownHandler.h"

class TestMarkdownHandler : public QObject {
    Q_OBJECT
private slots:
    void testBasicConversion() {
        QString md = "# Hello\n\nWorld";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("<h1>"));
        QVERIFY(html.contains("Hello"));
    }

    void testCodeBlock() {
        QString md = "```cpp\nint main() {}\n```";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("<code") || html.contains("<pre"));
    }

    void testEmpty() {
        QString html = MarkdownHandler::toHtml("");
        QVERIFY(html.isEmpty() || html == "\n");
    }

    void testExtractTitle() {
        QString md = "# 我的文档标题\n\n内容。";
        QString title = MarkdownHandler::extractTitle(md);
        QVERIFY(title.contains("我的文档标题"));
    }
};

QTEST_MAIN(TestMarkdownHandler)
#include "test_markdownhandler.moc"
```

- [ ] **步骤 6：构建并运行测试**

```bash
cd build && cmake .. && make -j$(nproc) && ctest --output-on-failure
```

- [ ] **步骤 7：提交**

```bash
git add src/core/MarkdownHandler.* src/core/PdfExporter.* tests/test_markdownhandler.cpp src/CMakeLists.txt tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat: 添加 MarkdownHandler (cmark-gfm) 和 PdfExporter"
```

---

### 任务 13：设置对话框

**文件：**
- 创建：`src/ui/SettingsDialog.h`、`src/ui/SettingsDialog.cpp`
- 修改：`src/CMakeLists.txt`、`src/ui/MainWindow.cpp`（集成）

**接口：**
- 产出：`SettingsDialog` — 模态设置对话框
- 消耗：`FileManager`（读写设置）、`ThemeManager`（主题设置）

- [ ] **步骤 1：编写 SettingsDialog**

`src/ui/SettingsDialog.h`:
```cpp
#ifndef MD_EDITOR_SETTINGSDIALOG_H
#define MD_EDITOR_SETTINGSDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QFontComboBox>

class FileManager;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(FileManager *fileManager, QWidget *parent = nullptr);

private slots:
    void onAccepted();

private:
    void setupUi();
    void loadSettings();

    // 通用
    QCheckBox *m_restoreSessionCheck;

    // 编辑器
    QFontComboBox *m_fontCombo;
    QSpinBox *m_fontSizeSpin;
    QCheckBox *m_lineNumbersCheck;
    QCheckBox *m_wordWrapCheck;
    QSpinBox *m_tabSizeSpin;

    // 外观
    QComboBox *m_themeCombo;
    QComboBox *m_langCombo;

    // 导出
    QComboBox *m_paperSizeCombo;
    QComboBox *m_orientationCombo;

    FileManager *m_fileManager;
};

#endif // MD_EDITOR_SETTINGSDIALOG_H
```

`src/ui/SettingsDialog.cpp`:
```cpp
#include "SettingsDialog.h"
#include "../core/FileManager.h"
#include "../app/Application.h"
#include "../app/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>

SettingsDialog::SettingsDialog(FileManager *fileManager, QWidget *parent)
    : QDialog(parent)
    , m_fileManager(fileManager)
{
    setWindowTitle("设置");
    setMinimumWidth(500);
    setupUi();
    loadSettings();
}

void SettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QTabWidget *tabs = new QTabWidget(this);

    // === 通用 ===
    QWidget *generalTab = new QWidget();
    QVBoxLayout *generalLayout = new QVBoxLayout(generalTab);

    QGroupBox *startupGroup = new QGroupBox("启动");
    QVBoxLayout *startupLayout = new QVBoxLayout();
    m_restoreSessionCheck = new QCheckBox("启动时恢复上次会话");
    startupLayout->addWidget(m_restoreSessionCheck);
    startupGroup->setLayout(startupLayout);
    generalLayout->addWidget(startupGroup);

    generalLayout->addStretch();
    tabs->addTab(generalTab, "通用");

    // === 编辑器 ===
    QWidget *editorTab = new QWidget();
    QFormLayout *editorForm = new QFormLayout(editorTab);
    m_fontCombo = new QFontComboBox();
    editorForm->addRow("字体:", m_fontCombo);
    m_fontSizeSpin = new QSpinBox();
    m_fontSizeSpin->setRange(10, 32);
    m_fontSizeSpin->setSuffix(" px");
    editorForm->addRow("字号:", m_fontSizeSpin);
    m_lineNumbersCheck = new QCheckBox();
    m_lineNumbersCheck->setChecked(true);
    editorForm->addRow("显示行号:", m_lineNumbersCheck);
    m_wordWrapCheck = new QCheckBox();
    m_wordWrapCheck->setChecked(true);
    editorForm->addRow("自动换行:", m_wordWrapCheck);
    m_tabSizeSpin = new QSpinBox();
    m_tabSizeSpin->setRange(2, 8);
    m_tabSizeSpin->setSuffix(" 空格");
    editorForm->addRow("Tab 宽度:", m_tabSizeSpin);
    tabs->addTab(editorTab, "编辑器");

    // === 外观 ===
    QWidget *appearanceTab = new QWidget();
    QFormLayout *appearanceForm = new QFormLayout(appearanceTab);
    m_themeCombo = new QComboBox();
    m_themeCombo->addItem("亮色", "light");
    m_themeCombo->addItem("暗色", "dark");
    appearanceForm->addRow("主题:", m_themeCombo);
    m_langCombo = new QComboBox();
    m_langCombo->addItem("简体中文", "zh_CN");
    m_langCombo->addItem("English", "en_US");
    appearanceForm->addRow("界面语言:", m_langCombo);
    tabs->addTab(appearanceTab, "外观");

    // === 导出 ===
    QWidget *exportTab = new QWidget();
    QFormLayout *exportForm = new QFormLayout(exportTab);
    m_paperSizeCombo = new QComboBox();
    m_paperSizeCombo->addItem("A4", QPageSize::A4);
    m_paperSizeCombo->addItem("Letter", QPageSize::Letter);
    exportForm->addRow("默认纸张:", m_paperSizeCombo);
    m_orientationCombo = new QComboBox();
    m_orientationCombo->addItem("纵向", QPageLayout::Portrait);
    m_orientationCombo->addItem("横向", QPageLayout::Landscape);
    exportForm->addRow("默认方向:", m_orientationCombo);
    tabs->addTab(exportTab, "导出");

    mainLayout->addWidget(tabs);

    // 按钮
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

void SettingsDialog::loadSettings()
{
    // 从 QSettings 加载
    QSettings settings("md-editor", "md-editor");
    m_restoreSessionCheck->setChecked(
        settings.value("session/restore", true).toBool());
    m_fontCombo->setCurrentText(
        settings.value("editor/font", "Sans Serif").toString());
    m_fontSizeSpin->setValue(settings.value("editor/fontSize", 16).toInt());
    m_lineNumbersCheck->setChecked(
        settings.value("editor/lineNumbers", true).toBool());
    m_wordWrapCheck->setChecked(
        settings.value("editor/wordWrap", true).toBool());
    m_tabSizeSpin->setValue(settings.value("editor/tabSize", 4).toInt());

    QString theme = Application::instance()->themeManager()->currentTheme();
    m_themeCombo->setCurrentIndex(m_themeCombo->findData(theme));
}

void SettingsDialog::onAccepted()
{
    // 保存设置
    QSettings settings("md-editor", "md-editor");
    settings.setValue("session/restore", m_restoreSessionCheck->isChecked());
    settings.setValue("editor/font", m_fontCombo->currentText());
    settings.setValue("editor/fontSize", m_fontSizeSpin->value());
    settings.setValue("editor/lineNumbers", m_lineNumbersCheck->isChecked());
    settings.setValue("editor/wordWrap", m_wordWrapCheck->isChecked());
    settings.setValue("editor/tabSize", m_tabSizeSpin->value());
    settings.sync();

    // 应用主题
    Application::instance()->themeManager()->switchTheme(
        m_themeCombo->currentData().toString());

    accept();
}
```

- [ ] **步骤 2：更新 MainWindow 集成设置对话框**

在 MainWindow 中添加 `onShowSettings` 槽并连接到帮菜单。

- [ ] **步骤 3：构建验证**

```bash
cd build && cmake .. && make -j$(nproc)
```

- [ ] **步骤 4：提交**

```bash
git add src/ui/SettingsDialog.* src/CMakeLists.txt src/ui/MainWindow.*
git commit -m "feat: 添加设置对话框，覆盖通用/编辑器/外观/导出设置"
```

---

### 任务 14：会话恢复与完整性联调

**文件：**
- 修改：`src/ui/MainWindow.cpp`（添加会话保存/恢复）
- 修改：`src/core/FileManager.cpp`（扩展会话支持）

**接口：**
- 产出：应用关闭时保存会话（打开的标签页列表 + 光标位置）
- 产出：应用启动时恢复会话（如果用户启用了此选项）
- 消耗：`QSettings`、`MainWindow` 所有组件

- [ ] **步骤 1：在 MainWindow 中添加会话保存/恢复**

在 `MainWindow` 中添加以下方法：

```cpp
void MainWindow::saveSession()
{
    QSettings settings("md-editor", "md-editor");
    settings.beginGroup("session");

    QStringList files;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) {
            files << tab->property("filePath").toString();
        }
    }
    settings.setValue("openFiles", files);
    settings.setValue("activeTab", m_tabWidget->currentIndex());
    settings.setValue("windowGeometry", saveGeometry());
    settings.endGroup();
    settings.sync();
}

void MainWindow::restoreSession()
{
    QSettings settings("md-editor", "md-editor");
    if (!settings.value("session/restore", true).toBool()) return;

    settings.beginGroup("session");
    QStringList files = settings.value("openFiles").toStringList();
    int activeTab = settings.value("activeTab", 0).toInt();
    QByteArray geometry = settings.value("windowGeometry").toByteArray();
    settings.endGroup();

    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    for (const QString &path : files) {
        if (QFile::exists(path)) {
            onFileDoubleClicked(path);
        }
    }

    if (m_tabWidget->count() > activeTab) {
        m_tabWidget->setCurrentIndex(activeTab);
    }

    if (m_tabWidget->count() == 0) {
        m_tabWidget->openNewTab();
    }
}
```

- [ ] **步骤 2：连接 closeEvent 保存会话**

在 `MainWindow` 中重写 `closeEvent`:
```cpp
void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSession();
    event->accept();
}
```

- [ ] **步骤 3：构建并完整测试**

```bash
cd build && cmake .. && make -j$(nproc) && ./md-editor
```

完整验收清单：
- [ ] 窗口正常显示，包含所有组件
- [ ] 菜单栏各菜单项可点击
- [ ] 工具栏按钮触发编辑器操作（加粗、斜体、列表等）
- [ ] 标题下拉可切换段落格式
- [ ] 侧边栏可切换文件树/大纲
- [ ] 文件树可浏览目录，双击打开文件
- [ ] 大纲面板实时显示当前文档标题
- [ ] 多标签页可创建、切换、关闭
- [ ] 脏标记（●）在编辑后正确显示
- [ ] 主题切换（亮色/暗色）正常
- [ ] 设置对话框可打开和保存

- [ ] **步骤 4：提交**

```bash
git add src/ui/MainWindow.*
git commit -m "feat: 添加会话恢复功能，完成阶段一集成联调"
```

---

### 任务 15：完善与补充

以下为阶段一可能遗漏的补充项：

- [ ] **补充 CSS 编辑器样式**：确保 dark 主题下编辑器组件（表格、引用等）显示正常
- [ ] **安装 Markdown 扩展**：TipTap 的 `@tiptap/extension-markdown` 实现真正的 Markdown 导入/导出，替代当前 HTML-only 方案
- [ ] **安装数学公式扩展**：TipTap 的数学扩展（基于 KaTeX）
- [ ] **安装 Mermaid 扩展**：Mermaid 图表渲染支持
- [ ] **优化图标**：用 SVG 图标替换当前 Unicode 字符占位符
- [ ] **完善 PDF 导出**：在 MainWindow 中正确集成 PdfExporter
- [ ] **状态栏字数统计**：实时更新字数/字符数
- [ ] **快捷键处理**：确保常用快捷键（Ctrl+S/N/O/B/I/Z/Y）正常工作
- [ ] **完善搜索栏**：集成到 MainWindow，Ctrl+F 弹出，支持在编辑器中查找/替换
