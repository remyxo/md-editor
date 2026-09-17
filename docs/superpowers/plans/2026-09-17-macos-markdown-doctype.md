# macOS 声明支持 Markdown 文档类型 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 macOS 上 md-editor 在「打开方式」推荐列表灰色的问题，并补齐双击/「打开方式」打开文件的完整链路（QFileOpenEvent 处理）。

**Architecture:** 打包侧把 `CFBundleDocumentTypes` 从纯扩展名匹配升级为 `LSItemContentTypes` 指向系统 UTI `net.daringfireball.markdown`（删除与系统冲突的自定义 UTI）；C++ 侧 `Application` 重写 `event()` 捕获 `QEvent::FileOpen` 转发信号，`MainWindow` 复用既有 `onFileDoubleClicked` 打开逻辑。规格见 `docs/superpowers/specs/2026-09-17-macos-markdown-doctype-design.md`。

**Tech Stack:** Qt 6（QApplication/QFileOpenEvent/QSignalSpy）、PlistBuddy（macOS 打包脚本）、QtTest。

## Global Constraints

- 注释与文档一律中文，代码标识符英文（CLAUDE.md 原则 1）
- 版本号唯一定义于根 `CMakeLists.txt` 的 `project(md-editor VERSION x.y.z)`；`MD_EDITOR_VERSION` 宏经根 `add_compile_definitions` 全局生效，测试目标自动继承，无需重复定义
- 提交规范：`fix:` / `chore:` 前缀 + 中文描述
- 本地构建目录为 `build/`（已 configure）；跑测试必须带 `QT_QPA_PLATFORM=offscreen`（CI 同款，`.github/workflows/build-deb.yml:50`）
- `QFileOpenEvent` / `QSignalSpy` API 在 Qt 6.8.2–6.10.2 稳定（Qt 6.0+），无需版本分支
- **工作区有既有未提交改动**（CdpPrinter/PDF 相关，含 `src/ui/MainWindow.cpp`、`tests/CMakeLists.txt`——本计划也要改这两个文件）：每任务只 stage 自己的文件；共享文件提交前必须 `git diff --cached` 复核无混入；无法干净切分时停下来问用户
- 改 C++ 后必须重编译（qrc/二进制进产物，CLAUDE.md 规则 9）
- 推送预检需 hooks 已启用：`git config core.hooksPath .githooks`（一次性）

---

### Task 1: 前置——工作区状态确认

**Files:** 无改动。

**Interfaces:**
- Consumes: 无
- Produces: 干净可切分的工作区（后续任务的提交边界前提）

- [ ] **Step 1: 查看工作区既有改动**

```bash
git status --short
```

预期：存在 CdpPrinter/PDF 相关未提交改动（`src/core/CdpPrinter.*`、`src/core/PdfExporter.cpp`、`src/ui/MainWindow.cpp`、`tests/CMakeLists.txt` 等）。

- [ ] **Step 2: 确认共享文件的改动内容**

```bash
git diff src/ui/MainWindow.cpp tests/CMakeLists.txt
```

记录这两个文件中与本计划无关的 hunk（后续 Task 2/3 提交时须避开）。若改动过大无法切分（如 MainWindow.cpp 的 diff 与本计划的连接点强耦合），停下来询问用户如何处置（先提交那批改动 / stash / 其他）。

---

### Task 2: Application 捕获 QFileOpenEvent（TDD）

**Files:**
- Create: `tests/test_application.cpp`
- Modify: `tests/CMakeLists.txt`（文件末尾追加）
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`

**Interfaces:**
- Consumes: 无
- Produces: `Application` 新增信号 `void fileOpenRequested(const QString &path)`（Task 3 的 MainWindow 连接依赖此签名）；`event()` 重写为 `protected: bool event(QEvent *e) override`

- [ ] **Step 1: 写失败测试 + CMake 目标**

新建 `tests/test_application.cpp`，内容如下：

```cpp
#include <QtTest>
#include <QFileOpenEvent>
#include "app/Application.h"

// Application::event() 对 QEvent::FileOpen 的转发（macOS Finder 双击/「打开方式」链路，
// 文件经 Apple Event 到达而非 argv）。QTEST_APPLESS_MAIN：不创建默认 QApplication，
// 由测试自建 Application 实例（仿 test_pdfexporter 的自建 app 模式）。
// ThemeManager::loadThemeFile 对 qrc 缺失静默容错（open 失败即跳过），
// 测试目标无需链接资源文件。
class TestApplication : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void fileOpenEventEmitsSignal();
    void emptyFileOpenEventEmitsNothing();

private:
    Application *m_app = nullptr;
};

void TestApplication::initTestCase()
{
    // argv 缓冲须比 Application 存续期长（QApplication 可能消费 argv 内容）
    static char argv0[] = "test_application";
    static char *argv[] = { argv0, nullptr };
    int argc = 1;
    m_app = new Application(argc, argv);
}

void TestApplication::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void TestApplication::fileOpenEventEmitsSignal()
{
    QSignalSpy spy(m_app, &Application::fileOpenRequested);
    QCoreApplication::postEvent(m_app,
        new QFileOpenEvent(QStringLiteral("/tmp/md-editor-claude/test.md")));
    QCoreApplication::sendPostedEvents();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("/tmp/md-editor-claude/test.md"));
}

void TestApplication::emptyFileOpenEventEmitsNothing()
{
    QSignalSpy spy(m_app, &Application::fileOpenRequested);
    QCoreApplication::postEvent(m_app, new QFileOpenEvent(QString()));
    QCoreApplication::sendPostedEvents();
    QCOMPARE(spy.count(), 0);
}

QTEST_APPLESS_MAIN(TestApplication)
#include "test_application.moc"
```

在 `tests/CMakeLists.txt` 末尾追加（注意该文件已有他人未提交改动，只追加，不动既有行）：

```cmake
# Application 的 QFileOpenEvent 转发（macOS Finder 打开文件链路）。
# ThemeManager 对 qrc 缺失静默容错，无需链接资源；LanguageManager 用到
# QApplication，须链接 Qt6::Widgets（同 test_languagemanager）
add_executable(test_application test_application.cpp
    ../src/app/Application.cpp ../src/app/ThemeManager.cpp ../src/app/LanguageManager.cpp)
target_include_directories(test_application PRIVATE ../src)
target_link_libraries(test_application PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME test_application COMMAND test_application)
```

- [ ] **Step 2: 构建确认失败（RED）**

```bash
cmake --build build --target test_application -j$(nproc) 2>&1 | tail -5
```

预期：编译错误，形如 `'fileOpenRequested' is not a member of 'Application'`（信号尚不存在）。

- [ ] **Step 3: 实现 Application 信号与 event() 重写**

`src/app/Application.h` 整体替换为：

```cpp
#ifndef MD_EDITOR_APPLICATION_H
#define MD_EDITOR_APPLICATION_H

#include <QApplication>
#include "LanguageManager.h"
#include "ThemeManager.h"

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    static Application *instance();
    ThemeManager *themeManager() const;
    LanguageManager *languageManager() const;

signals:
    // macOS：Finder 双击/「打开方式」的文件经 Apple Event → QEvent::FileOpen 到达
    //（不走 argv——argv 只覆盖 Linux .desktop %f / Windows 场景），
    // 由此信号转发给 MainWindow 按既有路径打开
    void fileOpenRequested(const QString &path);

protected:
    bool event(QEvent *e) override;

private:
    ThemeManager *m_themeManager;
    LanguageManager *m_languageManager;
};

#endif // MD_EDITOR_APPLICATION_H
```

`src/app/Application.cpp`：include 区加 `#include <QFileOpenEvent>`（放在既有 `#include "Application.h"` 之后），文件末尾（`instance()` 实现之后）追加：

```cpp
bool Application::event(QEvent *e)
{
    if (e->type() == QEvent::FileOpen) {
        // 空 FileOpen（Apple Event 不带文件的个别场景）静默吞掉，不向下传递
        const QString path = static_cast<QFileOpenEvent *>(e)->file();
        if (!path.isEmpty())
            emit fileOpenRequested(path);
        return true;
    }
    return QApplication::event(e);
}
```

- [ ] **Step 4: 构建并跑测试（GREEN）**

```bash
cmake --build build --target test_application -j$(nproc) && \
QT_QPA_PLATFORM=offscreen ./build/tests/test_application
```

预期：2 个测试全过（`fileOpenEventEmitsSignal`、`emptyFileOpenEventEmitsNothing`）。

- [ ] **Step 5: 全量回归**

```bash
cmake --build build -j$(nproc) && cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure; cd ..
```

预期：既有测试全过（Application 改动是纯增量，无行为变化）。

- [ ] **Step 6: 提交（只 stage 本任务文件，tests/CMakeLists.txt 需复核缓存区）**

```bash
git add tests/test_application.cpp src/app/Application.h src/app/Application.cpp tests/CMakeLists.txt
git diff --cached tests/CMakeLists.txt   # 复核：只含 test_application 目标追加，无 CdpPrinter 相关混入
git commit -m "fix: Application 捕获 QFileOpenEvent——macOS 打开方式的文件此前无人处理

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

若 `git diff --cached` 发现混入他人 hunk：`git restore --staged tests/CMakeLists.txt` 后改用「手工构造补丁」方式（`git diff tests/CMakeLists.txt` 导出本任务 hunk → `git apply --cached` 精确入缓存区），仍不行则停下问用户。

---

### Task 3: MainWindow 接线 fileOpenRequested

**Files:**
- Modify: `src/ui/MainWindow.cpp`（`connectSignals()` 内，约 470 行「文件树」连接之后；`../app/Application.h` 已在文件头引入，无需加 include）

**Interfaces:**
- Consumes: `Application::fileOpenRequested(const QString &path)` 信号（Task 2 产出）、`Application::instance()` 静态方法（既有）
- Produces: 无（终端消费点）

- [ ] **Step 1: 添加连接**

在 `src/ui/MainWindow.cpp` 的 `connectSignals()` 中，找到：

```cpp
    // === 文件树 ===
    connect(m_sideBar->fileTree(), &FileTree::fileDoubleClicked,
            this, &MainWindow::onFileDoubleClicked);
```

在其后追加：

```cpp
    // === macOS：Finder 双击/「打开方式」的文件 ===
    // QFileOpenEvent → Application::fileOpenRequested（该链路不经过 argv），
    // 复用文件树双击的打开逻辑（存在性检查/开标签/大文件警告全部沿用）
    connect(Application::instance(), &Application::fileOpenRequested,
            this, &MainWindow::onFileDoubleClicked);
```

- [ ] **Step 2: 构建验证 + 全量回归**

```bash
cmake --build build -j$(nproc) && cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure; cd ..
```

预期：编译通过，测试全过。此连接无法无头自动化（需真实 Apple Event），行为验证在 Task 6 真机进行。

- [ ] **Step 3: 确认二进制已更新（CLAUDE.md 规则 9）**

```bash
ls -la build/src/md-editor
```

预期：二进制时间戳新于 `src/ui/MainWindow.cpp` / `src/app/Application.cpp` 的修改时间。

- [ ] **Step 4: 提交（MainWindow.cpp 有他人未提交改动，须精确切分）**

```bash
git diff src/ui/MainWindow.cpp          # 先看本任务 hunk 与他人 hunk 的边界
git add src/ui/MainWindow.cpp
git diff --cached src/ui/MainWindow.cpp # 复核：缓存区只含 connectSignals 的 4 行追加
```

若混入他人 hunk：`git restore --staged src/ui/MainWindow.cpp`，改用「导出本任务 hunk → `git apply --cached`」精确入缓存区，仍不行则停下问用户。复核通过后：

```bash
git commit -m "fix: MainWindow 接线 fileOpenRequested 到既有打开逻辑——macOS 双击文件不再空窗

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 4: package-macos.sh 文档类型声明对齐系统 UTI

**Files:**
- Modify: `scripts/package-macos.sh:20-50`（1.5 节整体替换）

**Interfaces:**
- Consumes: 无
- Produces: 注入后的 `Info.plist` 含 `CFBundleDocumentTypes[0].LSItemContentTypes = [net.daringfireball.markdown]`；不再有 `UTImportedTypeDeclarations`

- [ ] **Step 1: 替换 1.5 节**

将 `scripts/package-macos.sh` 中从 `# 1.5 注入 Markdown 文档类型声明` 起到 `"$PLBUDDY" -c "Add :UTImportedTypeDeclarations:0:UTTypeIconFile string text-markdown" "$PLIST"` 止的整段，替换为：

```bash
# 1.5 注入 Markdown 文档类型声明：LSItemContentTypes 指向系统 UTI
#     net.daringfireball.markdown——「打开方式」推荐列表按 UTI 匹配，只声明扩展名
#     会被灰掉（v0.16.0 及之前的现象，需切「全部应用」才能选）。Typora/MacDown/
#     Obsidian 均声明该系统 UTI。CFBundleTypeExtensions 保留作老系统
#     （macOS 11- 无内建 md UTI）的扩展名兜底。不再注入自定义
#     UTImportedTypeDeclarations（com.md-editor.markdown 与系统 UTI 争抢同一
#     扩展名，类型解析与图标绑定不稳定）。
#     icon 在 Contents/Resources/text-markdown.icns（src/CMakeLists.txt 打包）。
#     CFBundleTypeRole=Editor + LSHandlerRank=Owner：声明本应用为 md 默认处理者
#     （最终默认与否仍由用户「打开方式」决定，macOS 按此机制授权）。
PLIST="$APP/Contents/Info.plist"
PLBUDDY="/usr/libexec/PlistBuddy"
# 先删后加保证幂等（重复打包不报 key 已存在；Delete UTImportedTypeDeclarations
# 亦防御旧版产物残留）
"$PLBUDDY" -c "Delete :CFBundleDocumentTypes" "$PLIST" 2>/dev/null || true
"$PLBUDDY" -c "Delete :UTImportedTypeDeclarations" "$PLIST" 2>/dev/null || true
"$PLBUDDY" -c "Add :CFBundleDocumentTypes array" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0 dict" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeName string 'Markdown 文档'" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeRole string Editor" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:LSHandlerRank string Owner" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:LSItemContentTypes array" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:LSItemContentTypes:0 string net.daringfireball.markdown" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions array" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions:0 string md" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions:1 string markdown" "$PLIST"
"$PLBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeIconFile string text-markdown" "$PLIST"
# 断言注入成功（key 缺失时 PlistBuddy 非零退出，set -e 阻断打包）
"$PLBUDDY" -c "Print :CFBundleDocumentTypes:0:LSItemContentTypes" "$PLIST" >/dev/null
```

- [ ] **Step 2: 语法检查**

```bash
bash -n scripts/package-macos.sh && echo "语法 OK"
```

预期：输出「语法 OK」。脚本实际执行需 macOS（macdeployqt/PlistBuddy），真实验证在 Task 6 经 CI 完成。

- [ ] **Step 3: 提交**

```bash
git add scripts/package-macos.sh
git commit -m "fix: macOS 文档类型声明对齐系统 UTI net.daringfireball.markdown——推荐列表不再灰色

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 5: 版本推进 0.16.1 + CHANGELOG

**Files:**
- Modify: `CMakeLists.txt:2`（`project(md-editor VERSION 0.16.0 ...)` → `0.16.1`）
- Modify: `CHANGELOG.md`（新条目插在 `## [0.16.0] — 2026-09-15` 之前）

**Interfaces:**
- Consumes: Task 2-4 已完成（CHANGELOG 条目描述其内容）
- Produces: 版本 0.16.1（Task 6 发布的 tag 依据）

- [ ] **Step 1: 推进版本号**

`CMakeLists.txt` 中 `project(md-editor VERSION 0.16.0 ...)` 的 `0.16.0` 改为 `0.16.1`（增量小、bug 修复性质，按 CLAUDE.md 规则取 `+0.0.1`）。

- [ ] **Step 2: CHANGELOG 补录**

在 `CHANGELOG.md` 的 `## [0.16.0] — 2026-09-15` 之前插入：

```markdown
## [0.16.1] — 2026-09-17

### 修复 — macOS
- **「打开方式」推荐列表灰色** — CFBundleDocumentTypes 此前只按扩展名声明（CFBundleTypeExtensions），现代 macOS 推荐列表按 UTI 匹配，md-editor 被灰掉、需切「全部应用」才能选。补 `LSItemContentTypes: net.daringfireball.markdown`（系统 UTI，Typora/MacDown 同款）；删除与系统争抢扩展名的自定义 UTI（`com.md-editor.markdown`）；CFBundleTypeExtensions 保留作老系统兜底；打包脚本注入后加 PlistBuddy 断言
- **双击 .md 打开后窗口空白** — macOS 上文件路径经 Apple Event → QFileOpenEvent 到达而非 argv，此前无人处理。Application 捕获 FileOpen 发 `fileOpenRequested`，MainWindow 复用 `onFileDoubleClicked` 打开（冷启动与运行中再打开均覆盖）；新增 test_application 回归
```

- [ ] **Step 3: 重新生成构建系统并回归（版本号进二进制）**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc) && \
cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure; cd ..
```

预期：configure 识别新版本，测试全过。

- [ ] **Step 4: 提交**

```bash
git add CMakeLists.txt CHANGELOG.md
git diff --cached   # 复核：CMakeLists.txt 若混入他人未提交改动则精确切分（同 Task 3 手法）
git commit -m "chore: 0.16.1 版本推进——CHANGELOG 补录 macOS 文档类型修复

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 6: 真机验证与发布（用户主导，不可自动完成）

**Files:** 无代码改动。

**Interfaces:**
- Consumes: Task 2-5 的全部产出（master 上的 4 个提交）
- Produces: v0.16.1 发布（tag + 四平台制品）

- [ ] **Step 1: 推送（触发预检）**

```bash
git push origin master
```

含 C++ 改动 → pre-push 钩子自动跑容器 clang 检查 + win11 VM MSVC 编译（`git config core.hooksPath .githooks` 未启用则先执行一次）。编译错误硬阻断；环境故障警告放行。逃生门：`git push --no-verify`。若遇 GnuTLS -110，仓库已局部配 HTTP/1.1，重试即可。

- [ ] **Step 2: 手动 dispatch macOS 构建**

```bash
gh workflow run build-macos.yml --ref master
```

完成后从 Artifacts 下载 `md-editor-0.16.1-macos-universal.dmg`，CI 日志中 PlistBuddy 断言通过（Task 4 的 Step 1 断言随打包生效）。

- [ ] **Step 3: 真机验证清单（请用户在 macOS 上执行）**

1. 安装新 DMG（覆盖旧版）
2. 右键 .md →「打开方式」→ 推荐列表应出现 md-editor 且**不灰**
3. 选中打开 → 文件正常加载到编辑器
4. 「始终以此方式打开」后双击 .md → 直接打开并加载
5. 应用运行中再双击另一个 .md → 应开新标签
6. 若旧版（v0.16.0）注册记录干扰（仍灰/不出现）：`/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f /Applications/md-editor.app` 强刷后重试

- [ ] **Step 4: 用户确认后打 tag 发布**

真机验证全部通过、用户明确同意后：

```bash
git tag v0.16.1 && git push origin v0.16.1
```

tag 触发 DEB/Windows 构建；macOS 包用 Step 2 已产出的 DMG（或再 dispatch 一次带 tag 的）。Release 页挂三/四制品与说明（沿用 v0.16.0 流程）。**未经用户确认不得打 tag。**

---

## Self-Review 记录

- **规格覆盖**：spec 一（plist 形态+断言）→ Task 4；spec 二（三文件事件流）→ Task 2/3；spec 三（单测/CI 断言/真机清单）→ Task 2、Task 4 Step 1 断言、Task 6 Step 3；版本推进 → Task 5/6。无遗漏
- **占位符扫描**：无 TBD/TODO；所有代码步骤含完整代码；所有命令含预期输出
- **类型一致性**：信号签名 `fileOpenRequested(const QString &path)` 在 Task 2（定义）、Task 3（连接）、测试（QSignalSpy）三处一致；`onFileDoubleClicked(const QString &path)` 既有签名匹配
