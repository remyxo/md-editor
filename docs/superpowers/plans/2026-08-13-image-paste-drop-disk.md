# 图片粘贴/拖拽落盘 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 粘贴或拖入编辑器的图片自动写入文档同级 `images/` 目录，并在文档中插入相对路径引用（文档移动后图片不失效）。

**Architecture:** 项目首个 JS↔C++ round-trip。JS（`editor.js` 的 ProseMirror `editorProps.handlePaste/handleDrop`）拦截图片，同步插入「占位图」并读为 base64，通过 QWebChannel 调用 `EditorBridge::requestImageSave` 槽；C++（`MainWindow` 按 tab 编排）取该 tab 的 `filePath`，用新工具类 `ImageManager` 落盘，再 `runJavaScript` 回调 `window.editorApi._onImageSaveResult`，JS 据唯一 `requestId` 定位占位图节点、把 src 替换为相对路径（失败则删除占位图）。

**Tech Stack:** Qt6 (QWebChannel / QWebEnginePage::runJavaScript) / C++17 / TipTap+ProseMirror（`editor.js`）/ QtTest。

## Global Constraints

- 文档与注释一律**中文**，代码标识符英文。
- 版本号唯一定义在 `CMakeLists.txt` 的 `project(md-editor VERSION x.y.z)`；本计划不碰版本（版本推进是发版/打 tag 时的事，见 CHANGELOG）。
- 提交规范：`feat:` / `fix:` / `chore:` 前缀 + 中文描述；每个 Task 末尾独立提交。
- C++17；依赖只用 `find_package`（不引入新第三方库）；Qt6 组件 `Widgets WebEngineWidgets WebChannel`。
- 编辑区内部存储 HTML；读写时 `MarkdownHandler` 做 MD↔HTML 转换（本计划不改 `MarkdownHandler`——图片完全走 Qt md4c 原生 `![](src)`↔`<img>`，相对路径原样往返）。
- 往返保真：图片 src 始终为相对 `.md` 所在目录的 `images/xxx.ext`，不丢失。
- 改 `editor.js` 只需重新 `make`（AUTORCC 重新编译 qrc），**无需 esbuild**（`tiptap-all.js` 不动）。
- 测试：纯 C++ 逻辑测试用 QtTest，无 GUI、无需 Xvfb（本地）；CI 跑 ctest 需 `QT_QPA_PLATFORM=offscreen`。

---

## File Structure

| 文件 | 责任 | 操作 |
|------|------|------|
| `src/core/ImageManager.h` / `.cpp` | 纯静态工具：把图片字节写入 `images/`，命名/碰撞/原子写，返回相对路径 | 新建 |
| `src/core/EditorBridge.h` / `.cpp` | 新增 `requestImageSave` 槽 + `imageSaveRequested` 信号（沿用 tabId 在末尾的约定） | 修改 |
| `src/ui/MainWindow.h` / `.cpp` | 在 `onEditorBridgeCreated` 连接新信号；新增 `onImageSaveRequested` 编排 + 文件静态回调串构造 | 修改 |
| `src/ui/EditorTab.h` / `.cpp` | 新增 `setFilePath`/`pushDocDir`：保存/打开/会话恢复时把文档目录经 `setDocDir` 推给 JS（图片相对路径→`file://` 渲染） | 修改 |
| `src/resources/web/editor.js` | config 加 `editorProps`（handlePaste/handleDrop + image NodeView）；`editorApi` 加落盘方法 + `setDocDir`/`_resolveImageSrc` | 修改 |
| `src/resources/web/editor.css` | 占位图加载样式 | 修改 |
| `tests/test_imagemanager.cpp` | ImageManager 单元测试（QtTest） | 新建 |
| `tests/test_editorbridge.cpp` | 新增 `requestImageSave` 转发用例 | 修改 |
| `tests/CMakeLists.txt` | 注册 `test_imagemanager` | 修改 |
| `.github/workflows/build-deb.yml` | 开启 `BUILD_TESTS=ON` + ctest 步骤 | 修改 |

---

## Task 1: ImageManager 落盘工具（C++，TDD）

**Files:**
- Create: `src/core/ImageManager.h`, `src/core/ImageManager.cpp`
- Create: `tests/test_imagemanager.cpp`
- Modify: `tests/CMakeLists.txt:17`（文件末尾追加）

**Interfaces:**
- Produces: `ImageManager::saveImage(docFilePath, bytes, suggestedName, fallbackExt="png") → QString`（相对路径如 `"images/photo.png"`；失败返回空 `QString`）。Task 4 的 `MainWindow` 依赖此签名。

- [ ] **Step 1: 在 `tests/CMakeLists.txt` 末尾注册新测试**

在 `/home/daijd/claude-workspace/md-editor/tests/CMakeLists.txt` 第 16 行（`add_test(... test_markdownhandler ...)` 之后、文件末尾）追加：

```cmake

add_executable(test_imagemanager test_imagemanager.cpp ../src/core/ImageManager.cpp)
target_include_directories(test_imagemanager PRIVATE ../src)
target_link_libraries(test_imagemanager PRIVATE Qt6::Test)
add_test(NAME test_imagemanager COMMAND test_imagemanager)
```

- [ ] **Step 2: 写失败测试 `tests/test_imagemanager.cpp`**

```cpp
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include "../src/core/ImageManager.h"

class TestImageManager : public QObject {
    Q_OBJECT
private slots:
    // 带建议名：按建议名写入，返回相对路径，字节一致
    void testSuggestedNameWritesFile() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString docPath = dir.path() + "/doc.md";
        QByteArray bytes("PNGBYTES");

        QString rel = ImageManager::saveImage(docPath, bytes, "photo.png");

        QCOMPARE(rel, QStringLiteral("images/photo.png"));
        QString abs = dir.path() + "/images/photo.png";
        QVERIFY(QFile::exists(abs));
        QFile f(abs); QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bytes);
    }

    // 自动创建 images/ 子目录
    void testCreatesImagesDir() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString docPath = dir.path() + "/doc.md";
        QVERIFY(!QFileInfo::exists(dir.path() + "/images"));

        QString rel = ImageManager::saveImage(docPath, "X", "a.png");

        QCOMPARE(rel, QStringLiteral("images/a.png"));
        QVERIFY(QFileInfo::exists(dir.path() + "/images"));
    }

    // 重名追加 -2/-3 后缀
    void testCollisionSuffix() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString docPath = dir.path() + "/doc.md";

        QCOMPARE(ImageManager::saveImage(docPath, "1", "dup.png"), QStringLiteral("images/dup.png"));
        QCOMPARE(ImageManager::saveImage(docPath, "2", "dup.png"), QStringLiteral("images/dup-2.png"));
        QCOMPARE(ImageManager::saveImage(docPath, "3", "dup.png"), QStringLiteral("images/dup-3.png"));
        QVERIFY(QFile::exists(dir.path() + "/images/dup-3.png"));
    }

    // 无建议名（剪贴板粘贴）：用 image-<时间戳>.<ext>
    void testEmptySuggestedUsesTimestamp() {
        QTemporaryDir dir; QVERIFY(dir.isValid());

        QString rel = ImageManager::saveImage(dir.path() + "/doc.md", "bytes", "", "png");

        QVERIFY2(rel.startsWith(QStringLiteral("images/image-")),
                 qPrintable("期望以 images/image- 开头，实际: " + rel));
        QVERIFY(rel.endsWith(QStringLiteral(".png")));
        QVERIFY(QFile::exists(dir.path() + "/" + rel));
    }

    // 空字节：返回空，不建目录
    void testEmptyBytesReturnsEmpty() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString rel = ImageManager::saveImage(dir.path() + "/doc.md", "", "x.png");
        QVERIFY(rel.isEmpty());
        QVERIFY(!QFileInfo::exists(dir.path() + "/images"));
    }

    // fallbackExt 由 MIME 决定（jpg）
    void testFallbackExtFromMime() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString rel = ImageManager::saveImage(dir.path() + "/doc.md", "B", "", "jpg");
        QVERIFY(rel.endsWith(QStringLiteral(".jpg")));
    }
};

QTEST_MAIN(TestImageManager)
#include "test_imagemanager.moc"
```

- [ ] **Step 3: 运行测试，确认编译失败（ImageManager 不存在）**

Run: `cd build && cmake --build . --target test_imagemanager 2>&1 | head -30`
Expected: 编译错误，`ImageManager.h: No such file`。

- [ ] **Step 4: 实现 `src/core/ImageManager.h`**

```cpp
#pragma once
#include <QString>
#include <QByteArray>

// 图片落盘工具：把粘贴/拖拽进来的图片字节写入文档同级的 images/ 目录，
// 返回相对文档所在目录的引用路径。纯静态工具类，便于单元测试。
class ImageManager {
public:
    // 将 bytes 写入 docFilePath 同级的 images/ 子目录。
    //   suggestedName: 期望文件名（含扩展名，如 "photo.png"）；为空时按时间戳生成。
    //   fallbackExt:    suggestedName 为空（剪贴板粘贴）时用的扩展名，默认 "png"。
    // 返回相对 docFilePath 所在目录的路径，如 "images/photo.png"；
    // 失败（空字节 / 建目录失败 / 写失败）返回空 QString。
    static QString saveImage(const QString &docFilePath, const QByteArray &bytes,
                             const QString &suggestedName,
                             const QString &fallbackExt = QStringLiteral("png"));
};
```

- [ ] **Step 5: 实现 `src/core/ImageManager.cpp`**

```cpp
#include "ImageManager.h"
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QFile>

// 从文件名取扩展名（小写无点）；无则用 fallback
static QString extFromName(const QString &name, const QString &fallback)
{
    int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot < name.length() - 1)
        return name.mid(dot + 1).toLower();
    return fallback;
}

QString ImageManager::saveImage(const QString &docFilePath, const QByteArray &bytes,
                                const QString &suggestedName, const QString &fallbackExt)
{
    if (bytes.isEmpty()) return QString();

    QFileInfo docInfo(docFilePath);
    QDir imagesDir(docInfo.absolutePath() + QStringLiteral("/images"));
    if (!imagesDir.exists() && !imagesDir.mkpath(QStringLiteral(".")))
        return QString();

    // 决定主名与扩展名
    QString baseName, ext;
    if (!suggestedName.isEmpty()) {
        QString full = QFileInfo(suggestedName).fileName(); // 防路径穿越
        ext = extFromName(full, fallbackExt);
        baseName = QFileInfo(full).completeBaseName();
        if (baseName.isEmpty()) baseName = QStringLiteral("image");
    } else {
        ext = fallbackExt.isEmpty() ? QStringLiteral("png") : fallbackExt;
        baseName = QStringLiteral("image-")
                   + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    }

    // 碰撞处理：photo.png → photo-2.png → photo-3.png ...
    QString fileName = baseName + QLatin1Char('.') + ext;
    for (int counter = 2; imagesDir.exists(fileName); ++counter)
        fileName = baseName + QStringLiteral("-%1.").arg(counter) + ext;

    // 原子写：临时文件 + rename（同文件系统内）
    QString tmpPath = imagesDir.absoluteFilePath(fileName) + QStringLiteral(".tmp");
    {
        QFile f(tmpPath);
        if (!f.open(QIODevice::WriteOnly)) return QString();
        if (f.write(bytes) != bytes.size()) { f.close(); QFile::remove(tmpPath); return QString(); }
        f.close();
    }
    QString absPath = imagesDir.absoluteFilePath(fileName);
    if (!QFile::rename(tmpPath, absPath)) { QFile::remove(tmpPath); return QString(); }

    return QStringLiteral("images/") + fileName;
}
```

- [ ] **Step 6: 运行测试，确认通过**

Run: `cd build && cmake --build . --target test_imagemanager && QT_QPA_PLATFORM=offscreen ./test_imagemanager`
Expected: `Totals: 6 passed, 0 failed`。

- [ ] **Step 7: 提交**

```bash
git add src/core/ImageManager.h src/core/ImageManager.cpp tests/test_imagemanager.cpp tests/CMakeLists.txt
git commit -m "feat: 新增 ImageManager 图片落盘工具 + 单元测试"
```

---

## Task 2: EditorBridge 图片落盘槽与信号（C++，TDD）

**Files:**
- Modify: `src/core/EditorBridge.h:14-34`（slots 与 signals 区段）
- Modify: `src/core/EditorBridge.cpp`（新增 slot 实现）
- Modify: `tests/test_editorbridge.cpp`（新增用例）

**Interfaces:**
- Consumes: 无（首个 bridge 协议改动）。
- Produces: slot `EditorBridge::requestImageSave(requestId, base64, ext, suggestedName)` 与 signal `imageSaveRequested(..., int tabId)`。Task 3 的 `MainWindow` 连接此信号；Task 5 的 `editor.js` 调用此 slot。

- [ ] **Step 1: 在 `tests/test_editorbridge.cpp` 写失败用例**

在 `testFormatChangedInvalidJson()`（约第 100-107 行）之后、`};`（第 108 行）之前插入：

```cpp
    void testRequestImageSave() {
        EditorBridge bridge(7);
        QSignalSpy spy(&bridge, &EditorBridge::imageSaveRequested);

        bridge.requestImageSave("r1", "BASE64DATA", "png", "photo.png");

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toString(), QStringLiteral("r1"));        // requestId
        QCOMPARE(spy[0][1].toString(), QStringLiteral("BASE64DATA")); // base64
        QCOMPARE(spy[0][2].toString(), QStringLiteral("png"));       // ext
        QCOMPARE(spy[0][3].toString(), QStringLiteral("photo.png")); // suggestedName
        QCOMPARE(spy[0][4].toInt(), 7);                              // tabId
    }
```

- [ ] **Step 2: 运行测试，确认编译失败（无此 slot/signal）**

Run: `cd build && cmake --build . --target test_editorbridge 2>&1 | head -20`
Expected: 编译错误，`requestImageSave is not a member` / `imageSaveRequested is not a signal`。

- [ ] **Step 3: 在 `EditorBridge.h` 加 slot 与 signal**

在 `/home/daijd/claude-workspace/md-editor/src/core/EditorBridge.h` 的 `public slots:` 区（第 24 行 `void modalDialogChanged(bool open);` 之后）追加：

```cpp
    // JS → C++：请求把粘贴/拖拽的图片字节落盘（requestId 用于回调定位占位图）
    void requestImageSave(const QString &requestId, const QString &base64,
                          const QString &ext, const QString &suggestedName);
```

在 `signals:` 区（第 34 行 `void modalDialogStateChanged(bool open, int tabId);` 之后）追加：

```cpp
    void imageSaveRequested(const QString &requestId, const QString &base64,
                            const QString &ext, const QString &suggestedName, int tabId);
```

- [ ] **Step 4: 在 `EditorBridge.cpp` 加 slot 实现**

在 `/home/daijd/claude-workspace/md-editor/src/core/EditorBridge.cpp` 现有 slot 实现（如 `cursorChanged`，第 19-22 行）附近追加（仿其转发范式，tabId 在末尾）：

```cpp
void EditorBridge::requestImageSave(const QString &requestId, const QString &base64,
                                    const QString &ext, const QString &suggestedName)
{
    emit imageSaveRequested(requestId, base64, ext, suggestedName, m_tabId);
}
```

- [ ] **Step 5: 运行测试，确认通过**

Run: `cd build && cmake --build . --target test_editorbridge && QT_QPA_PLATFORM=offscreen ./test_editorbridge`
Expected: 全部用例通过（含新增 `testRequestImageSave`）。

- [ ] **Step 6: 提交**

```bash
git add src/core/EditorBridge.h src/core/EditorBridge.cpp tests/test_editorbridge.cpp
git commit -m "feat: EditorBridge 新增 requestImageSave 槽与 imageSaveRequested 信号"
```

---

## Task 3: CI 开启测试（ctest）

**Files:**
- Modify: `.github/workflows/build-deb.yml:33-38`

**Interfaces:**
- Produces: CI 在构建后运行 `ctest`；本地 `ctest` 也应通过（验证 test pipeline）。

- [ ] **Step 1: 改 `build-deb.yml`：开启 BUILD_TESTS 并新增「运行测试」步骤**

在 `/home/daijd/claude-workspace/md-editor/.github/workflows/build-deb.yml` 中，把第 33-38 行：

```yaml
    - name: 配置 CMake (Release)
      run: |
        cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF

    - name: 构建
      run: cmake --build build --parallel $(nproc)
```

替换为：

```yaml
    - name: 配置 CMake (Release)
      run: |
        cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON

    - name: 构建
      run: cmake --build build --parallel $(nproc)

    - name: 运行测试
      run: |
        cd build
        QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

> 说明：`QT_QPA_PLATFORM=offscreen` 让 QtTest 在无显示的 CI 上也能构造 `QCoreApplication`/`QGuiApplication`。现有三个测试均为纯逻辑（无 widget），offscreen 足够。

- [ ] **Step 2: 本地跑完整 ctest，确认全绿**

Run: `cd build && cmake -DBUILD_TESTS=ON .. && cmake --build . -j$(nproc) && QT_QPA_PLATFORM=offscreen ctest --output-on-failure`
Expected: `test_editorbridge`、`test_filemanager`、`test_markdownhandler`、`test_imagemanager` 全部 PASS。

- [ ] **Step 3: 提交**

```bash
git add .github/workflows/build-deb.yml
git commit -m "chore: CI 开启 BUILD_TESTS 并运行 ctest"
```

> 注：CI 实际触发在 push/PR 时；本步骤本地验证已足够确认改动正确。

---

## Task 4: MainWindow 落盘编排与回调（集成）

**Files:**
- Modify: `src/ui/MainWindow.h:11`（前向声明加 `class EditorTab;`）、`:65`（私有方法声明区）
- Modify: `src/ui/MainWindow.cpp:16`（include）、`:383-429`（`onEditorBridgeCreated`，第 421 行后新增连接）
- Modify: `src/ui/MainWindow.cpp`（顶部 include；新增 `onImageSaveRequested` 实现 + 文件静态 `jsEscape` / `jsImageSaveResult`）

**Interfaces:**
- Consumes: Task 1 的 `ImageManager::saveImage`；Task 2 的 `EditorBridge::imageSaveRequested`；Task 5 约定的 JS 回调 `window.editorApi._onImageSaveResult(requestId, {ok,path}|{ok:false,err})`。
- Produces: `MainWindow::onImageSaveRequested(...)`；文件静态 `jsImageSaveResult(...)`。

- [ ] **Step 1: 在 `MainWindow.cpp` 顶部加 include**

`src/ui/MainWindow.cpp` 第 8 行已 `#include "EditorTab.h"`（`tab->webView()`/`bridge()` 可用，无需另加）；core 头用 `"../core/..."` 形式（见第 14-18 行）。在 include 区（第 16 行 `#include "../core/MarkdownHandler.h"` 之后）追加：

```cpp
#include "../core/ImageManager.h"
```

- [ ] **Step 2: 在 `MainWindow.h` 加 `EditorTab` 前向声明 + 私有方法声明**

`MainWindow.h` 第 8-15 行有前向声明块，但**不含 `EditorTab`**（现有方法都没用到）。先在第 11 行 `class TabWidget;` 之后追加一行：

```cpp
class EditorTab;
```

再在 `private:` 区（第 65 行 `void runEditorCommand(const QString &js);` 之后）追加方法声明：

```cpp
    void onImageSaveRequested(EditorTab *tab, const QString &requestId,
                              const QString &base64, const QString &ext,
                              const QString &suggestedName);
```

> 仅前向声明 `EditorTab`（`.cpp` 已 include 完整头）；`EditorBridge` 已在第 14 行前向声明，可直接用于参数。

- [ ] **Step 3: 在 `onEditorBridgeCreated` 加信号连接**

在 `/home/daijd/claude-workspace/md-editor/src/ui/MainWindow.cpp` 第 421 行（`modalDialogStateChanged` 连接的 lambda 闭合 `});`）之后、第 422 行空行处，插入：

```cpp

    // 图片落盘请求（粘贴/拖拽）：落盘后回调 JS 把占位图 src 替换为相对路径
    connect(bridge, &EditorBridge::imageSaveRequested, this,
            [this, bridge](const QString &requestId, const QString &base64,
                           const QString &ext, const QString &suggestedName, int /*tabId*/) {
        // bridge 的 parent 即拥有它的 EditorTab（见 EditorTab 构造 m_bridge(new EditorBridge(tabId, this))）
        EditorTab *tab = qobject_cast<EditorTab *>(bridge->parent());
        if (tab) onImageSaveRequested(tab, requestId, base64, ext, suggestedName);
    });
```

- [ ] **Step 4: 在 `MainWindow.cpp` 实现 `onImageSaveRequested` 与辅助函数**

在 `onInsertImage()`（第 525-533 行）附近，先加两个**文件静态**辅助函数（实现之上、类成员之外；放在 `onInsertImage` 之前的匿名命名空间或 `static`）：

```cpp
// 转义为单引号 JS 字符串字面量内容（不含外层引号）
static QString jsEscape(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    out.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    out.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    return out;
}

// 构造图片落盘结果回调 JS：成功 {ok:true,path}；失败 {ok:false,err}
static QString jsImageSaveResult(const QString &requestId, bool ok,
                                 const QString &path, const QString &err)
{
    if (ok)
        return QStringLiteral("window.editorApi._onImageSaveResult('%1',{ok:true,path:'%2'});")
            .arg(jsEscape(requestId), jsEscape(path));
    return QStringLiteral("window.editorApi._onImageSaveResult('%1',{ok:false,err:'%2'});")
        .arg(jsEscape(requestId), jsEscape(err));
}
```

再加成员实现：

```cpp
void MainWindow::onImageSaveRequested(EditorTab *tab, const QString &requestId,
                                      const QString &base64, const QString &ext,
                                      const QString &suggestedName)
{
    QString filePath = tab->webView()->property("filePath").toString();
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        m_statusBar->showMessage("请先保存文档再插入图片", 3000);
        tab->runJsCommand(jsImageSaveResult(requestId, false, QString(), QStringLiteral("unsaved")));
        return;
    }
    QByteArray bytes = QByteArray::fromBase64(base64.toUtf8());
    if (bytes.isEmpty()) {
        m_statusBar->showMessage("图片数据为空", 3000);
        tab->runJsCommand(jsImageSaveResult(requestId, false, QString(), QStringLiteral("io")));
        return;
    }
    QString rel = ImageManager::saveImage(filePath, bytes, suggestedName, ext);
    if (rel.isEmpty()) {
        m_statusBar->showMessage("图片保存失败", 3000);
        tab->runJsCommand(jsImageSaveResult(requestId, false, QString(), QStringLiteral("io")));
        return;
    }
    m_statusBar->showMessage("已插入图片", 1500);
    tab->runJsCommand(jsImageSaveResult(requestId, true, rel, QString()));
}
```

- [ ] **Step 5: 构建确认编译通过**

Run: `cd build && cmake --build . -j$(nproc) 2>&1 | tail -20`
Expected: 编译成功，无错误。

- [ ] **Step 6: 提交**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat: MainWindow 编排图片落盘请求并回调 JS 替换占位图"
```

> 本 Task 为 Qt GUI 集成胶水，无独立单测；端到端验证在 Task 5 完成后手动进行（见 Task 5 Step 6 清单）。

---

## Task 5: editor.js 粘贴/拖拽拦截与回调（JS）

**Files:**
- Modify: `src/resources/web/editor.js:984-996`（`_createEditor` 的 config，加 `editorProps`：含 handlePaste/handleDrop 与 image NodeView）
- Modify: `src/resources/web/editor.js:887-897`（`editorApi` 对象字面量，加图片落盘相关字段/方法 + `_docDir`）
- Modify: `src/resources/web/editor.js:1273`（`insertImage` 附近，追加新方法：落盘流程 + `setDocDir` + `_resolveImageSrc`）
- Modify: `src/resources/web/editor.css`（占位图样式）
- Modify: `src/ui/EditorTab.h`（新增 `setFilePath` / `pushDocDir` 声明）
- Modify: `src/ui/EditorTab.cpp`（实现 `setFilePath` / `pushDocDir`，`onPageLoadFinished` 补推文档目录，加 `<QFileInfo>`）
- Modify: `src/ui/MainWindow.cpp:726,785,967`（3 处 `tab->webView()->setProperty("filePath", path)` 改为 `tab->setFilePath(path)`）

**Interfaces:**
- Consumes: Task 2 的 `bridge.requestImageSave(requestId, base64, ext, suggestedName)`；Task 4 约定的 `_onImageSaveResult(requestId, result)`；C++ 经 `runJavaScript` 注入的 `window.editorApi.setDocDir(dir)`。
- Produces: ProseMirror `editorProps.handlePaste/handleDrop`；`editorProps.nodeViews.image`（相对路径渲染为 `file://`）；`editorApi._onImageSaveResult`（被 C++ 回调）；`editorApi.setDocDir` / `_resolveImageSrc`（文档目录解析）。C++ 侧 `EditorTab::setFilePath(path)`（供 MainWindow 在打开/保存/恢复会话时调用，替代直接 setProperty）。

**设计要点（占位图 + 唯一 marker）**：handlePaste/handleDrop **同步**插入占位图（保证光标位置正确，即便后续异步读文件），src 用唯一 marker `__pending_<requestId>__`；异步读完文件后发 base64 给 C++；C++ 回调后按 marker 定位节点，成功则 `setNodeMarkup` 替换 src 为相对路径，失败则删除节点。marker 唯一故可抗「同一次粘贴多张相同图」。

**图片渲染（相对路径 → file://）**：编辑器页面从 `qrc:/web/editor.html` 加载，base 为 `qrc:/web/`，相对图片 src（如 `images/foo.png`）默认会解析到 `qrc:/web/images/foo.png`（不存在 → 碎图）。因此用自定义 image **NodeView**：节点 attrs.src 始终保存**相对路径**（保证 .md 可移植 + 往返保真，MarkdownHandler 不动），但渲染时经 `_resolveImageSrc` 解析为 `file://<文档目录>/<相对路径>` 才能显示。C++ 在文档打开/保存/会话恢复时经 `EditorTab::setFilePath(path)` 把文档目录推给 JS（`setDocDir`），`setDocDir` 会刷新已渲染图片的显示 src（处理「先有内容后到目录」与「另存为改目录」两种时序）。绝对路径/网络/内联 URL 与占位 marker 原样返回，既有 `insertImage`（绝对路径）行为不变。

- [ ] **Step 1: 在 `_createEditor` 的 config 加 `editorProps`**

在 `/home/daijd/claude-workspace/md-editor/src/resources/web/editor.js` 第 986 行 `extensions: this._buildExtensions(),` 之后、第 987 行 `onUpdate:` 之前，插入：

```javascript
            editorProps: {
                handlePaste: function(view, event) {
                    return window.editorApi._handleImageClipboard(view, event, false);
                },
                handleDrop: function(view, event) {
                    return window.editorApi._handleImageClipboard(view, event, true);
                },
                // 自定义图片 NodeView：attrs.src 存相对路径（保往返保真），渲染时解析为 file://
                nodeViews: {
                    image: function(node) {
                        var raw = node.attrs.src || '';
                        var img = document.createElement('img');
                        img.setAttribute('data-src', raw);
                        img.src = window.editorApi._resolveImageSrc(raw);
                        img.alt = node.attrs.alt || '';
                        if (node.attrs.title) img.title = node.attrs.title;
                        return {
                            dom: img,
                            update: function(n) {
                                if (n.type.name !== 'image') return false;
                                var nr = n.attrs.src || '';
                                if (img.getAttribute('data-src') !== nr) {
                                    img.setAttribute('data-src', nr);
                                    img.src = window.editorApi._resolveImageSrc(nr);
                                }
                                img.alt = n.attrs.alt || '';
                                if (n.attrs.title) img.title = n.attrs.title;
                                else img.removeAttribute('title');
                                return true;
                            }
                        };
                    }
                }
            },
```

- [ ] **Step 2: 在 `editorApi` 对象字面量加图片落盘状态字段**

在 `/home/daijd/claude-workspace/md-editor/src/resources/web/editor.js` 第 896 行 `_readOnly: false,` 之后追加：

```javascript
    _imageSeq: 0,            // 占位图 requestId 自增计数
    _imagePending: {},       // requestId -> marker（占位图 src，回调时据此定位节点）
    _docDir: '',             // 当前文档目录（绝对路径），把相对图片 src 解析为 file://
```

- [ ] **Step 3: 在 `editorApi` 追加图片落盘方法**

在 `insertImage`（第 1273 行）之后追加：

```javascript
    // === 图片粘贴/拖拽落盘 ===
    _mimeToExt: function(mime) {
        var map = { 'image/png':'png', 'image/jpeg':'jpg', 'image/jpg':'jpg',
                    'image/gif':'gif', 'image/webp':'webp', 'image/bmp':'bmp',
                    'image/svg+xml':'svg', 'image/x-icon':'ico', 'image/tiff':'tiff' };
        return map[mime] || 'png';
    },

    // ProseMirror handlePaste/handleDrop 共用入口
    _handleImageClipboard: function(view, event, isDrop) {
        if (!this.bridge) return false;                       // 桥未就绪，交默认
        var dt = isDrop ? event.dataTransfer : event.clipboardData;
        if (!dt || !dt.files || !dt.files.length) return false;
        var handled = false;
        for (var i = 0; i < dt.files.length; ++i) {
            var f = dt.files[i];
            if (!f.type || f.type.indexOf('image/') !== 0) continue;  // 仅图片
            handled = true;
            this._ingestImageFile(f);
        }
        if (handled) event.preventDefault();
        return handled;
    },

    _ingestImageFile: function(file) {
        var requestId = 'img_' + (++this._imageSeq);
        var marker = '__pending_' + requestId + '__';
        // 同步插入占位图（位置正确 + 即时反馈）
        if (this.editor) this.editor.chain().focus().setImage({ src: marker, alt: file.name || '' }).run();
        this._imagePending[requestId] = marker;

        var self = this;
        var reader = new FileReader();
        reader.onload = function() {
            var dataURL = reader.result || '';
            var base64 = dataURL.split(',')[1] || '';
            var ext = self._mimeToExt(file.type);
            try {
                self.bridge.requestImageSave(requestId, base64, ext, file.name || '');
            } catch (e) {                                 // 桥异常：清理占位图
                self._removeImageBySrc(marker);
                delete self._imagePending[requestId];
            }
        };
        reader.readAsDataURL(file);
    },

    _removeImageBySrc: function(src) {
        if (!this.editor) return;
        var pos = null, size = 0;
        this.editor.state.doc.descendants(function(node, p) {
            if (pos !== null) return false;
            if (node.type.name === 'image' && node.attrs.src === src) { pos = p; size = node.nodeSize; return false; }
            return true;
        });
        if (pos !== null) this.editor.view.dispatch(this.editor.state.tr.delete(pos, pos + size));
    },

    // C++ 回调：成功替换占位图 src；失败删除占位图
    _onImageSaveResult: function(requestId, result) {
        var marker = this._imagePending[requestId];
        delete this._imagePending[requestId];
        if (!this.editor || !marker) return;

        var pos = null, node = null;
        this.editor.state.doc.descendants(function(n, p) {
            if (node) return false;
            if (n.type.name === 'image' && n.attrs.src === marker) { node = n; pos = p; return false; }
            return true;
        });
        if (!node) return;

        if (result && result.ok && result.path) {
            var attrs = Object.assign({}, node.attrs, { src: result.path });
            this.editor.view.dispatch(this.editor.state.tr.setNodeMarkup(pos, null, attrs));
        } else {
            this.editor.view.dispatch(this.editor.state.tr.delete(pos, pos + node.nodeSize));
        }
    },

    // C++ 在打开/保存/会话恢复时注入文档目录；并刷新已渲染图片的显示 src
    setDocDir: function(dir) {
        this._docDir = dir || '';
        var root = this.editor && this.editor.view && this.editor.view.dom;
        if (!root) return;
        var imgs = root.querySelectorAll('img[data-src]');
        for (var i = 0; i < imgs.length; ++i) {
            imgs[i].src = this._resolveImageSrc(imgs[i].getAttribute('data-src'));
        }
    },

    // 相对路径 → file://<文档目录>/<相对>；占位 marker / 绝对路径 / 网络 URL 原样返回
    _resolveImageSrc: function(raw) {
        if (!raw) return '';
        if (raw.indexOf('__pending_') === 0) return raw;              // 占位图 marker
        if (/^(data:|blob:|https?:|file:|qrc:)/i.test(raw)) return raw; // 带协议的 URL
        if (raw.charAt(0) === '/') return raw;                         // 绝对路径：保持既有 insertImage 行为
        var base = this._docDir || '';
        if (!base) return raw;                                         // 文档目录未知（未保存）：原样返回
        return 'file://' + base.replace(/[\\/]+$/, '') + '/' + raw.replace(/^[\\/]+/, '');
    },
```

- [ ] **Step 4: 在 `editor.css` 加占位图加载样式**

在 `/home/daijd/claude-workspace/md-editor/src/resources/web/editor.css` 末尾追加：

```css

/* 图片粘贴/拖拽落盘：落盘回调期间的占位图（marker src） */
.ProseMirror img[src^="__pending_"] {
    display: inline-block;
    min-width: 96px;
    min-height: 96px;
    background: rgba(128, 128, 128, 0.15);
    border: 1px dashed #999;
    vertical-align: middle;
}
```

> 亮/暗主题通用（半透明灰 + 虚线框）；仅占位图短暂显示，落盘后即被真实图片替换。

- [ ] **Step 5: C++ 文档目录管线（EditorTab::setFilePath + MainWindow 接线）**

把文档目录推给 JS，让 NodeView 能把相对图片 src 解析为 `file://`。改动三个文件：

**5a. `src/ui/EditorTab.h`**：在 `void loadFile(const QString &path, const QString &content);`（第 38 行）之后追加公有声明：

```cpp
    void setFilePath(const QString &path);
```

并在 `void setupWebChannel();`（第 59 行）之后追加私有声明：

```cpp
    void pushDocDir();
```

**5b. `src/ui/EditorTab.cpp`**：include 区追加 `#include <QFileInfo>`；在 `loadFile` 实现之后追加 `setFilePath` 与 `pushDocDir` 实现：

```cpp
void EditorTab::setFilePath(const QString &path)
{
    m_webView->setProperty("filePath", path);
    pushDocDir();   // 页面已就绪则立即推送；否则由 onPageLoadFinished 补推
}

void EditorTab::pushDocDir()
{
    QString path = m_webView->property("filePath").toString();
    if (path.isEmpty() || !m_pageLoaded) return;
    QString dir = QFileInfo(path).absolutePath();
    QString esc = dir;
    esc.replace("\\", "\\\\").replace("'", "\\'");
    runJsCommand(QString("window.editorApi.setDocDir('%1');").arg(esc));
}
```

并把 `onPageLoadFinished` 改为页面就绪后补推文档目录（新增 `if (ok) pushDocDir();` 一行，置于 `emit` 之前）：

```cpp
void EditorTab::onPageLoadFinished(bool ok)
{
    m_pageLoaded = ok;
    // 新标签页加载完成：补推文档目录（先于 TabWidget 的 setContent，图片首渲即用对目录）
    if (ok) pushDocDir();
    emit pageLoadFinished(ok);
}
```

**5c. `src/ui/MainWindow.cpp`**：把三处 `tab->webView()->setProperty("filePath", path);`（第 726、785、967 行，分别在 onFileDoubleClicked 打开、保存成功、会话恢复打开）改为：

```cpp
    tab->setFilePath(path);
```

> 时序说明：打开文档时 `TabWidget::openFileTab` 异步加载页面 → 紧接 `setFilePath`（页面未就绪，仅记属性）→ 页面 `loadFinished` → `onPageLoadFinished`（`pushDocDir` 先于 `emit pageLoadFinished`）→ TabWidget 的 lambda 调 `loadFile`→`setContent` 渲染图片，此时 `setDocDir` 已注入（同为 `runJavaScript` 队列，先于 `setContent` 执行），NodeView 首渲即正确。即便时序倒置，`setDocDir` 也会刷新已渲染图片。

- [ ] **Step 6: 重新构建并运行（editor.js 经 qrc 重新打包；C++ 重编）**

Run: `cd build && cmake --build . -j$(nproc) 2>&1 | tail -10`
Expected: 构建成功（AUTORCC 自动重新打包 `editor.js` / `editor.css`；EditorTab/MainWindow 重编链接通过）。

- [ ] **Step 7: 端到端手动验证清单**

运行 `./scripts/run-editor.sh`，逐项验证：

- [ ] 打开一个**已保存**的 .md 文件（状态栏有路径）。从剪贴板粘贴一张截图 → 图片出现；切源码模式（或保存后用文本编辑器打开 .md）可见 `![](images/image-<时间戳>.png)`；磁盘上 `<文档目录>/images/` 真有该文件。
- [ ] 从文件管理器拖一张 `photo.png` 到编辑器 → 图片出现，保存后 MD 为 `![](images/photo.png)`，保留原名。
- [ ] 再拖一次同名 `photo.png` → 第二个存为 `images/photo-2.png`。
- [ ] 新建未保存标签页 → 粘贴图片 → 状态栏提示「请先保存文档」，编辑区无占位图残留（占位图被删除）。
- [ ] 拖一个**非图片**文件（如 .txt）到编辑器 → 不触发图片逻辑（交默认，不插入）。
- [ ] 保存后**关闭再重新打开**该 .md → 图片正常显示（NodeView 经 `file://<文档目录>/images/...` 解析）。
- [ ] **另存为**到不同目录：原 `images/` 不随文档迁移 → 新目录下既有相对图片引用失效（碎图），属**已知限制**（见文末「已知限制」）。仅当 .md 与 `images/` 一同移动/改名（相对位置不变）时图片仍正常。
- [ ] 工具栏「插入图片」选一张**绝对路径**图片 → 仍正常显示（回归：绝对路径行为不变）。
- [ ] 粘贴含**纯文本**（无图片）的剪贴板 → 正常粘贴文本，不触发图片落盘。

- [ ] **Step 8: 提交**

```bash
git add src/resources/web/editor.js src/resources/web/editor.css \
        src/ui/EditorTab.h src/ui/EditorTab.cpp src/ui/MainWindow.cpp
git commit -m "feat: 粘贴/拖拽图片落盘 + 自定义 NodeView 把相对路径渲染为 file://"
```

---

## 自审（Self-Review）

- **Spec 覆盖（功能一 §2.1–2.5）**：
  - §2.1 requestId 回调协议 → Task 2（slot/signal）+ Task 4（编排/回调）+ Task 5（JS 注册表 `_imagePending`）。✓
  - §2.2 拖拽分层（窗口级仍只接 .md，图片走 ProseMirror handleDrop）→ Task 5 Step 1（不改 MainWindow DnD）。✓
  - §2.3 组件（ImageManager / EditorBridge slot+signal / MainWindow / editor.js handlePaste/handleDrop + image NodeView）→ Task 1/2/4/5。✓
  - §2.4 错误处理（未保存/IO/非图片/空数据）→ Task 4 Step 4（`onImageSaveRequested` 分支）+ Task 5（`_handleImageClipboard` 仅图片）。✓ 超大图阈值留 §8 未决项，本计划暂不卡（与 spec 一致）。
  - §2.5 测试（ImageManager QtTest + bridge 转发）→ Task 1/2。✓
- **类型一致性**：`requestImageSave(requestId, base64, ext, suggestedName)` 与 `imageSaveRequested(...,tabId)` 在 Task 2/4/5 三处签名一致；`ImageManager::saveImage(docFilePath, bytes, suggestedName, fallbackExt)` 在 Task 1/4 一致；JS `_onImageSaveResult(requestId,{ok,path}|{ok:false,err})` 与 Task 4 `jsImageSaveResult` 一致。✓
- **占位符扫描**：无 TBD/TODO；每个代码步骤均含真实代码。✓
- **范围**：本计划仅「图片落盘」。图片缩放（Plan B）、搜索（Plan C）独立，不在内。✓
- **图片渲染机制**（spec 未显式规定，但「图片正常显示」隐含要求）：相对 src 在 qrc 页面（base=`qrc:/web/`）无法直接解析 → Task 5 用 image NodeView 把 attrs.src(相对) 经 `_resolveImageSrc` 解析为 `file://<文档目录>/...`，C++ 经 `EditorTab::setFilePath`→`setDocDir` 推文档目录。attrs.src 始终存相对路径，MarkdownHandler 不动 → 往返保真不受影响。✓

## 已知限制（非 bug，spec §8 未决项）

- 超大图阈值未实现（暂不卡 >20MB）。
- 占位图加载期间显示虚线灰框（约数十 ms），落盘后替换为真实图。
- **「另存为」到不同目录不迁移图片**：另存为只写 .md，不会把 `images/`（及被引用的图片文件）复制到新目录 → 新目录下既有相对引用失效（碎图）。workaround：手动把 `images/` 一并复制到新目录；或在原目录内改名/整体移动 .md + images/。后续可作独立增强（save-as 时迁移引用的图片）。
