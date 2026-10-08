# 插入菜单公式项改造 + 导出 DOCX 快捷键 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 插入菜单「公式」改名「行内公式」、其下新增「块级公式」(Ctrl+Shift+M) 菜单项；导出 DOCX / HTML 增加 Ctrl+Shift+D / Ctrl+Shift+H 快捷键（HTML 为 2026-10-08 追加需求）。

**Architecture:** 纯 UI 接线。块级公式能力（`editorApi.insertMath('block')`）早已存在，本次把 Ctrl+Shift+M 从 MainWindow 的 `QShortcut` 迁到新菜单 QAction（与 Ctrl+M 同一先例；双注册会报 "Ambiguous shortcut"），新增 `MenuBar::insertMathBlock` 信号由 MainWindow 连到既有 JS API。新增 `tests/test_menubar.cpp` 锁定菜单契约（文案/快捷键/邻接/信号）。

**Tech Stack:** Qt6 Widgets（QMenuBar/QAction）、Qt Test、en_US.ts（qt6_add_translations 编译期 lrelease）、CTest；无头端到端验证复用 `scripts/srcmode-md-probe.js` 骨架（Xvfb + python3-xlib XTEST + CDP）。

**Spec:** `docs/superpowers/specs/2026-10-08-insert-menu-math-docx-shortcut-design.md`

## Global Constraints

- 所有文档与注释使用中文，代码标识符用英文
- 快捷键取值（精确）：行内公式 `Ctrl+M`（不变）、块级公式 `Ctrl+Shift+M`、导出 DOCX `Ctrl+Shift+D`、导出 HTML `Ctrl+Shift+H`
- 不推进版本号、不动 CHANGELOG（用户未要求；版本推进须用户明确指示）
- C++ / qrc 资源改动后必须重新编译（`cmake --build build`）；告知用户「已修复」前确认二进制时间戳新于全部改动源文件
- **提交门：用户目视验证通过前不提交**（用户惯例；除非用户明确指示提交）
- 无头验证须强制 xcb（`env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb`），XTEST 注入在 Wayland 下不生效
- 临时文件一律放 `/tmp/md-editor-claude-20261008/`（多 harness 并行防同名覆盖）

---

### Task 1: MenuBar 菜单改造 + test_menubar 单元测试

**Files:**
- Create: `tests/test_menubar.cpp`
- Modify: `tests/CMakeLists.txt`（`test_collapsiblesection` 目标块之后追加）
- Modify: `src/ui/MenuBar.h`（信号 + 成员变量）
- Modify: `src/ui/MenuBar.cpp`（setupUi 141-143 行、48-49 行；retranslateUi 222 行）

**Interfaces:**
- Consumes: 无（首个任务）
- Produces（供 Task 2/4 依赖，精确签名）：
  - 信号 `void MenuBar::insertMathBlock()`（无参）
  - 插入菜单动作「行内公式(&M)」→ 信号 `insertMath`，快捷键 Ctrl+M
  - 插入菜单动作「块级公式(&B)」（紧邻行内公式之下）→ 信号 `insertMathBlock`，快捷键 Ctrl+Shift+M
  - 文件→导出菜单动作「导出 DOCX...」快捷键 Ctrl+Shift+D（信号 `exportDocx` 不变）
  - 文件→导出菜单动作「导出 HTML...」快捷键 Ctrl+Shift+H（信号 `exportHtml` 不变）

- [ ] **Step 1: 写失败测试（新建 `tests/test_menubar.cpp`，全文如下）**

```cpp
#include <QtTest>
#include "ui/MenuBar.h"
#include <QAction>
#include <QMenu>
#include <QSignalSpy>

// 插入菜单公式入口（行内/块级）与导出 DOCX 快捷键的菜单契约回归。
// 背景：块级公式的 Ctrl+Shift+M 原由 MainWindow QShortcut 承载，本次改由
// 菜单 QAction 携带（双注册会报 "Ambiguous shortcut"）——本测试锁定菜单侧
// 文案/快捷键/邻接次序/信号，防止改名或接线回归。
class TestMenuBar : public QObject {
    Q_OBJECT
private slots:
    void insertMenu_inlineAndBlockMath();
    void export_shortcuts();
    void mathActions_emitSignals();

private:
    static QMenu *menuByTitle(QMenuBar &mb, const QString &title);
    static QAction *actionByText(QMenuBar &mb, const QString &text);
};

QMenu *TestMenuBar::menuByTitle(QMenuBar &mb, const QString &title)
{
    const QList<QMenu *> menus = mb.findChildren<QMenu *>();
    for (QMenu *m : menus) {
        if (m->title() == title) return m;
    }
    return nullptr;
}

QAction *TestMenuBar::actionByText(QMenuBar &mb, const QString &text)
{
    const QList<QMenu *> menus = mb.findChildren<QMenu *>();
    for (QMenu *m : menus) {
        const QList<QAction *> acts = m->actions();
        for (QAction *a : acts) {
            if (a->text() == text) return a;
        }
    }
    return nullptr;
}

void TestMenuBar::insertMenu_inlineAndBlockMath()
{
    MenuBar mb;
    QMenu *insert = menuByTitle(mb, QStringLiteral("插入(&I)"));
    QVERIFY2(insert, "未找到插入菜单");

    const QList<QAction *> acts = insert->actions();
    int inlineIdx = -1, blockIdx = -1;
    for (int i = 0; i < acts.size(); ++i) {
        if (acts[i]->text() == QStringLiteral("行内公式(&M)")) inlineIdx = i;
        if (acts[i]->text() == QStringLiteral("块级公式(&B)")) blockIdx = i;
    }
    QVERIFY2(inlineIdx >= 0, "插入菜单缺少「行内公式(&M)」");
    QVERIFY2(blockIdx >= 0, "插入菜单缺少「块级公式(&B)」");
    // 块级公式紧邻行内公式之下
    QCOMPARE(blockIdx, inlineIdx + 1);
    // 快捷键（PortableText 输出与平台无关，macOS 下亦为 Ctrl+… 字面）
    QCOMPARE(acts[inlineIdx]->shortcut().toString(QKeySequence::PortableText),
             QStringLiteral("Ctrl+M"));
    QCOMPARE(acts[blockIdx]->shortcut().toString(QKeySequence::PortableText),
             QStringLiteral("Ctrl+Shift+M"));
    // 旧文案「公式(&M)」已不存在（<source> 级精确匹配，避免撞新文案子串）
    QVERIFY2(!actionByText(mb, QStringLiteral("公式(&M)")), "旧「公式(&M)」文案残留");
}

void TestMenuBar::export_shortcuts()
{
    MenuBar mb;
    QAction *docx = actionByText(mb, QStringLiteral("导出 DOCX..."));
    QVERIFY2(docx, "未找到「导出 DOCX...」菜单项");
    QCOMPARE(docx->shortcut().toString(QKeySequence::PortableText),
             QStringLiteral("Ctrl+Shift+D"));

    QAction *html = actionByText(mb, QStringLiteral("导出 HTML..."));
    QVERIFY2(html, "未找到「导出 HTML...」菜单项");
    QCOMPARE(html->shortcut().toString(QKeySequence::PortableText),
             QStringLiteral("Ctrl+Shift+H"));
}

void TestMenuBar::mathActions_emitSignals()
{
    MenuBar mb;
    QAction *inlineAct = actionByText(mb, QStringLiteral("行内公式(&M)"));
    QAction *blockAct = actionByText(mb, QStringLiteral("块级公式(&B)"));
    QVERIFY(inlineAct);
    QVERIFY(blockAct);

    QSignalSpy inlineSpy(&mb, &MenuBar::insertMath);
    QSignalSpy blockSpy(&mb, &MenuBar::insertMathBlock);
    inlineAct->trigger();
    blockAct->trigger();
    QCOMPARE(inlineSpy.count(), 1);
    QCOMPARE(blockSpy.count(), 1);
}

QTEST_MAIN(TestMenuBar)
#include "test_menubar.moc"
```

- [ ] **Step 2: 注册测试目标并构建（预期红：编译失败）**

在 `tests/CMakeLists.txt` 的 `test_collapsiblesection` 目标块之后追加：

```cmake
# 菜单契约回归：插入菜单公式改名（行内/块级入口）+ 导出 DOCX 快捷键
add_executable(test_menubar test_menubar.cpp ../src/ui/MenuBar.cpp)
target_include_directories(test_menubar PRIVATE ../src)
target_link_libraries(test_menubar PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME test_menubar COMMAND test_menubar)
```

Run: `cmake --build build --target test_menubar -j$(nproc)`
Expected: 编译失败 —— `no member named 'insertMathBlock' in 'MenuBar'`（信号尚不存在）

- [ ] **Step 3: MenuBar.h 加信号与成员**

`src/ui/MenuBar.h` 信号区，`void insertMath();` 之后加：

```cpp
    void insertMathBlock();
```

成员区（81-82 行）改为：

```cpp
    QAction *m_linkAct, *m_imageAct, *m_tableAct, *m_mathAct, *m_mathBlockAct,
            *m_codeAct, *m_codeBlockAct, *m_hrAct, *m_emojiAct, *m_footnoteAct;
```

- [ ] **Step 4: 构建并运行测试（预期红：断言失败）**

Run: `cmake --build build --target test_menubar -j$(nproc) && ctest --test-dir build -R test_menubar --output-on-failure`
Expected: 编译通过、测试 FAIL —— 「插入菜单缺少『行内公式(&M)』」等（文案/快捷键尚未实现）

- [ ] **Step 5: MenuBar.cpp 三处实现**

（a）`src/ui/MenuBar.cpp:141-143`，替换为：

```cpp
    m_mathAct = m_insertMenu->addAction(tr("行内公式(&M)"));
    m_mathAct->setShortcut(QKeySequence("Ctrl+M"));
    connect(m_mathAct, &QAction::triggered, this, &MenuBar::insertMath);

    // 块级公式：Ctrl+Shift+M 由本 QAction 携带（原 MainWindow QShortcut 已随之
    // 删除——双注册报 "Ambiguous shortcut"，与 Ctrl+M 同一先例）
    m_mathBlockAct = m_insertMenu->addAction(tr("块级公式(&B)"));
    m_mathBlockAct->setShortcut(QKeySequence("Ctrl+Shift+M"));
    connect(m_mathBlockAct, &QAction::triggered, this, &MenuBar::insertMathBlock);
```

（b）`src/ui/MenuBar.cpp:48-51`，导出 DOCX / HTML 加快捷键（其余不动）：

```cpp
    m_exportDocxAct = m_exportMenu->addAction(tr("导出 DOCX..."));
    m_exportDocxAct->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(m_exportDocxAct, &QAction::triggered, this, &MenuBar::exportDocx);
    m_exportHtmlAct = m_exportMenu->addAction(tr("导出 HTML..."));
    m_exportHtmlAct->setShortcut(QKeySequence("Ctrl+Shift+H"));
    connect(m_exportHtmlAct, &QAction::triggered, this, &MenuBar::exportHtml);
```

（c）`src/ui/MenuBar.cpp` retranslateUi（原 222 行 `m_mathAct->setText(tr("公式(&M)"));`）替换为：

```cpp
    m_mathAct->setText(tr("行内公式(&M)"));
    m_mathBlockAct->setText(tr("块级公式(&B)"));
```

- [ ] **Step 6: 构建并运行测试（预期绿）**

Run: `cmake --build build --target test_menubar -j$(nproc) && ctest --test-dir build -R test_menubar --output-on-failure`
Expected: PASS（3 用例全过）

---

### Task 2: MainWindow 接线（删 QShortcut、连 insertMathBlock）

**Files:**
- Modify: `src/ui/MainWindow.cpp`（setupUi 169-174 行；connectSignals 259-262 行区域）

**Interfaces:**
- Consumes: Task 1 产出的 `MenuBar::insertMathBlock()` 信号
- Produces: 应用的块级公式快捷键主链路（Ctrl+Shift+M → 菜单 QAction → 信号 → `insertMath('block')`）

- [ ] **Step 1: 删除 Ctrl+Shift+M QShortcut（169-174 行整块替换为注释）**

旧代码：

```cpp
    // Ctrl+Shift+M 插入块级公式（Ctrl+M 行内公式由菜单「插入公式」QAction 携带，
    // 此处不再重复注册 QShortcut，否则与菜单快捷键冲突报 "Ambiguous shortcut"）
    QShortcut *mathBlockShortcut = new QShortcut(QKeySequence("Ctrl+Shift+M"), this);
    connect(mathBlockShortcut, &QShortcut::activated, this, [this]() {
        runEditorCommand("window.editorApi.insertMath('block');");
    });
```

新代码：

```cpp
    // Ctrl+M 行内公式 / Ctrl+Shift+M 块级公式均由插入菜单 QAction 携带快捷键
    // （见 MenuBar），此处不再注册 QShortcut，否则与菜单快捷键冲突报
    // "Ambiguous shortcut"。
```

（`#include <QShortcut>` 保留：F3/Shift+F3/Esc/Ctrl+B 四处 QShortcut 仍在使用。）

- [ ] **Step 2: 连接新信号（connectSignals 内现有 insertMath 连接之后加）**

现有：

```cpp
    connect(m_menuBar, &MenuBar::insertMath, this, [this]() {
        runEditorCommand("window.editorApi.insertMath('inline');");
    });
```

其后追加：

```cpp
    connect(m_menuBar, &MenuBar::insertMathBlock, this, [this]() {
        runEditorCommand("window.editorApi.insertMath('block');");
    });
```

- [ ] **Step 3: 构建 + 静态核对**

Run:
```bash
cmake --build build -j$(nproc)
grep -n "Ctrl+Shift+M" src/ui/MainWindow.cpp   # 预期：无输出（移到了 MenuBar）
grep -n "QShortcut" src/ui/MainWindow.cpp       # 预期：仍有 F3/Shift+F3/Esc/Ctrl+B 四处
```
Expected: 构建成功；第一条 grep 无输出；第二条 grep 显示四处仍在

---

### Task 3: 文案一致性（ToolBar 提示 + en_US.ts + 帮助文档）

**Files:**
- Modify: `src/ui/ToolBar.cpp`（234 行、365 行两处同串）
- Modify: `src/resources/i18n/md-editor_en_US.ts`（MenuBar 上下文 419-422 行；ToolBar 上下文 839-842 行）
- Modify: `src/resources/web/markdown-guide.md`（第 12 节快捷键表末尾）

**Interfaces:**
- Consumes: 无（独立文案任务）
- Produces: 无（终态文案与 Task 1 的菜单命名一致）

- [ ] **Step 1: ToolBar 两处提示改「插入行内公式」**

`src/ui/ToolBar.cpp:234`：

```cpp
        tr("插入行内公式 (Ctrl+M)"), ":/icons/light/math.svg");
```

`src/ui/ToolBar.cpp:365`：

```cpp
    findChild<QAction*>("mathAction")->setToolTip(tr("插入行内公式 (Ctrl+M)"));
```

- [ ] **Step 2: en_US.ts 两处消息替换**

MenuBar 上下文（419-422 行）：

```xml
    <message>
        <source>行内公式(&amp;M)</source>
        <translation>Inline &amp;Formula</translation>
    </message>
    <message>
        <source>块级公式(&amp;B)</source>
        <translation>&amp;Block Formula</translation>
    </message>
```

ToolBar 上下文（839-842 行）：

```xml
    <message>
        <source>插入行内公式 (Ctrl+M)</source>
        <translation>Insert Inline Formula (Ctrl+M)</translation>
    </message>
```

- [ ] **Step 3: 帮助文档快捷键表补一行**

`src/resources/web/markdown-guide.md` 第 12 节表格末尾（「加粗 / 斜体」行之后）加导出三行：

```markdown
| 导出 PDF | `Ctrl+P` |
| 导出 DOCX | `Ctrl+Shift+D` |
| 导出 HTML | `Ctrl+Shift+H` |
```

- [ ] **Step 4: 构建 + 校对**

Run:
```bash
cmake --build build -j$(nproc)     # 含 qrc 重打包 + lrelease 编译 .ts
grep -c "<source>公式(&amp;M)</source>" src/resources/i18n/md-editor_en_US.ts   # 预期：0
grep -n "插入公式 (Ctrl+M)" src/ui/ToolBar.cpp                                   # 预期：无输出
```
Expected: 构建成功（lrelease 无告警）；旧串 grep 均无残留

---

### Task 4: 端到端按键取证 + 全量回归 +（门控）提交

**Files:**
- 一次性探针（不提交）：`/tmp/md-editor-claude-20261008/math-shortcut-probe.js`（复制自 `scripts/srcmode-md-probe.js`）
- 探针输入：`/tmp/md-editor-claude-20261008/probe.md`

**Interfaces:**
- Consumes: Task 1/2/3 完成后的 `build/src/md-editor` 二进制；`scripts/srcmode-md-probe.js` 骨架
- Produces: 端到端证据（Ctrl+Shift+M → 块级公式面板、stderr 无 Ambiguous shortcut）

- [ ] **Step 1: 全量回归**

Run: `ctest --test-dir build --output-on-failure`
Expected: 24/24 通过（原 23 项 + 新增 test_menubar）

- [ ] **Step 2: 制备探针（复制骨架 + 三处 delta）**

```bash
mkdir -p /tmp/md-editor-claude-20261008
cp scripts/srcmode-md-probe.js /tmp/md-editor-claude-20261008/math-shortcut-probe.js
printf '# 探针文档\n\n块级公式快捷键端到端取证。\n' > /tmp/md-editor-claude-20261008/probe.md
```

Delta ①：`XKEY_PY`（骨架 63-69 行）按键序列改 Ctrl+Shift+M：

```js
    'kc_ctrl = d.keysym_to_keycode(XK.XK_Control_L)',
    'kc_shift = d.keysym_to_keycode(XK.XK_Shift_L)',
    'kc_m = d.keysym_to_keycode(XK.XK_m)',
    'if kc_m == 0: print("Xvfb 键位表无 m 键码"); sys.exit(4)',
    'xtest.fake_input(d, X.KeyPress, kc_ctrl)',
    'xtest.fake_input(d, X.KeyPress, kc_shift)',
    'xtest.fake_input(d, X.KeyPress, kc_m)',
    'xtest.fake_input(d, X.KeyRelease, kc_m)',
    'xtest.fake_input(d, X.KeyRelease, kc_shift)',
    'xtest.fake_input(d, X.KeyRelease, kc_ctrl)',
```

Delta ②：主流程「3. 合成 Ctrl+\ 切源码 / 4. 取证」段（骨架 243-262 行）替换为：

```js
        // 3. 合成 Ctrl+Shift+M（真实快捷键路径：菜单 QAction → MainWindow
        //    → insertMath('block') → JS 面板），等块级公式面板弹出
        await sendKey(displayNum, authFile);
        await pollGuard(
            () => cdp.evaluate('!!document.querySelector(".math-editor-popover")'),
            ENTER_TIMEOUT_MS, '块级公式面板弹出');
        await sleep(SETTLE_MS);

        // 4. 取证：面板类型须为 block（行内/块级接线错误在此暴露）
        const check = await cdp.evaluate(
            '(function(){ var me = window.editorApi._mathEditor;' +
            ' return { mathType: me ? me.mathType : null,' +
            '         popover: !!document.querySelector(".math-editor-popover"),' +
            '         overlay: !!document.querySelector(".math-editor-overlay") }; })()');
        if (!check || check.mathType !== 'block')
            fail('mathType 期望 block，实际 ' + JSON.stringify(check));
        md = JSON.stringify(check);

        // 5. stderr 无 "Ambiguous shortcut"（QShortcut+QAction 双注册的特征告警）
        const errText = fs.readFileSync(errLogPath, 'utf8');
        if (errText.includes('Ambiguous shortcut'))
            fail('stderr 出现 Ambiguous shortcut（快捷键双注册残留）: ' + errLogPath);
```

（`md` 变量沿用骨架尾部 `writeTextAtomic(opts.out, md)` 落盘逻辑。）

Delta ③：无需其他改动（生命周期/CDP/隔离 HOME/清理均复用骨架）。

- [ ] **Step 3: 运行探针**

Run:
```bash
node /tmp/md-editor-claude-20261008/math-shortcut-probe.js \
    --file /tmp/md-editor-claude-20261008/probe.md \
    --out /tmp/md-editor-claude-20261008/math-shortcut.json \
    --workdir /tmp/md-editor-claude-20261008/wd \
    --binary build/src/md-editor
cat /tmp/md-editor-claude-20261008/math-shortcut.json
```
Expected: 探针正常退出，JSON 为 `{"mathType":"block","popover":true,"overlay":true}`；无 fail 抛出

- [ ] **Step 4: 用户目视验证（提交门，必须用户确认）**

请用户确认：
1. 插入菜单显示「行内公式 Ctrl+M」「块级公式 Ctrl+Shift+M」，块级紧邻行内之下；
2. 文件→导出菜单「导出 DOCX...」显示 Ctrl+Shift+D、「导出 HTML...」显示 Ctrl+Shift+H，按下均弹出保存框；
3. 键盘按下 Ctrl+Shift+M 弹出「块级」公式面板（且不再有 Ctrl+M 的 Ambiguous 冲突行为）；
4. 切换 English 后菜单显示 Inline Formula / Block Formula / 工具栏提示 Insert Inline Formula。

- [ ] **Step 5: 提交（仅在第 4 步用户确认后执行；未经确认不提交）**

```bash
git add tests/test_menubar.cpp tests/CMakeLists.txt \
        src/ui/MenuBar.h src/ui/MenuBar.cpp src/ui/MainWindow.cpp src/ui/ToolBar.cpp \
        src/resources/i18n/md-editor_en_US.ts src/resources/web/markdown-guide.md \
        docs/superpowers/specs/2026-10-08-insert-menu-math-docx-shortcut-design.md \
        docs/superpowers/plans/2026-10-08-insert-menu-math-docx-shortcut.md
git commit -m "feat: 插入菜单拆分公式入口（行内/块级），导出 DOCX 加速快捷键

- 「公式」更名「行内公式」(Ctrl+M)，其下新增「块级公式」(Ctrl+Shift+M) 菜单项；
  原 MainWindow 的 Ctrl+Shift+M QShortcut 迁至菜单 QAction（双注册报
  Ambiguous shortcut，与 Ctrl+M 同一先例）
- 导出 DOCX 增加快捷键 Ctrl+Shift+D、导出 HTML 增加 Ctrl+Shift+H
- 工具栏 ∑ 提示改为「插入行内公式 (Ctrl+M)」；en_US.ts 同步新文案；
  帮助文档快捷键表补导出三行（PDF/DOCX/HTML）
- 新增 tests/test_menubar.cpp 菜单契约回归（文案/快捷键/邻接/信号）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```
