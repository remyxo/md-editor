# PDF 页眉页脚、脚注增强与会话恢复守卫 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按 `docs/superpowers/specs/2026-09-06-pdf-header-footer-footnote-design.md` 实现 PDF 页眉页脚/页码（paged.js 分页引擎）、脚注四项增强（块级定义/跳转互导/hover 气泡/内联改名）、会话恢复大文件软警告、ESC 文档补录。

**Architecture:** PDF 侧在 `PdfExporter` 渲染页注入 vendored paged.js，@page margin boxes 承载页眉标题与页码 counter，`printToPdf` 改为 0 边距整页输出；页眉页脚全关时走原管线零回归。脚注侧 `MarkdownHandler` 双向升级为 pandoc 式缩进续行（嵌套复用主管线），editor.js schema `footnoteDef` 升 `block+` 并加跳转/气泡/改名弹层交互。会话恢复抽 `confirmLargeFile` helper 复用软警告。

**Tech Stack:** C++17 / Qt 6.8（本地）· Qt 6.10.2（CI 基准）· QtWebEngine printToPdf · paged.js（vendored）· TipTap v3 / ProseMirror · Qt Test + Vitest（jsdom）

## Global Constraints

- 所有注释/提交信息中文；提交前缀 `feat:` / `fix:` / `chore:` / `docs:`
- **改任何 C++ 源码或 qrc 内文件（`src/resources/web/` 的 .js/.css、themes/ 的 .qss）后必须 `cmake --build build` 再验证**——qrc 打进二进制，不重编译跑的是旧版
- 本批**不推进版本号、不改 CHANGELOG**（版本收口时统一处理，见 spec §5）
- **不 push**（用户明确要求时才 push，push 走 pre-push 预检）
- 构建/测试命令：`cmake --build build -j$(nproc)` → `cd build && ctest --output-on-failure`；JS 测试：`cd src/resources/web && npx vitest run`
- E2E 临时文件一律放 `/tmp/md-editor-claude-2026-09-06/`（已存在，勿按进程名杀 md-editor——按 `/proc/$p/environ` 含 `md-editor-claude` 判定）
- UI 可见行为类任务（Task 1/5/8/9/10）**须用户目视验证通过后才 commit**；纯逻辑+单测任务（4/6/7）测试全绿即可 commit；纯文档/资源任务（2/3/11）直接 commit
- 编辑区往返保真红线：新转换逻辑不得丢失用户语义信息；占位符一律 `collisionFreePlaceholder` 模式带序号生成
- Qt 兼容基准 6.10.2（最低 6.8.2）：本批新逻辑不依赖任何版本相关的 `toMarkdown()`/`setHtml()` 转义行为

---

### Task 1: 会话恢复大文件软警告（confirmLargeFile helper）

**Files:**
- Modify: `src/ui/MainWindow.h`（private 方法声明区）
- Modify: `src/ui/MainWindow.cpp:959-975`（onFileDoubleClicked 内联警告块抽离）、`MainWindow.cpp:1261`（restoreSession 循环）
- Test: 无单测骨架（MainWindow 无测试目标），手工 E2E

**Interfaces:**
- Produces: `bool MainWindow::confirmLargeFile(const QString &path)` —— 返回 false 表示用户拒绝打开；Task 无后续依赖

- [ ] **Step 1: MainWindow.h 声明**

在 `MainWindow.h` private 段（`void restoreSession();` 附近）加：

```cpp
    // ≥2MB 大文件软警告（手动打开与会话恢复共用）：true = 继续打开，false = 用户拒绝
    bool confirmLargeFile(const QString &path);
```

- [ ] **Step 2: MainWindow.cpp 实现 helper（onFileDoubleClicked 前合适位置）**

```cpp
// ≥2MB 大文件软警告（手动打开与会话恢复共用）。
// 仅 2MB~10MB 且非二进制时提示：超上限 readFile 必拒、二进制有专门报错，
// 先问「是否仍要打开」只会造成双重弹窗
bool MainWindow::confirmLargeFile(const QString &path)
{
    const qint64 size = QFileInfo(path).size();
    if (size < FileManager::kLargeFileWarnBytes || size > FileManager::kMaxReadBytes
        || FileManager::isBinaryFile(path))
        return true;
    QMessageBox box(QMessageBox::Question, tr("文件较大"),
                    tr("文件 %1 大小为 %2 MB，打开后可能明显卡顿。\n是否仍要打开？")
                        .arg(path, QString::number(size / 1024.0 / 1024.0, 'f', 1)),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.button(QMessageBox::Yes)->setText(tr("是"));
    box.button(QMessageBox::No)->setText(tr("否"));
    return box.exec() == QMessageBox::Yes;
}
```

- [ ] **Step 3: onFileDoubleClicked 替换内联块**

`MainWindow.cpp:961-975` 的注释与 `if (size >= ...)` 整块替换为：

```cpp
    // 软警告层：2MB~10MB 的大文件经确认后才打开（与会话恢复共用 confirmLargeFile）
    if (!confirmLargeFile(path))
        return;
```

- [ ] **Step 4: restoreSession 循环接入**

`MainWindow.cpp:1261` 的 `for (const QString &path : restoreList) {` 内、`QString encoding;` 之前插入：

```cpp
        // 大文件软警告与会话恢复一致：拒绝则跳过该文件（不开标签/不 watch/不进最近）
        if (!confirmLargeFile(path))
            continue;
```

- [ ] **Step 5: 编译 + 全量回归**

```bash
cmake --build build -j$(nproc) && cd build && ctest --output-on-failure
```
Expected: 编译零错误，ctest 全部通过（既有用例无回归）。

- [ ] **Step 6: 手工 E2E（用隔离 HOME 启动）**

```bash
head -c 2621440 /dev/zero | tr '\0' 'x' > /tmp/md-editor-claude-2026-09-06/big-2.5mb.md
HOME=/tmp/md-editor-claude-2026-09-06/home QT_QPA_PLATFORM=xcb build/src/md-editor &
```
验证：① 正常打开小文件路径行为不变（双击文件树/拖放无弹窗）；② `big-2.5mb.md` 加入会话后重启——启动即弹「文件较大」框；③ 点「否」→ 该文件不恢复（无空标签），活动标签定位到其余文件/空白页；④ 点「是」→ 正常加载。⑤ 双击打开 2.5MB 文件仍弹同款框（回归）。

- [ ] **Step 7: 用户目视验证通过后提交**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "fix: 会话恢复路径补 ≥2MB 大文件软警告，与手动打开共用 confirmLargeFile"
```

---

### Task 2: ESC 机制文档补录（emoji/代码语言/脚注弹层）

**Files:**
- Modify: `docs/modal-dialog-esc-handling.md`（在 `## 教训` 一节之前插入新节）

**Interfaces:** 无代码接口；纯文档。

- [ ] **Step 1: 插入新节**

在 `## 教训` 标题前插入：

```markdown
---

## 浮层复用与语义放宽（2026-09-06 增补）

`modalDialogStateChanged` 标志的实际使用者已超出当初的「模态编辑对话框」，覆盖编辑器内全部阻塞型浮层：

| 浮层 | 打开入口 | 备注 |
|------|---------|------|
| 超链接编辑器 `_linkEditor` | 右键菜单 / 工具栏 | 原始设计内 |
| Mermaid 编辑器 `_mermaidEditor` | 工具栏插入 | 原始设计内 |
| 数学公式编辑器 `_mathEditor` | 工具栏插入 | 原始设计内 |
| emoji 选择器 `_emojiPicker` | 工具栏 | 复用此标记（2026-08-09 emoji spec 待办补录） |
| 代码语言选择器 `_codeLangPicker` | 代码块 | 同上 |
| 脚注改名弹层 `_footnoteEditor` | 双击脚注引用 | 2026-09-06 新增（脚注增强 spec §2.5） |

变量语义从「模态对话框打开」放宽为「编辑器内阻塞型浮层打开」；C++ 侧 `m_modalDialogOpen` 与 Esc 分流逻辑不变。

**新增浮层接入清单**：`open()` 时 `bridge.modalDialogChanged(true)`、`close()` 时传 `false`；并把 `close()` 调用加入 `MainWindow` Esc 链的 `runJavaScript` 串（`MainWindow.cpp:134-138`）。

**非阻塞浮层不占用此标志**：如脚注 hover 气泡——随鼠标离开即消失的瞬时预览，Esc 不应关闭它。
```

- [ ] **Step 2: 提交**

```bash
git add docs/modal-dialog-esc-handling.md
git commit -m "docs: ESC 模态机制补录 emoji/代码语言/脚注弹层复用与语义放宽"
```

---

### Task 3: vendor paged.js

**Files:**
- Create: `src/resources/web/paged.polyfill.js`（dist 原样落盘）
- Create: `docs/vendor-pagedjs.md`
- Modify: `src/resources/app.qrc:139`（mermaid.min.js 行后）

**Interfaces:**
- Produces: qrc 资源 `:/web/paged.polyfill.js`，暴露全局 `window.PagedPolyfill`（`PagedConfig={auto:false}` 时需手动调 `PagedPolyfill.preview()`，返回 Promise）——Task 4/5 依赖

- [ ] **Step 1: 取包并落盘（不污染 package.json）**

```bash
cd /home/daijd/claude-workspace/md-editor/src/resources/web
VER=$(npm view pagedjs version)   # 记下该版本号，写入 vendor 文档
npm pack "pagedjs@$VER"
tar xf "pagedjs-$VER.tgz"
cp package/dist/paged.polyfill.js .
rm -rf package "pagedjs-$VER.tgz"
head -c 200 paged.polyfill.js     # 目视确认非 HTML 错误页
grep -c "PagedPolyfill" paged.polyfill.js   # 应 ≥1（全局入口存在）
```

- [ ] **Step 2: 注册 qrc**

`src/resources/app.qrc` 的 `<file>web/mermaid.min.js</file>` 行后加：

```xml
        <file>web/paged.polyfill.js</file>
```

- [ ] **Step 3: 写 vendor 文档**（照 `docs/vendor-lowlight-plugin.md` 结构）

`docs/vendor-pagedjs.md` 内容要点：① 来源 `https://www.npmjs.com/package/pagedjs`，dist 文件 `paged.polyfill.js` 原样落盘（版本 `X.Y.Z`——填 Step 1 的 `$VER`）；② 用途：PDF 导出分页引擎（CSS Paged Media polyfill，@page margin boxes 承载页眉/页脚/页码，见 2026-09-06 spec §1）；③ 同步/升级流程：`npm view pagedjs version` → `npm pack` → 替换文件 → 跑 Task 5 的 E2E 清单；④ 与上游零差异（纯 dist，无本地补丁）；⑤ MIT 协议；⑥ **已知限制（spec §1.7 记入）**：用户自定义 CSS 含 `@page` 规则时可与本机制冲突（用户自担）；超一页高的不可断元素（长图/长代码块）在分页引擎下溢出裁切（与现状一致）。

- [ ] **Step 4: 重编译验证资源入包**

```bash
cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc)
```
Expected: 编译零错误（qrc 新文件被 rcc 打包）。测试目标不链 qrc，资源存在性由 Task 5 E2E 最终验证。

- [ ] **Step 5: 提交**

```bash
git add src/resources/web/paged.polyfill.js src/resources/app.qrc docs/vendor-pagedjs.md
git commit -m "chore: vendor paged.js 分页引擎（PDF 页眉页脚用），入 qrc 并建档"
```

---

### Task 4: PdfPageSpec 与 buildRenderHtml 分页注入（TDD）

**Files:**
- Modify: `src/core/PdfExporter.h`
- Modify: `src/core/PdfExporter.cpp:23-85`
- Test: `tests/test_pdfexporter.cpp`（追加用例）

**Interfaces:**
- Produces:
  - `struct PdfPageSpec { QPageSize::PageSizeId paper = QPageSize::A4; QPageLayout::Orientation orientation = QPageLayout::Portrait; QMarginsF marginsMm{18,18,18,18}; bool headerEnabled = false; bool footerEnabled = false; QString headerTitle; bool pagedActive() const; }`（PdfExporter.h）
  - `static QString buildRenderHtml(const QString &html, const QString &docDir, const QString &katexCss, const QString &customCss, const PdfPageSpec &spec)`（新重载；旧 4 参签名保留、委托给 `PdfPageSpec()` 默认值——既有测试不动）
  - 渲染页始终注入 `window.__preparePrint = function(){...}`（Promise，settle 即打印前置完成；paged 激活时内部链 `PagedPolyfill.preview()`）——Task 5 的 `onLoadFinished` 调用它

- [ ] **Step 1: 写失败测试**（`tests/test_pdfexporter.cpp`，类内追加）

```cpp
    PdfPageSpec specWithHeader() {
        PdfPageSpec s;
        s.headerEnabled = true; s.footerEnabled = true;
        s.headerTitle = QStringLiteral("我的文档");
        return s;
    }

    void testBuildRenderHtmlPagedInjectsPageRules() {
        PdfPageSpec s = specWithHeader();
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("@top-center"));
        QVERIFY(out.contains("我的文档"));
        QVERIFY(out.contains("@bottom-right"));
        QVERIFY(out.contains("counter(page)"));
        QVERIFY(out.contains("counter(pages)"));
        QVERIFY(out.contains("paged.polyfill.js"));
        QVERIFY(out.contains("PagedConfig={auto:false}"));
        QVERIFY(out.contains("window.__preparePrint"));
        // paged 激活时不得再有旧 @page{margin:0}（后位级联会覆盖新 margin）
        QVERIFY(!out.contains("@page{margin:0;}"));
    }

    void testBuildRenderHtmlLegacyPathUnchanged() {
        // 页眉页脚全关 → 不引入 paged.js，走原管线（@page{margin:0} 保留）
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "");
        QVERIFY(!out.contains("paged.polyfill.js"));
        QVERIFY(out.contains("@page{margin:0;}"));
        QVERIFY(out.contains("window.__preparePrint"));   // 打印前置入口两种路径都在
    }

    void testBuildRenderHtmlHeaderOnly() {
        PdfPageSpec s = specWithHeader(); s.footerEnabled = false;
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("@top-center"));
        QVERIFY(!out.contains("@bottom-right"));
        QVERIFY(out.contains("paged.polyfill.js"));
    }

    void testBuildRenderHtmlPaperOrientationMargins() {
        PdfPageSpec s = specWithHeader();
        s.paper = QPageSize::A5;
        s.orientation = QPageLayout::Landscape;
        s.marginsMm = QMarginsF(15, 20, 15, 22);   // 左上右下（QMarginsF 语义）
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("size:A5 landscape"));
        // @page margin 顺序 = 上 右 下 左：20mm 15mm 22mm 15mm
        QVERIFY(out.contains("margin:20mm 15mm 22mm 15mm"));
    }

    void testBuildRenderHtmlTitleEscaped() {
        PdfPageSpec s = specWithHeader();
        s.headerTitle = QStringLiteral("a\"b\\c\nd");
        QString out = PdfExporter::buildRenderHtml("<p>x</p>", "/tmp", "", "", s);
        QVERIFY(out.contains("a\\\"b\\\\c d"));   // " → \"，\ → \\，换行 → 空格
    }
```

注意：`QMarginsF(15, 20, 15, 22)` 构造顺序为（左，上，右，下）；@page margin CSS 输出顺序为（上，右，下，左）。文件头已 include `PdfExporter.h`（带 QPageLayout/QPageSize include）。

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build -j$(nproc) && cd build && ctest -R test_pdfexporter --output-on-failure
```
Expected: FAIL——`PdfPageSpec` 未定义（编译错误即预期失败）。

- [ ] **Step 3: PdfExporter.h 加 struct 与重载声明**

`class PdfExporter` 定义前加：

```cpp
// PDF 分页规格（buildRenderHtml 纯字符串路径的参数载体，exportToPdf 从 QSettings 组装）
struct PdfPageSpec {
    QPageSize::PageSizeId paper = QPageSize::A4;
    QPageLayout::Orientation orientation = QPageLayout::Portrait;
    QMarginsF marginsMm{18, 18, 18, 18};   // 左 上 右 下（mm）
    bool headerEnabled = false;            // @top-center 文档标题
    bool footerEnabled = false;            // @bottom-right 第 X 页 / 共 Y 页
    QString headerTitle;
    // 页眉/页脚任一启用即激活 paged.js 分页（全关走原 Chromium 分页管线）
    bool pagedActive() const { return headerEnabled || footerEnabled; }
};
```

类内 `buildRenderHtml` 声明区追加：

```cpp
    // 分页版：@page 规则 + paged.js 注入（spec.pagedActive() 时）
    static QString buildRenderHtml(const QString &html, const QString &docDir,
                                   const QString &katexCss, const QString &customCss,
                                   const PdfPageSpec &spec);
```

- [ ] **Step 4: PdfExporter.cpp 实现**

文件内（buildRenderHtml 上方）加转义 helper，并重写函数：

```cpp
// CSS content 字符串转义：\ 与 "；换行替换为空格（页眉单行）
static QString cssContentString(QString text)
{
    text.replace('\\', QStringLiteral("\\\\"));
    text.replace('"', QStringLiteral("\\\""));
    text.replace('\n', QLatin1Char(' '));
    text.replace('\r', QLatin1Char(' '));
    return text;
}

QString PdfExporter::buildRenderHtml(const QString &html, const QString &docDir,
                                     const QString &katexCss, const QString &customCss)
{
    return buildRenderHtml(html, docDir, katexCss, customCss, PdfPageSpec());
}

QString PdfExporter::buildRenderHtml(const QString &html, const QString &docDir,
                                     const QString &katexCss, const QString &customCss,
                                     const PdfPageSpec &spec)
{
    // 图片 base64 内联（相对路径相对 docDir 解析；绝对路径原样；读不到保留 src）
    QString body = HtmlExporter::inlineImages(html, docDir);

    const bool paged = spec.pagedActive();
    // @page 规则（paged 激活时）：置于 @media print 之外——paged.js 在屏幕媒体
    // 上解析分页；margin 顺序 CSS = 上 右 下 左
    QString pageCss;
    if (paged) {
        QString sizeStr = QPageSize(spec.paper).key();   // "A4"/"Letter"/"A5"…
        if (spec.orientation == QPageLayout::Landscape)
            sizeStr += QLatin1String(" landscape");
        pageCss = QStringLiteral("@page{size:%1;margin:%2mm %3mm %4mm %5mm;")
                      .arg(sizeStr,
                           QString::number(spec.marginsMm.top()),
                           QString::number(spec.marginsMm.right()),
                           QString::number(spec.marginsMm.bottom()),
                           QString::number(spec.marginsMm.left()));
        if (spec.headerEnabled)
            pageCss += QStringLiteral(
                           "@top-center{content:\"%1\";font-size:9pt;color:#666;}")
                           .arg(cssContentString(spec.headerTitle));
        if (spec.footerEnabled)
            pageCss += QStringLiteral(
                           "@bottom-right{content:\"第 \" counter(page) \" 页 / 共 \" "
                           "counter(pages) \" 页\";font-size:9pt;color:#666;}");
        pageCss += QLatin1Char('}');
    }
    // 旧 @page{margin:0}（Qt QPageLayout 边距方案）仅 legacy 路保留；
    // paged 路径下若同存，@media print 内的后位规则会级联覆盖新 margin
    const QString legacyPageZero = paged ? QString()
                                         : QStringLiteral("@page{margin:0;}");

    // 打印前置入口：mermaid 先渲染（SVG 定型），paged 后跑（分页测量正确）；
    // 双 settle 防挂起（沿用 __renderAllMermaid 的 .then/.catch 均真模式）
    const QString preparePrint = QStringLiteral(
        "window.__preparePrint=function(){"
        "return window.__renderAllMermaid()"
        ".then(function(){if(!window.PagedPolyfill)return true;"
        "return PagedPolyfill.preview().then(function(){return true;})"
        ".catch(function(){return true;});})"
        ".catch(function(){return true;});};");
    const QString pagedScripts = paged
        ? QStringLiteral("<script>window.PagedConfig={auto:false};</script>"
                         "<script src=\"paged.polyfill.js\"></script>")
        : QString();

    QString fullHtml = QString(
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><style>"
        "%1"                                    // @page 分页规则（paged 激活时）
        "@media print{"
        "%2"                                    // @page{margin:0}（仅 legacy）
        "body{font-family:\"Noto Sans SC\",\"DejaVu Sans\",sans-serif;"
        "font-size:11pt;line-height:1.6;color:#333;}"
        "h1{font-size:1.7em;margin-top:1em;margin-bottom:0.3em;}"
        "h2{font-size:1.3em;margin-top:0.8em;margin-bottom:0.2em;}"
        "h3{font-size:1.1em;margin-top:0.6em;margin-bottom:0.2em;}"
        "p{margin-top:0;margin-bottom:0.3em;}"
        "pre{background:#f4f5f7;padding:10px 14px;border-radius:4px;"
        "font-family:monospace;font-size:9.5pt;line-height:1.35;overflow-x:auto;margin:0.5em 0;}"
        "code{background:#f0f0f0;padding:1px 3px;border-radius:2px;font-size:0.9em;}"
        "table{border-collapse:collapse;width:100%%;margin:0.5em 0;}"
        "th,td{border:1px solid #ccc;padding:4px 8px;font-size:10pt;}"
        "th{background:#f5f5f5;}"
        "img{max-width:100%%;}"
        "blockquote{border-left:3px solid #0078d4;padding:4px 12px;"
        "color:#555;margin:0.5em 0;}"
        "[data-type=\"block-math\"]{display:block;text-align:center;margin:1em 0;}"
        "ul[data-type=\"taskList\"]{list-style:none;padding-left:0;}"
        // 子代选择器 > 限定直接 li：嵌套在任务项内的通用列表 li（如 列表2-2 下再嵌 <ol>）
        // 若也命中 flex 会被当成 flex 容器，其子元素 p 与嵌套 <ol> 排成一行、序号消失（与 editor.css 对齐）
        "ul[data-type=\"taskList\"] > li{display:flex;align-items:flex-start;gap:8px;}"
        "ul[data-type=\"taskList\"] > li label{margin-top:3px;}"
        "ul[data-type=\"taskList\"] > li > div > p{margin:0;}"
        "}"
        "%3"                                    // KaTeX CSS
        "</style>"
        "%4"                                    // PagedConfig + polyfill（paged 时）
        "<script src=\"katex.min.js\"></script>"
        "<script src=\"mermaid.min.js\"></script>"
        "<script>"
        "%5"                                    // __preparePrint
        "window.addEventListener('DOMContentLoaded',function(){"
        "var els=document.querySelectorAll('[data-latex]');"
        "for(var i=0;i<els.length;i++){"
        "var el=els[i];"
        "var latex=el.getAttribute('data-latex');"
        "if(!latex)continue;"
        "try{"
        "katex.render(latex,el,{throwOnError:false,displayMode:el.dataset.type==='block-math'});"
        "}catch(e){el.textContent=latex;}"
        "}"
        "});"
        "</script>"
        "</head><body>%6</body></html>"
    ).arg(pageCss, legacyPageZero, katexCss, pagedScripts, preparePrint, body);

    // 注入 mermaid 渲染 helper（定义 window.__renderAllMermaid，返回 Promise，
    // 不自调用——__preparePrint 显式链式调用并等待）。PDF 固定亮色打印。
    fullHtml.replace("</head>",
        QString("<script>%1</script></head>").arg(HtmlExporter::mermaidRenderScript("default")));

    if (!customCss.isEmpty()) {
        fullHtml.replace("</head>", QString("<style>%1</style></head>").arg(customCss));
    }
    return fullHtml;
}
```

注意：原函数中 `QString fullHtml = QString(...) .arg(body, katexCss)` 的两参 `.arg` 换成上面的六参 `.arg`（`%%` 转义保留给 width:100%%——它属 format 序列转义，与 .arg 参数个数无关，维持原样）。KaTeX 的 DOMContentLoaded 渲染在 polyfill 脚本之后声明，但 paged.js 手动模式（auto:false）不在 load 时运行，`__preparePrint` 串行保证 KaTeX（同步）→ mermaid → 分页的顺序。

- [ ] **Step 5: 跑测试确认全绿**

```bash
cmake --build build -j$(nproc) && cd build && ctest -R test_pdfexporter --output-on-failure && ctest --output-on-failure
```
Expected: test_pdfexporter 新旧用例全过；全量 ctest 无回归。

- [ ] **Step 6: 提交**

```bash
git add src/core/PdfExporter.h src/core/PdfExporter.cpp tests/test_pdfexporter.cpp
git commit -m "feat: buildRenderHtml 支持 PdfPageSpec——@page 页眉页脚规则与 paged.js 注入（全关走原管线）"
```

---

### Task 5: exportToPdf 分页管线 + 设置开关 + E2E

**Files:**
- Modify: `src/core/PdfExporter.cpp:87-145`（exportToPdf / onLoadFinished）
- Modify: `src/ui/SettingsDialog.h`（成员）、`src/ui/SettingsDialog.cpp`（setupUi `exportLayout->addStretch()` 前 :196、loadSettings :291-294 后、saveSettings :325-328 后、retranslateUi :238 后）
- Test: 手工 E2E（分页为渲染行为，单测不可覆盖）

**Interfaces:**
- Consumes: Task 3 的 `:/web/paged.polyfill.js`、Task 4 的 `PdfPageSpec`/`buildRenderHtml(...spec)`/`window.__preparePrint`
- Produces: QSettings 键 `export/pdfHeaderEnabled`（默认 true）、`export/pdfFooterEnabled`（默认 true）

- [ ] **Step 1: SettingsDialog.h 成员**

private 段（`m_marginRight` 附近）加：

```cpp
    QCheckBox *m_pdfHeaderCheck = nullptr;
    QCheckBox *m_pdfFooterCheck = nullptr;
```

- [ ] **Step 2: setupUi 加开关**（`exportLayout->addStretch();` 之前）

```cpp
    // PDF 页眉/页脚（paged.js 分页引擎，任一开启即启用——见 2026-09-06 spec §1）
    m_pdfHeaderCheck = new QCheckBox(exportTab);
    m_pdfFooterCheck = new QCheckBox(exportTab);
    exportLayout->addWidget(m_pdfHeaderCheck);
    exportLayout->addWidget(m_pdfFooterCheck);
```

- [ ] **Step 3: retranslateUi 文本**（`m_orientGroup->setTitle` 附近）

```cpp
    m_pdfHeaderCheck->setText(tr("PDF 页眉（文档标题）"));
    m_pdfFooterCheck->setText(tr("PDF 页码（第 X 页 / 共 Y 页）"));
```

- [ ] **Step 4: loadSettings / saveSettings**

loadSettings 边距四行后加：

```cpp
    m_pdfHeaderCheck->setChecked(
        settings.value("export/pdfHeaderEnabled", true).toBool());
    m_pdfFooterCheck->setChecked(
        settings.value("export/pdfFooterEnabled", true).toBool());
```

saveSettings 边距四行后加：

```cpp
    settings.setValue("export/pdfHeaderEnabled", m_pdfHeaderCheck->isChecked());
    settings.setValue("export/pdfFooterEnabled", m_pdfFooterCheck->isChecked());
```

- [ ] **Step 5: exportToPdf 组装 spec + 0 边距打印**

`exportToPdf` 中：读设置区（`QString customCss = ...` 行后）加：

```cpp
    // 页眉页脚规格（任一启用激活 paged.js 分页；标题取导出文件名基名——
    // 用户在保存框确认的名字，可预期，不猜文档首个 H1）
    PdfPageSpec spec;
    spec.paper = static_cast<QPageSize::PageSizeId>(paper);
    spec.orientation = static_cast<QPageLayout::Orientation>(orient);
    spec.marginsMm = QMarginsF(ml * 10, mt * 10, mr * 10, mb * 10);   // 左上右下 mm
    spec.headerEnabled = settings.value("export/pdfHeaderEnabled", true).toBool();
    spec.footerEnabled = settings.value("export/pdfFooterEnabled", true).toBool();
    spec.headerTitle = QFileInfo(outputPath).completeBaseName();
```

`QString fullHtml = buildRenderHtml(html, docDir, katexCss, customCss);` 改为：

```cpp
    QString fullHtml = buildRenderHtml(html, docDir, katexCss, customCss, spec);
```

`m_pendingLayout = QPageLayout(...)` 构造改为：

```cpp
    // paged 路径边距由 @page 承载（页面盒自带页边距），Qt 布局传 0 防双重边距；
    // legacy 路径维持 Qt QPageLayout 边距
    m_pendingLayout = QPageLayout(QPageSize(static_cast<QPageSize::PageSizeId>(paper)),
        static_cast<QPageLayout::Orientation>(orient),
        spec.pagedActive() ? QMarginsF(0, 0, 0, 0)
                           : QMarginsF(ml * 10, mt * 10, mr * 10, mb * 10),
        QPageLayout::Millimeter);
```

`onLoadFinished` 中 mermaid 直调改为统一前置入口：

```cpp
void PdfExporter::onLoadFinished(bool ok)
{
    if (!ok) {
        emit exportError("HTML 渲染失败");
        return;
    }
    // 打印前置：KaTeX（同步，已完成）→ mermaid 渲染 → paged.js 分页（任一启用时）。
    // __preparePrint 双 settle（.then/.catch 均真），保证 printToPdf 必定触发，
    // 避免 PDF 导出静默挂起
    m_renderView->page()->runJavaScript(
        "window.__preparePrint()",
        [this](const QVariant &) {
            m_renderView->page()->printToPdf(m_pendingOutputPath, m_pendingLayout);
        });
}
```

- [ ] **Step 6: 编译 + 全量回归**

```bash
cmake --build build -j$(nproc) && cd build && ctest --output-on-failure
```
Expected: 全绿。

- [ ] **Step 7: E2E 清单（spec §1.6，逐项导出目检 PDF）**

准备混合文档 `/tmp/md-editor-claude-2026-09-06/pdf-e2e.md`：≥5 页中文长文（各级标题）、KaTeX 行内+块级公式、mermaid flowchart/甘特/时序各一、跨页表格、长代码块、图片、列表/任务列表/引用。用 `HOME=/tmp/md-editor-claude-2026-09-06/home` 隔离环境运行 `build/src/md-editor` 逐项导出：

1. 默认设置（页眉页脚开）：每页页眉居中=文件基名、页脚右「第 X 页 / 共 Y 页」、页码连续且总数正确
2. KaTeX/mermaid 内容完整不碎、位置与编辑器一致（分页测量无错位）
3. 表格跨页正常断行；长代码块、图片、中文长文版式正常
4. 横向方向 + A5 纸张各导出一次：页面尺寸/方向正确、页眉页脚仍在
5. 单页文档显示「第 1 页 / 共 1 页」；空文档导出不崩
6. 设置关页眉（留页脚）、关页脚（留页眉）、全关（走 legacy——无页眉页脚、版式与旧版一致）各导出一次
7. 自定义四边距（如上 3cm）导出：边距生效、页眉不被裁

**逃生门**：mermaid SVG 被切断等硬阻断 → 停止，回报用户重议方案 B（spec §1.6）。

- [ ] **Step 8: 用户目视验证通过后提交**

```bash
git add src/core/PdfExporter.cpp src/ui/SettingsDialog.h src/ui/SettingsDialog.cpp
git commit -m "feat: PDF 导出页眉（文档标题）与页码（第 X 页/共 Y 页），paged.js 分页引擎接线与设置开关"
```

---

### Task 6: MarkdownHandler toHtml 块级脚注定义（TDD）

**Files:**
- Modify: `src/core/MarkdownHandler.h:8`（toHtml 签名加默认参）
- Modify: `src/core/MarkdownHandler.cpp`：`buildFootnoteDefHtml`（:867-883 重写）、主循环 def 分支（:1798-1807）、`extractFootnoteRefs`（:826-855 加开关参）、5 处调用点（:1956/1976/1999/2007/2066 附近）
- Test: `tests/test_markdownhandler.cpp`（追加）

**Interfaces:**
- Produces:
  - `static QString toHtml(const QString &markdown, bool extractFootnotes = true);`——嵌套调用传 false：定义内容内 `[^x]` 引用与 `[^x]:` 定义行**均按字面文本**穿过（嵌套脚注不支持）
  - toHtml 契约扩展：`[^id]: 首行` + 缩进续行（≥4 空格 / 1 tab；空行后接缩进行=定义内新段）→ `footnoteDef` 内 `div.footnote-content` 含**块级** HTML；Task 7/8 依赖

- [ ] **Step 1: 写失败测试**（`tests/test_markdownhandler.cpp` 脚注契约区追加）

```cpp
    void testFootnoteDefBlockParagraphsToHtml() {
        // 空行 + 缩进 = 定义内新段落
        QString md = "正文[^1]。\n\n[^1]: 第一段\n\n    第二段\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("data-type=\"footnoteDef\""));
        QVERIFY(html.contains("footnote-content"));
        // 两个段落都在定义内（块级），且不再残留在正文
        QVERIFY(html.contains("第一段"));
        QVERIFY(html.contains("第二段"));
        int bodyPos = html.indexOf("正文");
        int defPos = html.indexOf("第一段");
        QVERIFY(defPos > 0);
        // 第二段在定义区内部：其位置须在第一段之后且在容器闭合前
        QVERIFY(html.indexOf("第二段") > defPos);
        QVERIFY(html.indexOf("第二段") < html.indexOf("data-type=\"footnoteContainer\"") + 40
                || html.indexOf("第二段") > html.indexOf("footnoteContainer"));
    }

    void testFootnoteDefBlockListToHtml() {
        QString md = "[^1]: 段落\n    - 甲\n    - 乙\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("<li"));
        QVERIFY(html.contains("甲"));
        // 列表在定义内：正文流不残留列表文本（processed 里定义行被移除）
        QVERIFY(!html.left(html.indexOf("data-type=\"footnoteContainer\"")).contains("- 甲"));
    }

    void testFootnoteDefLazyContinuationStaysInBody() {
        // 非缩进续行不归属定义（与 MVP 行为一致），保留在正文
        QString md = "[^1]: 定义内容\n紧随正文\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("紧随正文"));
        // 紧随正文须出现在定义区之外（正文区）
        QVERIFY(html.indexOf("紧随正文") < html.indexOf("footnoteDef"));
    }

    void testFootnoteDefEmptyToHtml() {
        QString md = "正文[^1]。\n\n[^1]:\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("data-type=\"footnoteDef\""));
        // 空定义给一个空段落（block+ schema 最小内容）
        QVERIFY(html.contains("<p></p>"));
    }

    void testFootnoteDefNestedPipelineMath() {
        // 定义内容经嵌套主管线：行内公式占位符机制生效（非裸 setMarkdown）
        QString md = "[^1]: 公式 $a+b$ 注\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("data-latex"));
    }

    void testFootnoteDefNestedDefLiteral() {
        // 嵌套定义行按字面文本（不二次提取）
        QString md = "[^1]: 外层\n    [^2]: 内层\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(!html.contains("data-id=\"2\""));
        QVERIFY(html.count("data-type=\"footnoteDef\"") == 1);
    }
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build -j$(nproc) && cd build && ctest -R test_markdownhandler --output-on-failure
```
Expected: 新用例 FAIL（列表不在定义内 / 空定义无 `<p></p>` / 公式无 data-latex 等）。

- [ ] **Step 3: MarkdownHandler.h 签名**

```cpp
    // extractFootnotes=false：嵌套转换（脚注定义内容）——[^x] 引用与定义行均按字面
    static QString toHtml(const QString &markdown, bool extractFootnotes = true);
```

- [ ] **Step 4: 主循环 def 分支升级（续行捕获）**

`MarkdownHandler.cpp` def 分支（`else if (isFootnoteDef) {`）整体替换为：

```cpp
        } else if (isFootnoteDef && extractFootnotes) {
            // 脚注定义行 [^id]: 内容（非代码块）—— 抽取归集到文末容器，从正文流移除。
            // 嵌套层（extractFootnotes=false）不进此分支：定义行按字面输出。
            // 块级定义：缩进续行（≥4 空格 / 1 tab）归属定义；空行后仍有缩进行 =
            // 定义内新段落（pandoc 式）；非缩进行（含懒续行）即定义结束留在正文
            QString fid = footnoteDefMatch.captured(1).trimmed();
            QString contentMd = footnoteDefMatch.captured(2).trimmed();
            int j = i + 1;
            while (j < lines.size()) {
                const QString &cont = lines[j];
                if (cont.trimmed().isEmpty()) {
                    // 空行不消费：先向后探测，其后首个非空行是缩进行才算定义内段落分隔
                    int k = j;
                    while (k < lines.size() && lines[k].trimmed().isEmpty()) ++k;
                    if (k < lines.size()
                        && (lines[k].startsWith(QStringLiteral("    "))
                            || lines[k].startsWith(QLatin1Char('\t')))) {
                        contentMd += QStringLiteral("\n\n");
                        j = k;          // 消费中间空行（定义内段落分隔）
                        continue;
                    }
                    break;              // 空行后无缩进行 → 定义结束
                }
                if (!(cont.startsWith(QStringLiteral("    "))
                      || cont.startsWith(QLatin1Char('\t'))))
                    break;              // 非缩进非空 → 定义结束（懒续行留正文）
                contentMd += QLatin1Char('\n');
                contentMd += cont.startsWith(QLatin1Char('\t'))
                                 ? cont.mid(1) : cont.mid(4);   // 去一层缩进
                ++j;
            }
            if (!footnoteDefIds.contains(fid)) {   // 同 id 取首次定义
                footnoteDefIds << fid;
                footnoteDefs << buildFootnoteDefHtml(fid, contentMd);
            }
            processed += '\n';   // 行被移除，留空行维持段落分隔
            i = j - 1;           // 跳过已消费的续行
            lastBlock = BT_NONE;
        } else if (...)
```

（主循环本就是 `for (int i = 0; ...)` 索引式，`i = j - 1` 后 `++i` 正好落在 j。）

- [ ] **Step 5: buildFootnoteDefHtml 重写（嵌套主管线）**

原函数（:865-883）整体替换：

```cpp
// 脚注定义内容（MD，可多行块级）→ footnoteDef 契约 HTML（marker + 块级 content）。
// 内容经完整 MD→HTML 主管线嵌套转换（数学/任务列表/高亮/上下标等占位符全套生效，
// 嵌套层禁用脚注提取：内层 [^x] 按字面处理，嵌套脚注不支持）。
static QString buildFootnoteDefHtml(const QString &id, const QString &contentMd)
{
    QString inner;
    if (contentMd.trimmed().isEmpty()) {
        inner = QStringLiteral("<p></p>");   // 空定义：block+ schema 最小内容
    } else {
        QString h = MarkdownHandler::toHtml(contentMd, false);
        static const QRegularExpression bodyRe(
            QStringLiteral("<body[^>]*>(.*)</body>"),
            QRegularExpression::DotMatchesEverythingOption);
        auto bm = bodyRe.match(h);
        inner = bm.hasMatch() ? bm.captured(1).trimmed() : h.trimmed();
        // Qt toHtml 携带的片段注释不进编辑器内容
        inner.remove(QStringLiteral("<!--StartFragment-->"));
        inner.remove(QStringLiteral("<!--EndFragment-->"));
    }
    return "<div data-type=\"footnoteDef\" data-id=\"" + id + "\">"
         + "<span class=\"footnote-marker\">" + id + "</span>"
         + "<div class=\"footnote-content\">" + inner + "</div></div>";
}
```

同文件 `extractFootnoteRefs`（:829）签名加末参并在开头早退：

```cpp
static QString extractFootnoteRefs(QString line, QStringList &refs, QStringList &phs,
                                   QSet<QString> &collisions, bool enabled = true)
{
    if (!enabled) return line;   // 嵌套层：[^x] 按字面穿过
    ...
```

主循环内 5 处 `extractFootnoteRefs(` 调用点全部追加实参 `extractFootnotes`（形参名与函数参一致；用 grep `extractFootnoteRefs(` 定位全数更新）。def 分支条件已在 Step 4 中带上 `&& extractFootnotes`——嵌套层定义行落到普通段落分支按字面输出。

- [ ] **Step 6: 跑测试确认全绿 + 全量回归**

```bash
cmake --build build -j$(nproc) && cd build && ctest -R test_markdownhandler --output-on-failure && ctest --output-on-failure
```
Expected: 新 6 用例 + 既有脚注用例 + roundtrip_matrix 全绿（重点盯既有 `testFootnote*` 与矩阵无回归——契约从 span 升 div，矩阵若断言 span 形态需同步更新断言为 div）。

- [ ] **Step 7: 提交**

```bash
git add src/core/MarkdownHandler.h src/core/MarkdownHandler.cpp tests/test_markdownhandler.cpp tests/test_roundtrip_matrix.cpp
git commit -m "feat: 脚注定义支持块级内容（toHtml 缩进续行捕获 + 嵌套主管线转换）"
```

---

### Task 7: MarkdownHandler fromHtml 块级脚注定义（TDD）

**Files:**
- Modify: `src/core/MarkdownHandler.cpp:3728-3790`（fromHtml step 7 重写）
- Test: `tests/test_markdownhandler.cpp`（追加）

**Interfaces:**
- Consumes: Task 6 的 toHtml 契约（缩进续行）
- Produces: fromHtml 契约——`div.footnote-content`（块级，兼容旧 `span`）逐 def 经**完整 fromHtml 主管线**转 MD，输出 `[^id]: 首行` + 续行 4 空格缩进（空行不缩进）；Task 8 往返依赖

- [ ] **Step 1: 写失败测试**

```cpp
    void testFootnoteDefBlockRoundTrip() {
        QString md = "正文[^1]。\n\n[^1]: 第一段\n\n    第二段\n";
        QString html = MarkdownHandler::toHtml(md);
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("[^1]: 第一段"), qPrintable(back));
        QVERIFY2(back.contains("    第二段"), qPrintable(back));   // 续行缩进
        // 二次往返稳定
        QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
        QCOMPARE(back2, back);
    }

    void testFootnoteDefBlockListRoundTrip() {
        QString md = "[^1]: 段落\n    - 甲\n    - 乙\n";
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("    - 甲"), qPrintable(back));
        QVERIFY2(back.contains("    - 乙"), qPrintable(back));
        QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
        QCOMPARE(back2, back);
    }

    void testFootnoteDefCodeBlockRoundTrip() {
        QString md = "[^1]: 说明\n        code line\n";
        // 8 空格 = 去一层后 4 空格缩进代码块
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("code line"), qPrintable(back));
        QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
        QCOMPARE(back2, back);
    }

    void testFootnoteDefEmptyRoundTrip() {
        QString md = "正文[^1]。\n\n[^1]:\n";
        QString back = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(md));
        QVERIFY2(back.contains("[^1]:"), qPrintable(back));
        QString back2 = MarkdownHandler::fromHtml(MarkdownHandler::toHtml(back));
        QCOMPARE(back2, back);
    }

    void testFootnoteDefTaskListInDefRoundTrip() {
        // 定义内任务列表：嵌套 fromHtml 主管线的 taskList 占位保护对称生效
        QString md = "[^1]: 待办\n    - [ ] 事项\n";
        QString html = MarkdownHandler::toHtml(md);
        QVERIFY(html.contains("taskList"));
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("- [ ] 事项"), qPrintable(back));
    }

    void testFootnoteDefSpanContentBackwardCompat() {
        // 旧内联形态（span.footnote-content，编辑器同会话旧 HTML/单测构造）仍可解析
        QString html = "<div data-type=\"footnoteContainer\">"
                       "<div data-type=\"footnoteDef\" data-id=\"1\">"
                       "<span class=\"footnote-marker\">1</span>"
                       "<span class=\"footnote-content\">旧内容</span></div></div>";
        QString back = MarkdownHandler::fromHtml(html);
        QVERIFY2(back.contains("[^1]: 旧内容"), qPrintable(back));
    }
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build -j$(nproc) && cd build && ctest -R test_markdownhandler --output-on-failure
```
Expected: FAIL（现实现非贪婪 `</div>` 会截断嵌套结构 / 无缩进输出）。

- [ ] **Step 3: step 7 重写**

`fromHtml` step 7 区块（`// 7) 脚注容器` 起至该块闭合 `}` ）替换为：

```cpp
    // 7) 脚注容器 <div data-type="footnoteContainer"> → [^id]: 内容（多条）
    //    块级定义：content 容器为 div（旧 span 形态兼容）；def 定界与 content
    //    提取均用平衡扫描（taskList 内嵌 <div>、Qt 嵌套 <span> 会破坏非贪婪
    //    匹配）；内容经完整 fromHtml 主管线嵌套转换后按缩进续行输出。
    //    依赖前置的 Step 6：footnoteRef 的 <sup> 已先隔离为 ZZFR。
    {
        QRegularExpression contOpenRe(
            R"(<div\b[^>]*\bdata-type\s*=\s*"footnoteContainer"[^>]*>)");
        int n = 0;
        int offset = 0;
        while (true) {
            auto om = contOpenRe.match(processed, offset);
            if (!om.hasMatch()) break;
            int closeStart = findMatchingDivClose(processed, om.capturedStart());
            if (closeStart < 0) { offset = om.capturedStart() + 1; continue; }
            QString inner = processed.mid(om.capturedEnd(),
                                          closeStart - om.capturedEnd());
            // 定位每条 def 开标签（定界交给平衡扫描）
            QRegularExpression defOpenRe(
                R"(<div\b[^>]*\bdata-type\s*=\s*"footnoteDef"[^>]*>)");
            QRegularExpression defIdRe("data-id\\s*=\\s*\"([^\"]*)\"");
            // content 容器：div（块级新形态）| span（内联旧形态）
            QRegularExpression contentOpenRe(
                R"(<(div|span)\b[^>]*class\s*=\s*"footnote-content"[^>]*>)");
            QString mdBlock;
            int doff = 0;
            while (true) {
                auto dm = defOpenRe.match(inner, doff);
                if (!dm.hasMatch()) break;
                int defClose = findMatchingDivClose(inner, dm.capturedStart());
                if (defClose < 0) { doff = dm.capturedEnd(); continue; }
                auto iim = defIdRe.match(dm.captured());
                QString id = iim.hasMatch() ? iim.captured(1) : QString();
                QString defInner = inner.mid(dm.capturedEnd(),
                                             defClose - dm.capturedEnd());
                auto com = contentOpenRe.match(defInner);
                QString contentHtml;
                if (com.hasMatch()) {
                    const bool isDiv = (com.captured(1) == QLatin1String("div"));
                    int contentClose = isDiv
                        ? findMatchingDivClose(defInner, com.capturedStart())
                        : findMatchingSpanClose(defInner, com.capturedStart());
                    contentHtml = (contentClose >= 0)
                        ? defInner.mid(com.capturedEnd(),
                                       contentClose - com.capturedEnd())
                        : defInner.mid(com.capturedEnd());
                } else {
                    contentHtml = defInner;   // 无 content 容器（异常兜底）
                }
                // 块级 HTML → MD：嵌套完整 fromHtml（任务列表/数学/行内代码等
                // 占位保护对称生效；内容内若含嵌套脚注容器，转出的 [^x]: 行
                // 即字面文本，与 toHtml 端字面处理对称）
                QString cm = MarkdownHandler::fromHtml(contentHtml);
                while (cm.endsWith('\n')) cm.chop(1);
                // 缩进续行输出：首行接 [^id]: 之后，其余行补 4 空格（空行不缩进）
                QStringList outLines = cm.split('\n');
                mdBlock += "[^" + id + "]: " + outLines.value(0);
                for (int li = 1; li < outLines.size(); ++li) {
                    mdBlock += '\n';
                    if (!outLines[li].trimmed().isEmpty())
                        mdBlock += QStringLiteral("    ") + outLines[li];
                }
                mdBlock += '\n';
                doff = defClose + 6;   // 跳过 </div>
            }
            QString ph = collisionFreePlaceholder(collisions, "FD", n++);
            placeholders << ph;
            backups << mdBlock;
            int totalLen = closeStart + 6 - om.capturedStart();  // 含 </div>
            QString wrapped = wrapBlock(ph);
            processed.replace(om.capturedStart(), totalLen, wrapped);
            offset = om.capturedStart() + wrapped.length();
        }
    }
```

注意：旧实现内 def 循环用 `defDivRe(.*?)</div>` 非贪婪——taskList 的 `<li><div>` 会提前截断，必须换平衡扫描（上面已换）。空行判定 `outLines[li].trimmed().isEmpty()` 不缩进——pandoc 兼容（Task 6 的捕获端已按「空行后接缩进行」对齐）。

- [ ] **Step 4: 跑测试确认全绿 + 全量回归**

```bash
cmake --build build -j$(nproc) && cd build && ctest -R test_markdownhandler --output-on-failure && ctest --output-on-failure
```
Expected: 新 6 用例过；roundtrip_matrix 全绿（若矩阵含脚注断言需同步 div 形态）。

- [ ] **Step 5: 提交**

```bash
git add src/core/MarkdownHandler.cpp tests/test_markdownhandler.cpp tests/test_roundtrip_matrix.cpp
git commit -m "feat: fromHtml 块级脚注定义平衡扫描提取与缩进续行输出（嵌套主管线对称转换）"
```

---

### Task 8: editor.js 脚注 schema 升级 block+（含 insertFootnote 空段修复）

**Files:**
- Modify: `src/resources/web/editor.js:685-715`（buildFootnoteDef）、`editor.js:3242`（insertFootnote def 创建）
- Modify: `src/resources/web/editor.css:810-818`（marker/def 样式）
- Test: `src/resources/web/footnote-block.test.js`（新建，Vitest）

**Interfaces:**
- Consumes: Task 6/7 的 `div.footnote-content` 契约
- Produces: `footnoteDef` content `block+`；`renderHTML` 产出 `div.footnote-content`——Task 9/10 依赖（跳转查询选择器 `div[data-type="footnoteDef"][data-id=…] .footnote-content`）

- [ ] **Step 1: 写失败测试**（`src/resources/web/footnote-block.test.js`，加载模式照 `line-gutter.test.js`）

```js
// @vitest-environment jsdom
// 脚注块级定义 schema 测试：footnoteDef content 'block+'（多段/列表可入）、
// renderHTML 产出 div.footnote-content（MarkdownHandler 块级契约对齐）、
// insertFootnote 创建空定义（block+ 最小内容 = 空段落）。
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadEditor() {
  const libs = [
    'prosemirror-model.js', 'prosemirror-state.js', 'prosemirror-transform.js',
    'prosemirror-view.js', 'tiptap-core.js', 'lowlight.js', 'searchPlugin.js',
  ];
  for (const f of libs) {
    try { window.eval(readFileSync(path.join(W, f), 'utf8')); } catch (e) { /* tiptap-all 聚合 */ }
  }
  window.eval(readFileSync(path.join(W, 'katex.min.js'), 'utf8'));
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf8'));
}

beforeAll(loadEditor);

describe('脚注块级定义 schema', () => {
  it('footnoteDef 接受多块内容（两段 + 列表）', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<p>正文<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1">' +
      '<span class="footnote-marker">1</span>' +
      '<div class="footnote-content"><p>第一段</p><ul><li>甲</li></ul></div>' +
      '</div></div>');
    const defs = [];
    api.editor.state.doc.descendants(n => {
      if (n.type.name === 'footnoteDef') defs.push(n);
    });
    expect(defs).toHaveLength(1);
    expect(defs[0].childCount).toBe(2);
    expect(defs[0].child(0).type.name).toBe('paragraph');
    expect(defs[0].child(1).type.name).toBe('bulletList');
  });

  it('renderHTML 产出 div.footnote-content（块级契约）', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1">' +
      '<div class="footnote-content"><p>x</p></div></div></div>');
    expect(api.editor.getHTML()).toContain('<div class="footnote-content">');
  });

  it('insertFootnote 空定义含一个空段落', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent('<p>文</p>');
    api.insertFootnote();
    let def = null;
    api.editor.state.doc.descendants(n => {
      if (!def && n.type.name === 'footnoteDef') def = n;
    });
    expect(def).not.toBeNull();
    expect(def.childCount).toBe(1);
    expect(def.child(0).type.name).toBe('paragraph');
  });
});
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cd src/resources/web && npx vitest run footnote-block.test.js
```
Expected: FAIL——`inline*` 不接受 bulletList 子节点（setContent 解析丢弃或断言失败）。

- [ ] **Step 3: buildFootnoteDef 升级**

`editor.js:685-715` 整函数替换（content / renderHTML 两处变化，parseHTML 不动——`querySelector('.footnote-content')` 对 div 同样生效）：

```js
// 构造单条脚注定义（容器子节点，块级富文本）
// marker span（只读，CSS float 左浮首段绕排）+ div.footnote-content（块级 content，
// MarkdownHandler 块级契约对齐——toHtml/fromHtml 产 div 形态）
function buildFootnoteDef() {
    return TipTapNode.create({
        name: 'footnoteDef',
        content: 'block+',
        defining: true,
        addAttributes: function() {
            return {
                id: { default: '' }
            };
        },
        parseHTML: function() {
            return [{
                tag: 'div[data-type=footnoteDef]',
                getAttrs: function(el) {
                    return { id: el.getAttribute('data-id') || '' };
                },
                // prosemirror ParseRule 字段名为 contentElement（非 getContentElement，
                // 写错时规则被忽略，marker 文本混入正文且每次往返叠加）
                contentElement: function(el) {
                    return el.querySelector('.footnote-content') || el;
                }
            }];
        },
        renderHTML: function(ref) {
            var id = ref.node.attrs.id;
            return ['div', { 'data-type': 'footnoteDef', 'data-id': id },
                ['span', { class: 'footnote-marker', contenteditable: 'false' }, id],
                ['div', { class: 'footnote-content' }, 0]];
        }
    });
}
```

- [ ] **Step 4: insertFootnote 空段修复**

`editor.js:3242` `var def = schema.nodes.footnoteDef.create({ id: newId });` 改为：

```js
        // block+ 最小内容：空定义须带一个空段落，否则无效节点被解析器丢弃
        var def = schema.nodes.footnoteDef.create(
            { id: newId }, schema.nodes.paragraph.create());
```

- [ ] **Step 5: editor.css 块级样式**

`editor.css:810-818` 的 def/marker 区块调整为（保留暗色行 :820-823 不动，按需补一条）：

```css
.ProseMirror div[data-type="footnoteDef"] {
  margin: 0.25em 0;
  overflow: hidden;             /* BFC 包住浮动 marker，防浮出容器 */
}
.ProseMirror .footnote-marker {
  color: #1967d2;
  font-weight: 600;
  margin-right: 0.35em;
  float: left;                  /* 首段绕排，视觉与旧内联形态连续 */
}
```

- [ ] **Step 6: 重编译（qrc）+ JS 测试 + C++ 回归**

```bash
cd src/resources/web && npx vitest run
cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc) && cd build && ctest --output-on-failure
```
Expected: vitest 全绿（新文件 + 既有 .test.js）；ctest 全绿。

- [ ] **Step 7: 手工 E2E（隔离 HOME）**——打开含块级定义脚注的 md：定义区可多段/列表编辑、保存重开内容不丢、源码模式显示缩进续行、暗色主题正常。

- [ ] **Step 8: 用户目视验证通过后提交**

```bash
git add src/resources/web/editor.js src/resources/web/editor.css src/resources/web/footnote-block.test.js
git commit -m "feat: 脚注定义升级块级内容（block+ schema、div.footnote-content、insertFootnote 空段修复）"
```

---

### Task 9: 脚注跳转互导 + hover 气泡预览

**Files:**
- Modify: `src/resources/web/editor.js`：ref NodeView（:626-662 加 click/hover）、buildFootnoteDef（加 addNodeView——返回箭头）、editorApi 增查找/跳转助手与 `_footnoteBubble`
- Modify: `src/resources/web/editor.css`（flash/shake/气泡/返回箭头样式 + 暗色）
- Test: `src/resources/web/footnote-nav.test.js`（新建，纯函数部分）

**Interfaces:**
- Consumes: Task 8 的 div.footnote-content 契约
- Produces: `editorApi.findFootnoteDefPos(id)`/`findFootnoteRefPos(id)`（返回 PM pos，未找到 -1）、`jumpToFootnoteDef(id)`/`jumpToFootnoteRef(id)`（返回 bool）、`_footnoteBubble.showFor(id, anchorEl)/scheduleHide()/cancelHide()/hide()`——Task 10 弹层打开时调 `hide(true)` 关气泡

- [ ] **Step 1: 写失败测试**（`footnote-nav.test.js`，加载模式同 Task 8）

```js
// @vitest-environment jsdom
// 脚注跳转纯函数测试：findFootnoteDefPos/findFootnoteRefPos 定位、
// _footnoteValidateId 校验规则（Task 10 复用）。
import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';
const W = path.dirname(fileURLToPath(import.meta.url));
function loadEditor() {
  const libs = ['prosemirror-model.js', 'prosemirror-state.js', 'prosemirror-transform.js',
    'prosemirror-view.js', 'tiptap-core.js', 'lowlight.js', 'searchPlugin.js'];
  for (const f of libs) {
    try { window.eval(readFileSync(path.join(W, f), 'utf8')); } catch (e) {}
  }
  window.eval(readFileSync(path.join(W, 'katex.min.js'), 'utf8'));
  window.eval(readFileSync(path.join(W, 'tiptap-all.js'), 'utf8'));
  window.eval(readFileSync(path.join(W, 'editor.js'), 'utf8'));
}
beforeAll(loadEditor);

describe('脚注定位助手', () => {
  it('findFootnoteDefPos/RefPos 按 id 定位（多次引用取首个 ref）', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<p>前<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<p>后<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '</div>');
    const refPos = api.findFootnoteRefPos('1');
    const defPos = api.findFootnoteDefPos('1');
    expect(refPos).toBeGreaterThan(0);
    expect(defPos).toBeGreaterThan(0);
    expect(defPos).toBeGreaterThan(refPos);
    expect(api.findFootnoteRefPos('不存在')).toBe(-1);
    expect(api.findFootnoteDefPos('不存在')).toBe(-1);
  });
  it('首次 ref 在第二次之前（首个引用语义）', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<p>a<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<p>b<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '</div>');
    const first = api.findFootnoteRefPos('1');
    const doc = api.editor.state.doc;
    let second = -1, seen = 0;
    doc.descendants((n, pos) => {
      if (n.type.name === 'footnoteReference' && n.attrs.id === '1') {
        seen += 1;
        if (seen === 2) second = pos;
      }
    });
    expect(first).toBeLessThan(second);
  });
});
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cd src/resources/web && npx vitest run footnote-nav.test.js
```
Expected: FAIL——`findFootnoteRefPos` 未定义。

- [ ] **Step 3: editorApi 助手实现**（`scrollToPos` 定义附近插入）

```js
    // === 脚注跳转互导（2026-09-06 脚注增强 spec §2.3） ===
    // 按 id 找首个 footnoteDef / footnoteReference 的文档 pos；未找到返回 -1
    findFootnoteDefPos: function(id) {
        var found = -1;
        if (!this.editor) return -1;
        this.editor.state.doc.descendants(function(node, pos) {
            if (found < 0 && node.type.name === 'footnoteDef'
                && node.attrs.id === id) found = pos;
        });
        return found;
    },
    findFootnoteRefPos: function(id) {
        var found = -1;
        if (!this.editor) return -1;
        this.editor.state.doc.descendants(function(node, pos) {
            if (found < 0 && node.type.name === 'footnoteReference'
                && node.attrs.id === id) found = pos;
        });
        return found;
    },
    // 目标高亮闪烁（重放动画：移除类 → 强制 reflow → 重加）
    _footnoteFlash: function(selector) {
        var el = document.querySelector(selector);
        if (!el) return;
        el.classList.remove('footnote-flash');
        void el.offsetWidth;
        el.classList.add('footnote-flash');
        setTimeout(function() { el.classList.remove('footnote-flash'); }, 1600);
    },
    // 悬空目标抖动提示（无对应 def/ref 时不跳转）
    _footnoteShake: function(selector) {
        var el = document.querySelector(selector);
        if (!el) return;
        el.classList.remove('footnote-shake');
        void el.offsetWidth;
        el.classList.add('footnote-shake');
        setTimeout(function() { el.classList.remove('footnote-shake'); }, 500);
    },
    // 单击引用 → 跳定义并高亮；无定义 → 原地抖动
    jumpToFootnoteDef: function(id) {
        var pos = this.findFootnoteDefPos(id);
        var sel = 'sup[data-type="footnoteRef"][data-id="' + CSS.escape(id) + '"]';
        if (pos < 0) { this._footnoteShake(sel); return false; }
        this.scrollToPos(pos);
        this._footnoteFlash('div[data-type="footnoteDef"][data-id="' + CSS.escape(id) + '"]');
        return true;
    },
    // 返回箭头 → 跳回首个引用并高亮；无引用 → 定义区抖动
    jumpToFootnoteRef: function(id) {
        var pos = this.findFootnoteRefPos(id);
        if (pos < 0) {
            this._footnoteShake('div[data-type="footnoteDef"][data-id="' + CSS.escape(id) + '"]');
            return false;
        }
        this.scrollToPos(pos);
        this._footnoteFlash('sup[data-type="footnoteRef"][data-id="' + CSS.escape(id) + '"]');
        return true;
    },
```

- [ ] **Step 4: ref NodeView 加单击跳转 + hover 气泡**（`editor.js:637-649` dblclick 区改造）

NodeView 工厂内（`var dom = ...` 与 `render()` 之间）加：

```js
                // 单击跳转（250ms 判别双击改名：双击时取消跳转定时器）
                var clickTimer = null;
                dom.addEventListener('click', function(e) {
                    e.stopPropagation();
                    if (clickTimer) return;
                    var targetId = node.attrs.id;
                    clickTimer = setTimeout(function() {
                        clickTimer = null;
                        window.editorApi.jumpToFootnoteDef(targetId);
                    }, 250);
                });
                // hover 气泡预览（非模态，不占 modalDialogStateChanged）
                dom.addEventListener('mouseenter', function() {
                    window.editorApi._footnoteBubble.cancelHide();
                    window.editorApi._footnoteBubble.showFor(node.attrs.id, dom);
                });
                dom.addEventListener('mouseleave', function() {
                    window.editorApi._footnoteBubble.scheduleHide();
                });
```

dblclick 处理器开头加 `clearTimeout(clickTimer); clickTimer = null;`（Task 10 会把 prompt 换成弹层，此处保持 prompt 不动）。

- [ ] **Step 5: def NodeView（返回箭头 + contentDOM）**

`buildFootnoteDef()`（Task 8 版）追加 `addNodeView`：

```js
        addNodeView: function() {
            return function(ref) {
                var node = ref.node;
                var dom = document.createElement('div');
                dom.setAttribute('data-type', 'footnoteDef');
                // marker（只读，float）内嵌返回箭头：hover 显现，跳回首个引用
                var marker = document.createElement('span');
                marker.className = 'footnote-marker';
                marker.setAttribute('contenteditable', 'false');
                var backBtn = document.createElement('button');
                backBtn.type = 'button';
                backBtn.className = 'footnote-back-btn';
                backBtn.textContent = '↩';
                backBtn.setAttribute('tabindex', '-1');
                backBtn.addEventListener('mousedown', function(e) {
                    e.preventDefault(); e.stopPropagation();   // 不抢编辑焦点
                });
                backBtn.addEventListener('click', function(e) {
                    e.preventDefault(); e.stopPropagation();
                    window.editorApi.jumpToFootnoteRef(node.attrs.id);
                });
                marker.appendChild(backBtn);
                var content = document.createElement('div');
                content.className = 'footnote-content';
                dom.appendChild(marker);
                dom.appendChild(content);
                // 重渲染时先清 marker 旧文本节点再插入新 id（防改名后叠加）
                function render() {
                    dom.setAttribute('data-id', node.attrs.id);
                    while (marker.firstChild && marker.firstChild !== backBtn)
                        marker.removeChild(marker.firstChild);
                    marker.insertBefore(document.createTextNode(node.attrs.id), backBtn);
                }
                render();
                return {
                    dom: dom,
                    contentDOM: content,
                    update: function(n) {
                        if (n.type !== node.type) return false;
                        node = n;
                        render();
                        return true;
                    }
                };
            };
        },
```

- [ ] **Step 6: `_footnoteBubble` 模块**（editorApi 对象内，`_linkEditor` 附近）

```js
    // 脚注 hover 气泡：克隆 def 首两块内容预览；非模态（ESC 不管，鼠标离开即收）
    _footnoteBubble: {
        el: null,
        hideTimer: null,
        _position: function(el, anchor) {
            var r = anchor.getBoundingClientRect();
            el.style.visibility = 'hidden';
            el.style.display = 'block';
            var top = r.top - el.offsetHeight - 8;
            if (top < 8) top = r.bottom + 8;   // 上缘不足翻下方
            el.style.left = Math.max(8,
                Math.min(r.left, window.innerWidth - el.offsetWidth - 8)) + 'px';
            el.style.top = top + 'px';
            el.style.visibility = 'visible';
        },
        _mount: function(anchor, buildContent) {
            this.hide(true);
            var el = document.createElement('div');
            el.className = 'footnote-bubble';
            buildContent(el);
            el.addEventListener('mouseenter', function() {
                window.editorApi._footnoteBubble.cancelHide();
            });
            el.addEventListener('mouseleave', function() {
                window.editorApi._footnoteBubble.scheduleHide();
            });
            document.body.appendChild(el);
            this._position(el, anchor);
            this.el = el;
        },
        showFor: function(id, anchor) {
            var def = document.querySelector(
                'div[data-type="footnoteDef"][data-id="' + CSS.escape(id) + '"] .footnote-content');
            if (!def) {
                this._mount(anchor, function(el) {
                    el.textContent = i18n('footnote.undefined');
                });
                return;
            }
            var blocks = Array.prototype.slice.call(def.children);
            if (blocks.length === 0) {
                this._mount(anchor, function(el) {
                    el.textContent = i18n('footnote.empty');
                });
                return;
            }
            this._mount(anchor, function(el) {
                for (var i = 0; i < Math.min(2, blocks.length); i++)
                    el.appendChild(blocks[i].cloneNode(true));
                if (blocks.length > 2) {
                    var more = document.createElement('div');
                    more.className = 'footnote-bubble-more';
                    more.textContent = '…';
                    el.appendChild(more);
                }
            });
        },
        scheduleHide: function() {
            var self = this;
            clearTimeout(this.hideTimer);
            this.hideTimer = setTimeout(function() { self.hide(); }, 300);
        },
        cancelHide: function() { clearTimeout(this.hideTimer); },
        hide: function() {
            clearTimeout(this.hideTimer);
            if (this.el) { this.el.remove(); this.el = null; }
        }
    },
```

i18n 两键（中/英两处字典各加，`footnote.prompt` 旁）：

```js
    'footnote.undefined': '未定义',
    'footnote.empty': '（空）',
```
```js
    'footnote.undefined': 'Undefined',
    'footnote.empty': '(empty)',
```

- [ ] **Step 7: editor.css 样式**

```css
/* 脚注跳转高亮/抖动 */
@keyframes footnote-flash-bg {
  0% { background-color: rgba(25, 103, 210, 0.25); }
  100% { background-color: transparent; }
}
.ProseMirror .footnote-flash { animation: footnote-flash-bg 1.5s ease-out; }
@keyframes footnote-shake-x {
  0%, 100% { transform: translateX(0); }
  25% { transform: translateX(-3px); }
  75% { transform: translateX(3px); }
}
.ProseMirror .footnote-shake { animation: footnote-shake-x 0.3s ease-in-out 2; }

/* 定义区返回箭头：hover 显现 */
.ProseMirror .footnote-back-btn {
  display: none;
  border: none;
  background: none;
  color: #1967d2;
  cursor: pointer;
  font-size: 0.9em;
  padding: 0 0.25em;
}
.ProseMirror .footnote-marker:hover .footnote-back-btn { display: inline; }

/* hover 气泡预览（非模态） */
.footnote-bubble {
  position: fixed;
  z-index: 1000;
  max-width: 320px;
  padding: 8px 12px;
  background: #fff;
  border: 1px solid #e0e0e0;
  border-radius: 6px;
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.12);
  font-size: 0.85em;
  line-height: 1.5;
  pointer-events: auto;
}
.footnote-bubble p { margin: 0 0 0.3em; }
.footnote-bubble .footnote-bubble-more { color: #999; }
body.dark .footnote-bubble {
  background: #2b2f33;
  border-color: #444;
  color: #d0d4d8;
}
```

- [ ] **Step 8: 重编译 + JS 测试 + C++ 回归**

```bash
cd src/resources/web && npx vitest run
cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc) && cd build && ctest --output-on-failure
```

- [ ] **Step 9: 手工 E2E**——单击引用跳定义并闪烁；悬空引用单击原地抖动；定义 marker hover 出 ↩，点击回首个引用并闪烁；仅有一个引用时同上；hover 引用出气泡（前两块 + …），移开 300ms 收；气泡在编辑器上缘时翻下方；暗色主题配色正常；双击仍弹 prompt 改名（Task 10 换弹层）。

- [ ] **Step 10: 用户目视验证通过后提交**

```bash
git add src/resources/web/editor.js src/resources/web/editor.css src/resources/web/footnote-nav.test.js
git commit -m "feat: 脚注点击跳转互导（返回箭头）与 hover 气泡预览"
```

---

### Task 10: 脚注内联改名弹层（替换 prompt）

**Files:**
- Modify: `src/resources/web/editor.js`：`_footnoteEditor`（新建，`_linkEditor` 后）、ref NodeView dblclick（:637-649 替换 prompt）、i18n 字典
- Modify: `src/ui/MainWindow.cpp:134-138`（ESC 链补 close 调用）
- Test: `src/resources/web/footnote-nav.test.js`（追加校验用例）

**Interfaces:**
- Consumes: Task 9 的 `_footnoteBubble.hide()`（弹层打开时关气泡）、`renameFootnote(from, to)`（editor.js:3257，既有）
- Produces: `editorApi._footnoteEditor.open(id)` / `.close()`；`editorApi._footnoteValidateId(v, curId)` → `{ok:bool, err:'duplicate'|'invalid'|''}`

- [ ] **Step 1: 追加失败测试**（footnote-nav.test.js）

```js
describe('脚注改名校验', () => {
  it('_footnoteValidateId 拒空/非法字符/重名，接受正常改名', () => {
    const api = window.editorApi;
    api.init();
    api.editor.commands.setContent(
      '<p>a<sup data-type="footnoteRef" data-id="1">1</sup></p>' +
      '<div data-type="footnoteContainer">' +
      '<div data-type="footnoteDef" data-id="1"><div class="footnote-content"><p>x</p></div></div>' +
      '<div data-type="footnoteDef" data-id="2"><div class="footnote-content"><p>y</p></div></div>' +
      '</div>');
    expect(api._footnoteValidateId('', '1').ok).toBe(false);
    expect(api._footnoteValidateId('a[b', '1').ok).toBe(false);
    expect(api._footnoteValidateId('a b', '1').ok).toBe(false);
    expect(api._footnoteValidateId('a:b', '1').ok).toBe(false);
    expect(api._footnoteValidateId('2', '1').ok).toBe(false);      // 已存在
    expect(api._footnoteValidateId('2', '1').err).toBe('duplicate');
    expect(api._footnoteValidateId('注A', '1').ok).toBe(true);
    expect(api._footnoteValidateId('1', '1').ok).toBe(true);       // 原名直过
  });
});
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cd src/resources/web && npx vitest run footnote-nav.test.js
```

- [ ] **Step 3: 实现 `_footnoteValidateId` 与 `_footnoteEditor`**（editorApi 内，`_linkEditor` 定义后）

```js
    // 脚注标记校验：非空、不含 [ ] 空白冒号、不与现有 id 重名（原名直过）
    _footnoteValidateId: function(v, curId) {
        v = (v || '').trim();
        if (!v) return { ok: false, err: 'invalid' };
        if (/[\[\]\s:]/.test(v)) return { ok: false, err: 'invalid' };
        if (v !== curId && this.findFootnoteDefPos(v) >= 0)
            return { ok: false, err: 'duplicate' };
        // 引用与定义任一处占用即重名
        if (v !== curId && this.findFootnoteRefPos(v) >= 0)
            return { ok: false, err: 'duplicate' };
        return { ok: true, err: '' };
    },

    // 脚注改名弹层（仿 _linkEditor：遮罩+弹层、modalDialogStateChanged、Enter 确定）
    _footnoteEditor: {
        overlay: null,
        popover: null,
        input: null,
        errLabel: null,
        _curId: '',
        _bodyOverflow: '',

        open: function(id) {
            var ed = window.editorApi.editor;
            if (!ed) return;
            this._curId = id;
            window.editorApi._footnoteBubble.hide();   // 弹层压过气泡
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(true);
            this._bodyOverflow = document.body.style.overflow;
            document.body.style.overflow = 'hidden';
            this.buildDom();
            this.input.value = id;
            this.show();
            this.input.focus();
            this.input.select();
            ed.view.dom.blur();
        },

        buildDom: function() {
            this.cleanupDom();
            var self = this;
            this.overlay = document.createElement('div');
            this.overlay.className = 'math-editor-overlay';
            this.popover = document.createElement('div');
            this.popover.className = 'math-editor-popover footnote-editor-popover';

            var titleBar = document.createElement('div');
            titleBar.className = 'math-editor-titlebar';
            var tag = document.createElement('span');
            tag.className = 'math-editor-type-tag';
            _setI18n(tag, 'footnote.renameTitle');
            titleBar.appendChild(tag);
            var closeBtn = document.createElement('button');
            closeBtn.className = 'math-editor-close-btn';
            closeBtn.textContent = '×';
            _setI18nTitle(closeBtn, 'common.close');
            closeBtn.addEventListener('click', function() { self.close(); });
            titleBar.appendChild(closeBtn);
            this.popover.appendChild(titleBar);

            this.input = document.createElement('input');
            this.input.type = 'text';
            this.input.className = 'math-editor-input';
            this.input.spellcheck = false;
            this.input.addEventListener('keydown', function(e) {
                if (e.key === 'Enter') { e.preventDefault(); self.confirm(); }
            });
            this.input.addEventListener('input', function() { self.clearError(); });
            this.popover.appendChild(this.input);

            this.errLabel = document.createElement('div');
            this.errLabel.className = 'footnote-editor-error';
            this.errLabel.style.display = 'none';
            this.popover.appendChild(this.errLabel);

            var btnRow = document.createElement('div');
            btnRow.className = 'math-editor-btnrow';
            var cancelBtn = document.createElement('button');
            cancelBtn.className = 'math-editor-btn';
            _setI18n(cancelBtn, 'common.cancel');
            cancelBtn.addEventListener('click', function() { self.close(); });
            var okBtn = document.createElement('button');
            okBtn.className = 'math-editor-btn math-editor-btn-primary';
            _setI18n(okBtn, 'common.ok');
            okBtn.addEventListener('click', function() { self.confirm(); });
            btnRow.appendChild(cancelBtn);
            btnRow.appendChild(okBtn);
            this.popover.appendChild(btnRow);

            this.overlay.addEventListener('wheel', function(e) { e.preventDefault(); });
            this.overlay.appendChild(this.popover);
            document.body.appendChild(this.overlay);
        },

        show: function() { if (this.overlay) this.overlay.style.display = 'flex'; },
        clearError: function() { this.errLabel.style.display = 'none'; },
        showError: function(err) {
            this.errLabel.style.display = 'block';
            _setI18n(this.errLabel,
                err === 'duplicate' ? 'footnote.duplicateId' : 'footnote.invalidId');
        },

        confirm: function() {
            var v = this.input.value;
            var check = window.editorApi._footnoteValidateId(v, this._curId);
            if (!check.ok) { this.showError(check.err); return; }
            var newId = v.trim();
            if (newId !== this._curId)
                window.editorApi.renameFootnote(this._curId, newId);
            this._confirmedClose();
        },

        _confirmedClose: function() {
            // 改名产生文档变更（onUpdate 已置脏并通知 C++），此处无需恢复
            this.close();
        },

        close: function() {
            this.cleanupDom();
            document.body.style.overflow = this._bodyOverflow;
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(false);
        },

        cleanupDom: function() {
            if (this.overlay) { this.overlay.remove(); this.overlay = null; }
            this.popover = null; this.input = null; this.errLabel = null;
        }
    },
```

注意：`common.cancel`/`common.ok`/`common.close` 若字典中无对应键，先 grep `common.` 现有键名并**复用**（如已有 `common.close` 则同样存在取消/确定键；若缺则补 `'common.cancel': '取消'` / `'common.ok': '确定'` 与英文）。`math-editor-btnrow`/`math-editor-btn` 类若 `_linkEditor` 未用，grep 现有按钮类名并对齐（弹层视觉与链接编辑器一致即可）。

i18n 新键（中英）：

```js
    'footnote.renameTitle': '重命名脚注标记',
    'footnote.duplicateId': '该标记已存在',
    'footnote.invalidId': '标记不能包含 [ ] 空白或冒号',
```
```js
    'footnote.renameTitle': 'Rename footnote marker',
    'footnote.duplicateId': 'Marker already exists',
    'footnote.invalidId': 'Marker cannot contain [ ] whitespace or colon',
```

CSS（editor.css，math-editor 弹层样式区附近）：

```css
.footnote-editor-error { color: #d93025; font-size: 0.85em; margin: 4px 0; }
body.dark .footnote-editor-error { color: #f28b82; }
.footnote-editor-popover { min-width: 280px; }
```

- [ ] **Step 4: dblclick 换弹层**

ref NodeView dblclick 处理器（Task 9 改造后）替换为：

```js
                dom.addEventListener('dblclick', function(e) {
                    e.preventDefault();
                    e.stopPropagation();
                    clearTimeout(clickTimer);
                    clickTimer = null;
                    window.editorApi._footnoteEditor.open(node.attrs.id);
                });
```

（删除 prompt 分支；`footnote.editId` title 与 `footnote.prompt` 字典键保留——title 仍作悬浮提示。）

- [ ] **Step 5: C++ ESC 链补 close（需重编译）**

`MainWindow.cpp:134-138` 的 runJavaScript 串追加一行：

```cpp
            runEditorCommand("window.editorApi._linkEditor.close();"
                             "window.editorApi._mermaidEditor.close();"
                             "window.editorApi._mathEditor.close();"
                             "window.editorApi._emojiPicker.close();"
                             "window.editorApi._codeLangPicker.close();"
                             "window.editorApi._footnoteEditor.close();");
```

- [ ] **Step 6: 重编译 + 全部测试**

```bash
cd src/resources/web && npx vitest run
cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc) && cd build && ctest --output-on-failure
```

- [ ] **Step 7: 手工 E2E**——双击引用弹改名层（预填当前 id）；Enter/确定改名后正文所有引用与定义同步；重名/非法输入红字提示且不关层；取消/× /ESC（经 C++ 链）关闭无副作用；弹层开时 hover 气泡已收；脏状态：改名后标签页变 ●。

- [ ] **Step 8: 用户目视验证通过后提交**

```bash
git add src/resources/web/editor.js src/resources/web/editor.css src/ui/MainWindow.cpp src/resources/web/footnote-nav.test.js
git commit -m "feat: 脚注改名改内联弹层（校验重名/非法、ESC 接线），替换原生 prompt"
```

---

### Task 11: 收尾文档同步

**Files:**
- Modify: `CLAUDE.md`（已知问题区、关键文件表、专题文档区）
- Modify: `README.md`（功能列表）
- Modify: `docs/superpowers/specs/2026-08-09-footnote-design.md`（文末执行记录）

**Interfaces:** 无。

- [ ] **Step 1: CLAUDE.md**

已知问题区删除行：`- PDF 导出页眉页脚/页码尚未实现（\`PdfExporter\` 已支持页边距与方向）`（其余行保留）。

关键文件表加一行（`HtmlExporter` 行后）：

```markdown
| `src/resources/web/paged.polyfill.js` | vendored paged.js 分页引擎（PDF 页眉页脚/页码，见 docs/vendor-pagedjs.md） |
```

专题文档列表加一行：

```markdown
- **[vendor paged.js 说明](docs/vendor-pagedjs.md)** — PDF 分页引擎来源/版本/同步流程
```

- [ ] **Step 2: README.md 功能列表**

「导出」行改为：

```markdown
- **导出** — PDF（Chromium 渲染，页眉标题 + 页码）、DOCX、自包含 HTML 单文件
```

「完整 Markdown 语法」行后补一条：

```markdown
- **脚注** — 块级定义、点击跳转互导、hover 预览、内联改名
```

- [ ] **Step 3: footnote MVP spec 增补执行记录**

`docs/superpowers/specs/2026-08-09-footnote-design.md` 文末追加：

```markdown
---

## 12. 第二阶段执行记录（2026-09-06）

§11 四项（跳转互导 / hover 气泡 / 块级定义 / 内联改名）已实现，设计与实现计划见
`docs/superpowers/specs/2026-09-06-pdf-header-footer-footnote-design.md` §2 与
`docs/superpowers/plans/2026-09-06-pdf-header-footer-footnote.md` Task 6-10。

与原 §11 设想的差异：
- 块级内容转换复用 MarkdownHandler 主管线嵌套调用（非裸 setMarkdown），数学/任务列表对称生效
- 续行判定：非缩进「懒续行」不归属定义（保持 MVP 行为，往返稳定）
- 嵌套脚注（定义内再定义）按字面文本往返，仍不支持
```

- [ ] **Step 4: 提交**

```bash
git add CLAUDE.md README.md docs/superpowers/specs/2026-08-09-footnote-design.md
git commit -m "docs: PDF 页眉页脚与脚注增强落地后的文档同步（已知问题/README/spec 执行记录）"
```

---

## 验证总门（全部任务完成后）

- [ ] `cd build && ctest --output-on-failure` 全绿；`cd src/resources/web && npx vitest run` 全绿
- [ ] Task 5 E2E 清单 + Task 8/9/10 手工 E2E 全部目检通过（用户确认）
- [ ] 二进制时间戳新于全部改动源文件（`ls -la build/src/md-editor` vs `find src -newer build/src/md-editor`）
- [ ] 版本收口：与用户确认后 +0.1.0（CMakeLists/CHANGELOG/tag，按 CLAUDE.md 规则）
