# HTML 导出 + 自定义 CSS + 文档记账清理 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增「导出 HTML」（自包含单文件、完整渲染、资源全 base64 内联、跟随主题），新增「自定义 CSS」（设置注入编辑器 + 应用于 HTML/PDF 导出），并清理 CLAUDE.md/README/CHANGELOG 三处记账漂移。

**Architecture:** 复用现有 `PdfExporter`/`DocxExporter` 的 QObject+信号导出模式，新增 `HtmlExporter`；自定义 CSS 存 `QSettings["editor/customCss"]`，经 `editorApi.applyCustomCss` 注入编辑器，经导出器内联进产物。

**Tech Stack:** C++17 / Qt6（QWebEngine、QFile、QRegularExpression、QSettings）/ JS（TipTap editorApi）。

## Global Constraints

- 文档与注释用中文，代码标识符用英文。
- 兼容基准 Qt 6.4.2：不得依赖 6.8.x 独有行为。
- 改 `src/resources/` 下 qrc 资源（.js/.css/.qss）或 C++ 源码后必须 `cmake --build build` 重编译。
- 提交前缀 `feat:`/`fix:`/`chore:`，中文描述；每条 commit 末尾加 `Co-Authored-By: Claude <noreply@anthropic.com>`。
- 版本推进：本轮属新增内容多 → 0.7.0 → 0.8.0（改 `CMakeLists.txt` 的 `project(md-editor VERSION)` + CHANGELOG + 打 `v0.8.0` tag）。
- 构建/测试命令：`cmake --build build -j$(nproc)`；`cd build && ctest --output-on-failure`。

---

## Task 1: 将 highlight.min.js 纳入 qrc

**Files:**
- Modify: `src/resources/app.qrc:143`

**Interfaces:**
- Produces: qrc 资源 `:/web/highlight.min.js`（供 Task 2 的 HtmlExporter 内联）。

- [ ] **Step 1: 在 app.qrc 注册 highlight.min.js**

在 `<file>web/hljs-dark.css</file>` 之后新增一行：

```xml
        <file>web/hljs-dark.css</file>
        <file>web/highlight.min.js</file>
```

（`src/resources/web/highlight.min.js` 已存在，是 highlight.js v11.9.0 独立版，`var hljs=...` 声明，内联为 `<script>` 后暴露全局 `hljs`。）

- [ ] **Step 2: 重新编译验证资源可访问**

Run: `cmake --build build -j$(nproc)`
Expected: 编译通过（qrc 重新打包）。

- [ ] **Step 3: Commit**

```bash
git add src/resources/app.qrc
git commit -m "chore: highlight.min.js 纳入 qrc 供 HTML 导出内联

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 2: HtmlExporter 核心类 + 单元测试

**Files:**
- Create: `src/core/HtmlExporter.h`
- Create: `src/core/HtmlExporter.cpp`
- Modify: `src/CMakeLists.txt:11`（SOURCES）、`src/CMakeLists.txt:36`（HEADERS）
- Create: `tests/test_htmlexporter.cpp`
- Modify: `tests/CMakeLists.txt`（末尾）

**Interfaces:**
- Produces: `HtmlExporter`，公开方法 `void exportToHtml(const QString &html, const QString &docDir, const QString &outputPath, const QString &theme)`；信号 `exportFinished(bool, QString)`、`exportError(QString)`；公开静态助手 `QString mimeForExt(const QString &ext)`、`QString inlineCssFonts(const QString &css, const QString &fontDir)`、`QString inlineImages(const QString &html, const QString &docDir)`、`QString bodyCss(const QString &theme)`、`QString renderScript(const QString &theme)`。

- [ ] **Step 1: 写失败测试 tests/test_htmlexporter.cpp**

```cpp
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "../src/core/HtmlExporter.h"

class TestHtmlExporter : public QObject {
    Q_OBJECT
private slots:
    void testMimeForExt() {
        QCOMPARE(HtmlExporter::mimeForExt("woff2"), QStringLiteral("font/woff2"));
        QCOMPARE(HtmlExporter::mimeForExt("woff"), QStringLiteral("font/woff"));
        QCOMPARE(HtmlExporter::mimeForExt("ttf"), QStringLiteral("font/ttf"));
        QCOMPARE(HtmlExporter::mimeForExt("png"), QStringLiteral("image/png"));
        QCOMPARE(HtmlExporter::mimeForExt("jpg"), QStringLiteral("image/jpeg"));
        QCOMPARE(HtmlExporter::mimeForExt("jpeg"), QStringLiteral("image/jpeg"));
        QCOMPARE(HtmlExporter::mimeForExt("svg"), QStringLiteral("image/svg+xml"));
    }

    void testInlineCssFontsReplacesUrlWithBase64() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString fontsDir = dir.path() + "/fonts";
        QVERIFY(QDir().mkpath(fontsDir));
        QFile f(fontsDir + "/KaTeX_Main-Regular.woff2");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("FONTDATA"); f.close();

        QString css = "src:url(fonts/KaTeX_Main-Regular.woff2) format('woff2');";
        QString out = HtmlExporter::inlineCssFonts(css, dir.path() + "/fonts");
        QVERIFY(out.contains("data:font/woff2;base64,"));
        QVERIFY(!out.contains("url(fonts/"));
    }

    void testInlineImagesReplacesRelativeSrcWithBase64() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile f(dir.path() + "/a.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("PNGDATA"); f.close();

        QString html = "<p><img src=\"a.png\" alt=\"x\"></p>";
        QString out = HtmlExporter::inlineImages(html, dir.path());
        QVERIFY(out.contains("data:image/png;base64,"));
        QVERIFY(!out.contains("src=\"a.png\""));
        QVERIFY(out.contains("alt=\"x\"")); // 其他属性保留
    }

    void testInlineImagesSkipsMissingFile() {
        QString html = "<p><img src=\"nope.png\"></p>";
        QString out = HtmlExporter::inlineImages(html, "/nonexistent");
        QVERIFY(out.contains("src=\"nope.png\"")); // 读不到时原样保留
    }

    void testBodyCssDiffersByTheme() {
        QVERIFY(HtmlExporter::bodyCss("light").contains("background:"));
        QVERIFY(HtmlExporter::bodyCss("light") != HtmlExporter::bodyCss("dark"));
    }

    void testRenderScriptUsesTheme() {
        QVERIFY(HtmlExporter::renderScript("dark").contains("'dark'"));
        QVERIFY(HtmlExporter::renderScript("light").contains("'default'"));
    }
};

QTEST_APPLESS_MAIN(TestHtmlExporter)
#include "test_htmlexporter.moc"
```

- [ ] **Step 2: 在 tests/CMakeLists.txt 注册测试**

```cmake
add_executable(test_htmlexporter test_htmlexporter.cpp ../src/core/HtmlExporter.cpp)
target_include_directories(test_htmlexporter PRIVATE ../src)
target_link_libraries(test_htmlexporter PRIVATE Qt6::Test)
add_test(NAME test_htmlexporter COMMAND test_htmlexporter)
```

- [ ] **Step 3: 运行测试确认失败**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R test_htmlexporter --output-on-failure`
Expected: 编译失败（HtmlExporter.h 不存在）。

- [ ] **Step 4: 写 src/core/HtmlExporter.h**

```cpp
#ifndef MD_EDITOR_HTMLEXPORTER_H
#define MD_EDITOR_HTMLEXPORTER_H

#include <QObject>
#include <QString>

class HtmlExporter : public QObject {
    Q_OBJECT
public:
    explicit HtmlExporter(QObject *parent = nullptr);

    // 导出为自包含单文件 HTML（资源全 base64 内联）
    void exportToHtml(const QString &html, const QString &docDir,
                      const QString &outputPath, const QString &theme);

    // 可测试的纯逻辑助手
    static QString mimeForExt(const QString &ext);
    static QString inlineCssFonts(const QString &css, const QString &fontDir);
    static QString inlineImages(const QString &html, const QString &docDir);
    static QString bodyCss(const QString &theme);
    static QString renderScript(const QString &theme);

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);

private:
    QString readResource(const QString &path) const;
};

#endif // MD_EDITOR_HTMLEXPORTER_H
```

- [ ] **Step 5: 写 src/core/HtmlExporter.cpp**

```cpp
#include "HtmlExporter.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

HtmlExporter::HtmlExporter(QObject *parent) : QObject(parent) {}

QString HtmlExporter::readResource(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QString s = QString::fromUtf8(f.readAll());
    f.close();
    return s;
}

QString HtmlExporter::mimeForExt(const QString &ext)
{
    const QString e = ext.toLower();
    if (e == "woff2") return "font/woff2";
    if (e == "woff")  return "font/woff";
    if (e == "ttf")   return "font/ttf";
    if (e == "png")   return "image/png";
    if (e == "jpg" || e == "jpeg") return "image/jpeg";
    if (e == "gif")   return "image/gif";
    if (e == "svg")   return "image/svg+xml";
    if (e == "webp")  return "image/webp";
    return "application/octet-stream";
}

QString HtmlExporter::inlineCssFonts(const QString &css, const QString &fontDir)
{
    QRegularExpression re("url\\(fonts/([^)]+)\\)");
    QString out = css;
    int offset = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(css);
    while (it.hasNext()) {
        auto m = it.next();
        QString name = m.captured(1);
        QFile f(fontDir + "/" + name);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QByteArray bytes = f.readAll();
        f.close();
        QString mime = mimeForExt(QFileInfo(name).suffix());
        QString data = "url(data:" + mime + ";base64," + QString::fromLatin1(bytes.toBase64()) + ")";
        int start = m.capturedStart(0) + offset;
        int len = m.capturedLength(0);
        out.replace(start, len, data);
        offset += data.length() - len;
    }
    return out;
}

QString HtmlExporter::inlineImages(const QString &html, const QString &docDir)
{
    QRegularExpression re("<img\\b[^>]*src=\"([^\"]+)\"");
    QString out = html;
    int offset = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        QString src = m.captured(1);
        if (src.startsWith("data:") || src.startsWith("http://") || src.startsWith("https://"))
            continue;
        QString path = src.startsWith('/') ? src : docDir + "/" + src;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QByteArray bytes = f.readAll();
        f.close();
        QString mime = mimeForExt(QFileInfo(src).suffix());
        QString dataUri = "data:" + mime + ";base64," + QString::fromLatin1(bytes.toBase64());
        int start = m.capturedStart(1) + offset;
        int len = m.capturedLength(1);
        out.replace(start, len, dataUri);
        offset += dataUri.length() - len;
    }
    return out;
}

QString HtmlExporter::bodyCss(const QString &theme)
{
    const bool dark = (theme == "dark");
    if (dark) {
        return QStringLiteral(
            "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;font-size:16px;"
            "line-height:1.7;color:#c9d1d9;background:#0d1117;max-width:860px;margin:0 auto;padding:40px 32px;}"
            "h1,h2,h3,h4,h5,h6{line-height:1.3;margin:1.2em 0 .5em;font-weight:600;}"
            "h1{font-size:2em;border-bottom:1px solid #30363d;padding-bottom:.3em;}"
            "h2{font-size:1.5em;border-bottom:1px solid #30363d;padding-bottom:.3em;}"
            "h3{font-size:1.25em;}p{margin:.6em 0;}"
            "a{color:#58a6ff;text-decoration:none;}a:hover{text-decoration:underline;}"
            "code{background:#161b22;padding:2px 5px;border-radius:4px;font-size:.9em;}"
            "pre{background:#161b22;padding:12px 16px;border-radius:6px;overflow-x:auto;line-height:1.45;}"
            "pre code{background:transparent;padding:0;}"
            "blockquote{border-left:4px solid #30363d;color:#8b949e;padding:4px 16px;margin:.8em 0;}"
            "table{border-collapse:collapse;margin:.8em 0;display:block;overflow-x:auto;}"
            "th,td{border:1px solid #30363d;padding:6px 13px;}th{background:#21262d;font-weight:600;}"
            "tr:nth-child(2n){background:#161b22;}img{max-width:100%;}"
            "hr{border:0;border-top:1px solid #30363d;margin:1.5em 0;}"
            "ul,ol{padding-left:1.5em;}li{margin:.3em 0;}"
            "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;overflow-x:auto;}"
            "[data-mermaid]{text-align:center;margin:1em 0;}");
    }
    return QStringLiteral(
        "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;font-size:16px;"
        "line-height:1.7;color:#24292f;background:#ffffff;max-width:860px;margin:0 auto;padding:40px 32px;}"
        "h1,h2,h3,h4,h5,h6{line-height:1.3;margin:1.2em 0 .5em;font-weight:600;}"
        "h1{font-size:2em;border-bottom:1px solid #eaecef;padding-bottom:.3em;}"
        "h2{font-size:1.5em;border-bottom:1px solid #eaecef;padding-bottom:.3em;}"
        "h3{font-size:1.25em;}p{margin:.6em 0;}"
        "a{color:#0969da;text-decoration:none;}a:hover{text-decoration:underline;}"
        "code{background:#f0f1f3;padding:2px 5px;border-radius:4px;font-size:.9em;}"
        "pre{background:#f6f8fa;padding:12px 16px;border-radius:6px;overflow-x:auto;line-height:1.45;}"
        "pre code{background:transparent;padding:0;}"
        "blockquote{border-left:4px solid #d0d7de;color:#57606a;padding:4px 16px;margin:.8em 0;}"
        "table{border-collapse:collapse;margin:.8em 0;display:block;overflow-x:auto;}"
        "th,td{border:1px solid #d0d7de;padding:6px 13px;}th{background:#f6f8fa;font-weight:600;}"
        "tr:nth-child(2n){background:#f6f8fa;}img{max-width:100%;}"
        "hr{border:0;border-top:1px solid #d0d7de;margin:1.5em 0;}"
        "ul,ol{padding-left:1.5em;}li{margin:.3em 0;}"
        "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;overflow-x:auto;}"
        "[data-mermaid]{text-align:center;margin:1em 0;}");
}

QString HtmlExporter::renderScript(const QString &theme)
{
    const QString mermaidTheme = (theme == "dark") ? "dark" : "default";
    return QStringLiteral(
        "window.addEventListener('DOMContentLoaded',function(){"
        "if(window.katex){var es=document.querySelectorAll('[data-latex]');"
        "for(var i=0;i<es.length;i++){var e=es[i],l=e.getAttribute('data-latex');if(!l)continue;"
        "try{katex.render(l,e,{throwOnError:false,displayMode:e.getAttribute('data-type')==='block-math'});}"
        "catch(err){e.textContent=l;}}}"
        "if(window.mermaid){mermaid.initialize({startOnLoad:false,securityLevel:'loose',theme:'%1',"
        "themeVariables:{fontSize:'16px'}});"
        "var pres=document.querySelectorAll('pre code.language-mermaid');var seq=0;"
        "for(var j=0;j<pres.length;j++){var code=pres[j];var src=code.textContent;"
        "var pre=code.parentNode;var holder=document.createElement('div');holder.id='mermaid-export-'+(++seq);"
        "pre.parentNode.replaceChild(holder,pre);"
        "try{mermaid.render(holder.id,src).then(function(res){holder.innerHTML=res.svg;});}"
        "catch(err){holder.textContent=src;}}}"
        "if(window.hljs){hljs.highlightAll();}"
        "});").arg(mermaidTheme);
}

void HtmlExporter::exportToHtml(const QString &html, const QString &docDir,
                                const QString &outputPath, const QString &theme)
{
    QString body = inlineImages(html, docDir);

    // KaTeX CSS + 字体 base64
    QString katexCss = inlineCssFonts(readResource(":/web/katex.min.css"), ":/web/fonts");
    // 高亮主题 CSS
    QString hljsCss = readResource(theme == "dark" ? ":/web/hljs-dark.css" : ":/web/hljs-light.css");
    // 自定义 CSS（覆盖默认）
    QSettings settings("md-editor", "md-editor");
    QString customCss = settings.value("editor/customCss").toString();

    QString full = QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Markdown 导出</title>"
        "<style>%1</style><style>%2</style><style>%3</style>%4"
        "</head><body>%5</body></html>")
        .arg(bodyCss(theme), katexCss, hljsCss,
             customCss.isEmpty() ? QString() : QString("<style>%1</style>").arg(customCss),
             body);

    // 渲染脚本（顺序：KaTeX → highlight → Mermaid → 渲染）
    QString scripts = QStringLiteral("<script>%1</script><script>%2</script><script>%3</script><script>%4</script>")
        .arg(readResource(":/web/katex.min.js"),
             readResource(":/web/highlight.min.js"),
             readResource(":/web/mermaid.min.js"),
             renderScript(theme));
    full.replace("</body>", scripts + "</body>");

    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportError("HTML 写入失败");
        return;
    }
    out.write(full.toUtf8());
    out.close();
    emit exportFinished(true, outputPath);
}
```

- [ ] **Step 6: 在 src/CMakeLists.txt 注册新文件**

在 SOURCES 的 `core/DocxExporter.cpp` 后加 `core/HtmlExporter.cpp`；在 HEADERS 的 `core/DocxExporter.h` 后加 `core/HtmlExporter.h`。

- [ ] **Step 7: 运行测试确认通过**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R test_htmlexporter --output-on-failure`
Expected: 5 个用例全部 PASS。

- [ ] **Step 8: Commit**

```bash
git add src/core/HtmlExporter.h src/core/HtmlExporter.cpp src/CMakeLists.txt tests/test_htmlexporter.cpp tests/CMakeLists.txt
git commit -m "feat: HtmlExporter 自包含 HTML 导出（资源 base64 内联）+ 单元测试

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 3: 接线 HTML 导出（MenuBar + ToolBar + MainWindow）

**Files:**
- Modify: `src/ui/MenuBar.h:23`、`src/ui/MenuBar.cpp:47`
- Modify: `src/ui/ToolBar.h:51`、`src/ui/ToolBar.cpp:257`
- Modify: `src/ui/MainWindow.h:38`、`src/ui/MainWindow.cpp`

**Interfaces:**
- Consumes: `HtmlExporter::exportToHtml(html, docDir, path, theme)`（Task 2）。
- Produces: `MenuBar::exportHtml()`、`ToolBar::htmlExportClicked()` 信号；`MainWindow::onExportHtml()` 槽。

- [ ] **Step 1: MenuBar 增加导出 HTML 菜单项**

`MenuBar.h` 在 `void exportDocx();` 后加一行信号声明：

```cpp
    void exportDocx();
    void exportHtml();
```

`MenuBar.cpp` 在 `exportDocxAct` 连接后（第 48 行后）加：

```cpp
    QAction *exportHtmlAct = exportMenu->addAction("导出 HTML...");
    connect(exportHtmlAct, &QAction::triggered, this, &MenuBar::exportHtml);
```

- [ ] **Step 2: ToolBar 增加导出 HTML 菜单项**

`ToolBar.h` 在 `void docxExportClicked();` 后加：

```cpp
    void docxExportClicked();
    void htmlExportClicked();
```

`ToolBar.cpp` 在 `exportMenu->addAction("导出 DOCX...", ...)` 后（第 257 行后）加：

```cpp
        exportMenu->addAction("导出 HTML...", this, &ToolBar::htmlExportClicked);
```

- [ ] **Step 3: MainWindow 增加 onExportHtml 槽与连接**

`MainWindow.h` 在 `void onExportDocx();` 后加：

```cpp
    void onExportDocx();
    void onExportHtml();
```

`MainWindow.cpp` 顶部 `#include "../core/DocxExporter.h"` 后加：

```cpp
#include "../core/HtmlExporter.h"
```

并新增 `#include <QFileInfo>`（`onExportHtml` 解析文档目录用；若已存在则跳过）。

`connectSignals()` 中，在 `connect(m_menuBar, &MenuBar::exportDocx, ...)` 后加：

```cpp
    connect(m_menuBar, &MenuBar::exportHtml, this, &MainWindow::onExportHtml);
```

在 `connect(m_toolBar, &ToolBar::docxExportClicked, ...)` 后加：

```cpp
    connect(m_toolBar, &ToolBar::htmlExportClicked, this, &MainWindow::onExportHtml);
```

在 `onExportDocx()` 实现之后新增 `onExportHtml()`：

```cpp
void MainWindow::onExportHtml()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;

    QString path = QFileDialog::getSaveFileName(this, "导出 HTML", "document.html",
                                                 "HTML 文件 (*.html)");
    if (path.isEmpty()) return;

    // 文档目录（解析相对图片路径）；未保存文档时为空字符串
    QString docDir = QFileInfo(tab->webView()->property("filePath").toString()).absolutePath();
    QString theme = Application::instance()->themeManager()->currentTheme();

    tab->webView()->page()->runJavaScript(
        "window.editorApi.getContent();",
        [this, path, docDir, theme](const QVariant &result) {
            QString html = result.toString();
            auto *exporter = new HtmlExporter(this);
            connect(exporter, &HtmlExporter::exportFinished, this,
                    [this, exporter](bool ok, const QString &out) {
                m_statusBar->showMessage(ok ? QString("已导出: %1").arg(out) : "导出失败", 3000);
                exporter->deleteLater();
            });
            exporter->exportToHtml(html, docDir, path, theme);
        });
}
```

- [ ] **Step 4: 重新编译**

Run: `cmake --build build -j$(nproc)`
Expected: 编译通过。

- [ ] **Step 5: 手工验收**

Run: `./scripts/run-editor.sh`，打开一个含代码块/表格的文档 → 文件→导出→导出 HTML → 用浏览器打开导出的 .html，确认代码高亮与排版正常。
Expected: 生成 .html，浏览器打开可见完整样式。

- [ ] **Step 6: Commit**

```bash
git add src/ui/MenuBar.h src/ui/MenuBar.cpp src/ui/ToolBar.h src/ui/ToolBar.cpp src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat: 接线 HTML 导出（菜单/工具栏/主窗口）

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 4: 自定义 CSS —— editor.js 注入 + EditorTab + MainWindow 应用

**Files:**
- Modify: `src/resources/web/editor.js`（editorApi 对象内，`setTheme` 附近约 1647 行）
- Modify: `src/ui/EditorTab.h:41`、`src/ui/EditorTab.cpp`（`setEditorWidth` 附近）
- Modify: `src/ui/MainWindow.h:56`、`src/ui/MainWindow.cpp`

**Interfaces:**
- Produces: `editorApi.applyCustomCss(css)`（JS）、`EditorTab::applyCustomCss(const QString &css)`、`MainWindow::applyCustomCss()`。

- [ ] **Step 1: editor.js 新增 applyCustomCss**

在 `setTheme: function(theme) { ... },` 方法后新增：

```js
    applyCustomCss: function(css) {
        var style = document.getElementById('custom-css');
        if (css) {
            if (!style) {
                style = document.createElement('style');
                style.id = 'custom-css';
                document.head.appendChild(style);
            }
            style.textContent = css;
        } else if (style) {
            style.parentNode.removeChild(style);
        }
    },
```

- [ ] **Step 2: EditorTab 新增 applyCustomCss**

`EditorTab.h` 在 `void setEditorWidth(int width);` 后加：

```cpp
    void setEditorWidth(int width);
    void applyCustomCss(const QString &css);
```

`EditorTab.cpp` 在 `setEditorWidth` 实现后新增：

```cpp
void EditorTab::applyCustomCss(const QString &css)
{
    if (m_pageLoaded) {
        QString esc = css;
        esc.replace("\\", "\\\\").replace("'", "\\'").replace("\n", "\\n").replace("\r", "\\r");
        runJsCommand(QString("window.editorApi.applyCustomCss('%1');").arg(esc));
    }
}
```

- [ ] **Step 3: MainWindow 新增 applyCustomCss 并接入两处调用点**

`MainWindow.h` 在 `void applyEditorSettings();` 后加：

```cpp
    void applyEditorSettings();
    void applyCustomCss();
```

`MainWindow.cpp` 在 `applyEditorSettings()` 实现后新增：

```cpp
void MainWindow::applyCustomCss()
{
    QSettings settings("md-editor", "md-editor");
    QString css = settings.value("editor/customCss").toString();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->applyCustomCss(css);
    }
}
```

两处调用点：

1. `onShowSettings()`（第 820-827 行）末尾追加：

```cpp
    applyEditorSettings();
    applyCustomCss();
```

2. `onEditorBridgeCreated()` 中 `pageLoadFinished` lambda（第 429-431 行）改为：

```cpp
        connect(tab, &EditorTab::pageLoadFinished, this,
                [this](bool ok) { if (ok) { applyEditorFont(); applyEditorWidth(); applyEditorSettings(); applyCustomCss(); } });
```

- [ ] **Step 4: 重新编译（editor.js 为 qrc 资源，必须重编）**

Run: `cmake --build build -j$(nproc)`
Expected: 编译通过。

- [ ] **Step 5: Commit**

```bash
git add src/resources/web/editor.js src/ui/EditorTab.h src/ui/EditorTab.cpp src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat: 自定义 CSS 注入编辑器（editorApi.applyCustomCss + EditorTab + MainWindow）

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 5: 自定义 CSS —— 设置 UI + PDF 导出应用

**Files:**
- Modify: `src/ui/SettingsDialog.h`（成员 + `#include <QPlainTextEdit>`）、`src/ui/SettingsDialog.cpp`
- Modify: `src/core/PdfExporter.cpp`

**Interfaces:**
- Consumes: `editor/customCss`（QSettings 键，Task 4 已消费同键）。
- Produces: 设置对话框自定义 CSS 编辑框；`PdfExporter` 导出时内联 customCss。

- [ ] **Step 1: SettingsDialog.h 增加成员**

在 `#include <QFontComboBox>` 后加 `#include <QPlainTextEdit>`；在「外观」成员区（`QComboBox *m_langCombo;` 后）加：

```cpp
    QComboBox *m_langCombo;
    QPlainTextEdit *m_customCssEdit;
```

- [ ] **Step 2: SettingsDialog.cpp setupUi 增加编辑框**

在 `appearanceForm->addRow("界面语言:", m_langCombo);` 后加：

```cpp
    m_customCssEdit = new QPlainTextEdit();
    m_customCssEdit->setPlaceholderText("/* 可选：覆盖编辑器与导出的默认样式 */");
    m_customCssEdit->setMinimumHeight(120);
    appearanceForm->addRow("自定义 CSS:", m_customCssEdit);
```

- [ ] **Step 3: SettingsDialog.cpp loadSettings/onAccepted 读写**

`loadSettings()` 末尾（`m_themeCombo->setCurrentIndex(...)` 后）加：

```cpp
    m_customCssEdit->setPlainText(settings.value("editor/customCss").toString());
```

`onAccepted()` 中 `settings.setValue("editor/width", ...)` 后加：

```cpp
    settings.setValue("editor/customCss", m_customCssEdit->toPlainText());
```

- [ ] **Step 4: PdfExporter.cpp 内联 customCss**

在 `exportToPdf` 开头读取 margins 之后（约第 34 行 `int paper = ...` 后）加：

```cpp
    QString customCss = settings.value("editor/customCss").toString();
```

在 `fullHtml` 构建完成后、`m_pendingOutputPath = outputPath;` 之前加：

```cpp
    if (!customCss.isEmpty()) {
        fullHtml.replace("</head>", QString("<style>%1</style></head>").arg(customCss));
    }
```

- [ ] **Step 5: 重新编译**

Run: `cmake --build build -j$(nproc)`
Expected: 编译通过。

- [ ] **Step 6: 手工验收**

Run: `./scripts/run-editor.sh` → 帮助→设置→外观→自定义 CSS 输入 `body{font-family:monospace;}` → 确定，观察编辑器字体变为等宽；导出 HTML 与 PDF，确认样式已应用。
Expected: 编辑器、HTML、PDF 三者样式均被覆盖。

- [ ] **Step 7: Commit**

```bash
git add src/ui/SettingsDialog.h src/ui/SettingsDialog.cpp src/core/PdfExporter.cpp
git commit -m "feat: 设置新增自定义 CSS 并应用于 PDF 导出

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 6: 文档记账清理（CLAUDE.md / README / CHANGELOG）

**Files:**
- Modify: `CLAUDE.md`
- Modify: `README.md`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: CLAUDE.md 更新「已知问题 & 待完善」**

将当前四条（PDF 未接线 / 搜索需对接 / 数学二阶段 / esbuild）替换为：

```markdown
## 已知问题 & 待完善

- PDF 导出页眉页脚/页码尚未实现（`PdfExporter` 已支持页边距与方向）
- 自动保存已被移除（0.2.1，因未完整接线；会话恢复仍可用）
- 标签页交互待补全：中键关闭、Ctrl+1~9 跳转、右键批量关闭
- 大纲光标自动高亮待实现（当前仅点击跳转）
- 界面语言 English 为占位（设置已有下拉，未接入 QTranslator）
- `tiptap-all.js` 更新需在 `src/resources/web/` 下运行 `npx esbuild tiptap-entry.js --bundle --outfile=tiptap-all.js --format=iife`
```

在「关键文件」表 `| `src/core/MarkdownHandler.cpp` | ... |` 行之前，将 `FileManager` 行描述从 `文件 I/O + 自动保存 + 文件监控` 改为 `文件 I/O + 文件监控`，并在表格末尾新增两行：

```markdown
| `src/core/DocxExporter.cpp` | DOCX 导出（基于 QTextDocument + zip） |
| `src/core/HtmlExporter.cpp` | 自包含 HTML 导出（资源 base64 内联） |
```

- [ ] **Step 2: README.md 修正功能清单**

将：

```markdown
- **文件管理** — 左侧文件树浏览，多标签页编辑，自动保存
```

改为：

```markdown
- **文件管理** — 左侧文件树浏览，多标签页编辑
```

将：

```markdown
- **PDF 导出** — 基于 Chromium 渲染引擎的高质量输出
```

改为：

```markdown
- **导出** — PDF（Chromium 渲染）、DOCX、自包含 HTML 单文件
```

- [ ] **Step 3: CHANGELOG.md 补录 DOCX 导出**

在 `## [0.3.10] — 2026-08-12` 的「### 新增」列表末尾补两条（补录历史遗漏）：

```markdown
- **DOCX 导出** — 文件→导出→导出 DOCX，基于 QTextDocument 解析 + zip 打包生成 Word 文档
- **导出按钮下拉菜单** — 工具栏「导出」折叠为 PDF/DOCX 下拉（后扩展 HTML）
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md README.md CHANGELOG.md
git commit -m "docs: 清理过时记账（CLAUDE.md 已知问题 / README 自动保存 / DOCX 补录）

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 7: 版本推进 0.8.0 + 打 tag

**Files:**
- Modify: `CMakeLists.txt:2`（`project(md-editor VERSION 0.7.0 ...)` → `0.8.0`）
- Modify: `CHANGELOG.md`（顶部新增 0.8.0 条目）

- [ ] **Step 1: 推进 CMakeLists.txt 版本号**

`project(md-editor VERSION 0.7.0 LANGUAGES CXX)` → `project(md-editor VERSION 0.8.0 LANGUAGES CXX)`。

- [ ] **Step 2: CHANGELOG.md 顶部新增 0.8.0 条目**

在 `# Changelog` 说明块之后、`## [0.7.0]` 之前插入：

```markdown
## [0.8.0] — 2026-08-15

本次为导出增强大版本：新增自包含 HTML 导出与自定义 CSS 样式，并清理项目记账。

### 新增

- **HTML 导出** — 文件/工具栏「导出 HTML」，产出单文件自包含 HTML：KaTeX/Mermaid/highlight.js 及图片全部 base64 内联，双击浏览器离线打开即完整渲染（数学公式/图表/代码高亮/图片），跟随当前明暗主题
- **自定义 CSS** — 设置「外观」新增自定义 CSS 编辑框，注入编辑器实时生效，并在 HTML/PDF 导出时一并应用（覆盖默认样式）
- **highlight.min.js 纳入 qrc** — 供 HTML 导出内联代码高亮

### 变更

- **文档记账清理** — CLAUDE.md「已知问题」移除已实现项（PDF 导出/搜索/数学公式）；README 移除已删除的自动保存；CHANGELOG 补录 DOCX 导出
```

- [ ] **Step 3: 全量重编译 + 运行全部测试**

Run: `cmake --build build -j$(nproc) && cd build && ctest --output-on-failure`
Expected: 全部测试通过（含 test_htmlexporter）。

- [ ] **Step 4: 确认二进制时间戳新于所有改动源文件**

Run: `stat -c '%Y %n' build/src/md-editor src/core/HtmlExporter.cpp src/resources/web/editor.js` 核对可执行文件时间戳最新。

- [ ] **Step 5: 提交 + 打 tag**

```bash
git add CMakeLists.txt CHANGELOG.md
git commit -m "chore: 版本推进至 0.8.0

Co-Authored-By: Claude <noreply@anthropic.com>"
git tag v0.8.0
```

---

## Self-Review 备注

- **规格覆盖**：设计规格 §2（HTML 导出）→ Task 1/2/3；§3（自定义 CSS）→ Task 4/5；§4（文档清理）→ Task 6；版本推进 → Task 7。全覆盖。
- **类型一致**：`HtmlExporter::exportToHtml(html, docDir, outputPath, theme)` 签名在 Task 2 定义、Task 3 消费一致；`applyCustomCss`（JS/EditorTab/MainWindow）三处命名一致；`editor/customCss` 键在 Task 2/4/5 一致。
- **范围**：单一实施计划，7 个任务各自可独立测试/提交。
