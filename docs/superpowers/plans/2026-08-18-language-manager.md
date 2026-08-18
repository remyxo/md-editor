# LanguageManager 中英文运行时切换 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 LanguageManager（QTranslator 运行时切换中英文），全组件 `retranslateUi()` + editor.js 字典 + `setLanguage`，接通设置对话框已有的 English 下拉项。

**Architecture:** 源语言 = 中文（现有硬编码文案即源字符串），目标语言 = 英文。`LanguageManager`（挂 `Application`，仿 `ThemeManager`）统一装载/卸载 `QTranslator`、持久化 `appearance/language`、发 `languageChanged`。C++ 侧所有用户可见字符串包 `tr()` 并抽出 `retranslateUi()`；Web 编辑器用 `I18N` 字典 + `data-i18n` 属性 + `setLanguage` 重刷。翻译资源走标准 Qt：`lupdate` 生成 `.ts`，构建时 `qt6_add_translations` 编译 `.qm` 打进 qrc。

**Tech Stack:** Qt 6.4.2（兼容基准）、Qt6::LinguistTools（qt6-tools-dev）、QTranslator、QSettings、原生 JS（TipTap NodeView / 弹窗）。

## Global Constraints

- 所有文档和注释使用中文，代码标识符使用英文。
- 版本号全局唯一：`CMakeLists.txt` 的 `project(md-editor VERSION x.y.z)`；本次实现完成后与用户确认版本号再更新（见 Task 11）。
- 兼容基准 Qt 6.4.2（CI ubuntu-24.04）；`qt6_add_translations` 自 Qt 6.2 引入，6.4.2 可用。
- 源语言 = 中文；英文翻译写在 `src/resources/i18n/md-editor_en_US.ts`，翻译资源 `RESOURCE_PREFIX "/i18n"` → 运行时路径 `:/i18n/md-editor_en_US.qm`。
- 语言持久化键：`appearance/language`，取值 `zh_CN` / `en_US`，缺省 `zh_CN`。
- QSettings 组织/应用名固定为 `("md-editor", "md-editor")`（与现有代码一致）。
- emoji 分类名保持中文（不翻译）；emoji 搜索关键词 `kw` 数据不动。
- Qt 内建标准按钮（QMessageBox Yes/No、QFileDialog 内部）本版不翻译。
- 改 qrc/源码后必须 `cmake --build build` 重新编译。

---

### Task 1: LanguageManager 组件 + 单测 + Application 接入

**Files:**
- Create: `src/app/LanguageManager.h`
- Create: `src/app/LanguageManager.cpp`
- Modify: `src/app/Application.h`（加成员 + getter）
- Modify: `src/app/Application.cpp`（构造 LanguageManager）
- Modify: `src/CMakeLists.txt`（SOURCES/HEADERS 加两行）
- Test: `tests/test_languagemanager.cpp`
- Modify: `tests/CMakeLists.txt`（加 test_languagemanager）

**Interfaces:**
- Consumes: 无（首个任务）。
- Produces: `LanguageManager` 类 —— `currentLanguage()`（返回 `QString`）、`availableLanguages()`（返回 `QStringList`）、`switchLanguage(const QString&)`（slot）、信号 `languageChanged(const QString&)`。`Application::languageManager()` 返回 `LanguageManager*`。后续任务依赖这些签名。

- [ ] **Step 1: 写 LanguageManager 头文件**

创建 `src/app/LanguageManager.h`：

```cpp
#ifndef MD_EDITOR_LANGUAGEMANAGER_H
#define MD_EDITOR_LANGUAGEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>

// 界面语言管理器：装载/卸载 QTranslator，持久化 appearance/language，
// 发 languageChanged 信号。源语言=中文（源字符串直通），英文装载 en_US.qm。
class LanguageManager : public QObject {
    Q_OBJECT
public:
    explicit LanguageManager(QObject *parent = nullptr);

    QString currentLanguage() const;        // "zh_CN" | "en_US"
    QStringList availableLanguages() const; // {"zh_CN", "en_US"}

public slots:
    void switchLanguage(const QString &lang);

signals:
    void languageChanged(const QString &lang);

private:
    QString m_currentLanguage;
    QTranslator m_translator;  // 成员持有：installTranslator 不接管所有权
};

#endif // MD_EDITOR_LANGUAGEMANAGER_H
```

- [ ] **Step 2: 写 LanguageManager 实现**

创建 `src/app/LanguageManager.cpp`：

```cpp
#include "LanguageManager.h"
#include <QApplication>
#include <QSettings>
#include <QDebug>

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
    , m_currentLanguage(QStringLiteral("zh_CN"))
{
    // 启动恢复：读偏好并装载（先于 MainWindow 构造 → 启动即正确语言）
    QSettings settings("md-editor", "md-editor");
    const QString saved = settings.value("appearance/language", "zh_CN").toString();
    switchLanguage(saved);
}

QString LanguageManager::currentLanguage() const
{
    return m_currentLanguage;
}

QStringList LanguageManager::availableLanguages() const
{
    return {QStringLiteral("zh_CN"), QStringLiteral("en_US")};
}

void LanguageManager::switchLanguage(const QString &lang)
{
    if (lang == m_currentLanguage)
        return;

    // 卸载旧翻译（zh_CN 无翻译 = 源字符串直通）
    qApp->removeTranslator(&m_translator);

    if (lang == QStringLiteral("en_US")) {
        if (m_translator.load(QStringLiteral(":/i18n/md-editor_en_US.qm"))) {
            qApp->installTranslator(&m_translator);
        } else {
            // 资源缺失：降级（界面保持中文源字符串），但偏好仍记录
            qWarning() << "[LanguageManager] 加载 md-editor_en_US.qm 失败，界面保持中文";
        }
    }

    m_currentLanguage = lang;
    QSettings settings("md-editor", "md-editor");
    settings.setValue("appearance/language", lang);
    settings.sync();

    emit languageChanged(lang);
}
```

- [ ] **Step 3: Application 接入**

`src/app/Application.h`：加 `#include "LanguageManager.h"`；私有成员 `LanguageManager *m_languageManager;`；公开 `LanguageManager *languageManager() const;`。

`src/app/Application.cpp`：构造函数里 `m_themeManager = new ThemeManager(this);` 之后加一行 `m_languageManager = new LanguageManager(this);`，并实现 getter：

```cpp
LanguageManager *Application::languageManager() const
{
    return m_languageManager;
}
```

- [ ] **Step 4: CMake 注册源文件**

`src/CMakeLists.txt`：`SOURCES` 列表加 `app/LanguageManager.cpp`，`HEADERS` 列表加 `app/LanguageManager.h`（放在 `app/ThemeManager.*` 附近）。

- [ ] **Step 5: 写单测**

创建 `tests/test_languagemanager.cpp`：

```cpp
#include <QtTest>
#include <QSettings>
#include <QDir>
#include "app/LanguageManager.h"

// LanguageManager 单测：状态/持久化/信号/幂等/未知语言忽略。
// 通过 QSettings 重定向到临时目录，避免污染真实用户配置。
class TestLanguageManager : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    }

    void init() {
        QSettings s("md-editor", "md-editor");
        s.remove("appearance/language");
        s.sync();
    }

    void testAvailableLanguages() {
        LanguageManager m;
        QCOMPARE(m.availableLanguages(), QStringList({"zh_CN", "en_US"}));
    }

    void testDefaultIsChinese() {
        LanguageManager m;  // 无已存偏好 → zh_CN
        QCOMPARE(m.currentLanguage(), QString("zh_CN"));
    }

    void testSwitchAndPersist() {
        LanguageManager m;
        QSignalSpy spy(&m, &LanguageManager::languageChanged);
        m.switchLanguage("en_US");
        QCOMPARE(m.currentLanguage(), QString("en_US"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("en_US"));
        QCOMPARE(QSettings("md-editor", "md-editor").value("appearance/language").toString(),
                 QString("en_US"));
    }

    void testIdempotent() {
        LanguageManager m;
        QSignalSpy spy(&m, &LanguageManager::languageChanged);
        m.switchLanguage("en_US");
        m.switchLanguage("en_US");  // 同语言 → 直接返回
        QCOMPARE(spy.count(), 1);
    }

    void testUnknownLanguageIgnored() {
        LanguageManager m;
        QSignalSpy spy(&m, &LanguageManager::languageChanged);
        m.switchLanguage("fr_FR");
        QCOMPARE(m.currentLanguage(), QString("zh_CN"));  // 保持当前语言
        QCOMPARE(spy.count(), 0);
    }

    void testRestoreFromSettings() {
        QSettings("md-editor", "md-editor").setValue("appearance/language", "en_US");
        LanguageManager m;  // 构造时读已存偏好
        QCOMPARE(m.currentLanguage(), QString("en_US"));
    }
};

QTEST_MAIN(TestLanguageManager)
#include "test_languagemanager.moc"
```

- [ ] **Step 6: tests/CMakeLists.txt 注册**

在 `tests/CMakeLists.txt` 末尾追加：

```cmake
add_executable(test_languagemanager test_languagemanager.cpp
    ../src/app/LanguageManager.cpp)
target_include_directories(test_languagemanager PRIVATE ../src)
target_link_libraries(test_languagemanager PRIVATE Qt6::Test)
add_test(NAME test_languagemanager COMMAND test_languagemanager)
```

- [ ] **Step 7: 构建并跑单测**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -R test_languagemanager --output-on-failure
```
预期：全部 PASS。

- [ ] **Step 8: Commit**

```bash
git add src/app/LanguageManager.h src/app/LanguageManager.cpp \
        src/app/Application.h src/app/Application.cpp \
        src/CMakeLists.txt tests/test_languagemanager.cpp tests/CMakeLists.txt
git commit -m "feat: LanguageManager 组件与单测"
```

---

### Task 2: 翻译工具链打通（走通骨架，以 AboutDialog 验证）

本任务安装 qt6-tools-dev、接入 LinguistTools + `qt6_add_translations`、生成初始 `.ts`、给 `AboutDialog` 包 `tr()` 并验证「启动即英文」整条链路。

**Files:**
- Modify: `CMakeLists.txt`（find_package 加 LinguistTools）
- Modify: `src/CMakeLists.txt`（qt6_add_translations）
- Create: `src/resources/i18n/md-editor_en_US.ts`（本任务仅含 AboutDialog 上下文，Task 11 用 lupdate 重生成完整版）
- Modify: `src/ui/AboutDialog.h`（加 retranslateUi 声明）
- Modify: `src/ui/AboutDialog.cpp`（tr() + retranslateUi）

**Interfaces:**
- Consumes: 无（AboutDialog 改动不依赖 Task 1，但构建需 Task 1 已注册 LanguageManager 以免 CMake 缺源文件——按顺序执行）。
- Produces: 翻译资源路径 `:/i18n/md-editor_en_US.qm`；`AboutDialog::retranslateUi()`。后续任务复用同一工具链模式。

- [ ] **Step 1: 安装 qt6-tools-dev**

```bash
sudo apt install qt6-tools-dev
lrelease -version   # 确认可用
```
若本机无法 sudo，改由用户在终端执行 `! sudo apt install qt6-tools-dev`。CI 的 GitHub Actions 需在 ubuntu-24.04 构建步骤补 `qt6-tools-dev`（见 Step 2 备注）。

- [ ] **Step 2: 顶层 CMakeLists 加 LinguistTools**

`CMakeLists.txt` 第 10 行改为：

```cmake
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets WebEngineWidgets WebChannel DBus LinguistTools)
```

同时确认 `.github/workflows/*.yml`（若存在）中 ubuntu-24.04 的 apt 安装清单补 `qt6-tools-dev`。

- [ ] **Step 3: src/CMakeLists 接入翻译编译**

`src/CMakeLists.txt` 在 `add_executable(...)` 之后、`target_link_libraries(...)` 附近追加：

```cmake
qt6_add_translations(${PROJECT_NAME}
    TS_FILES resources/i18n/md-editor_en_US.ts
    RESOURCE_PREFIX "/i18n"
)
```

- [ ] **Step 4: AboutDialog 包 tr() 并抽出 retranslateUi**

`src/ui/AboutDialog.h` 加 `private: void retranslateUi();`（类已有 Q_OBJECT）。

`src/ui/AboutDialog.cpp` 改造：`setupUi` 拆为「结构 + retranslateUi」，构造函数末尾调 `retranslateUi()`。完整新版：

```cpp
#include "AboutDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setFixedSize(400, 210);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 16, 20, 16);
    mainLayout->setSpacing(14);

    QLabel *iconLabel = new QLabel();
    iconLabel->setPixmap(QPixmap(":/icons/md-editor.svg").scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setFixedSize(64, 64);
    mainLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(4);

    m_nameLabel = new QLabel("Markdown Editor");
    QFont nameFont = m_nameLabel->font();
    nameFont.setPointSize(16);
    nameFont.setBold(true);
    m_nameLabel->setFont(nameFont);
    rightLayout->addWidget(m_nameLabel);

    m_versionLabel = new QLabel();
    m_versionLabel->setStyleSheet("color: #666; font-size: 13px;");
    rightLayout->addWidget(m_versionLabel);

    rightLayout->addSpacing(20);

    m_descLabel = new QLabel();
    m_descLabel->setWordWrap(true);
    m_descLabel->setStyleSheet("color: #888; font-size: 13px;");
    rightLayout->addWidget(m_descLabel);

    m_copyLabel = new QLabel("© 2026");
    m_copyLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    rightLayout->addWidget(m_copyLabel);

    rightLayout->addStretch();

    m_closeBtn = new QPushButton();
    m_closeBtn->setFixedWidth(80);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    rightLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(rightLayout);

    retranslateUi();
}

void AboutDialog::retranslateUi()
{
    setWindowTitle(tr("关于 Markdown Editor"));
    // 应用名 Markdown Editor 为品牌名，不翻译
    m_versionLabel->setText(tr("版本 %1").arg(MD_EDITOR_VERSION));
    m_descLabel->setText(tr("一款基于 Qt6 + TipTap 的 Markdown 所见即所得编辑器"));
    m_closeBtn->setText(tr("确定"));
}
```

对应 `src/ui/AboutDialog.h` 加成员：`QLabel *m_nameLabel; QLabel *m_versionLabel; QLabel *m_descLabel; QLabel *m_copyLabel; QPushButton *m_closeBtn;`（头文件需 `class QLabel; class QPushButton;` 前向声明或 include）。

- [ ] **Step 5: 生成初始 .ts（仅 AboutDialog 上下文）**

创建 `src/resources/i18n/md-editor_en_US.ts`（手写最小版，验证工具链；Task 11 会 `lupdate` 重生成完整版并补全）：

```xml
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en_US">
<context>
    <name>AboutDialog</name>
    <message>
        <source>关于 Markdown Editor</source>
        <translation>About Markdown Editor</translation>
    </message>
    <message>
        <source>版本 %1</source>
        <translation>Version %1</translation>
    </message>
    <message>
        <source>一款基于 Qt6 + TipTap 的 Markdown 所见即所得编辑器</source>
        <translation>A WYSIWYG Markdown editor based on Qt6 + TipTap</translation>
    </message>
    <message>
        <source>确定</source>
        <translation>OK</translation>
    </message>
</context>
</TS>
```

- [ ] **Step 6: 构建验证**

```bash
cmake --build build -j$(nproc)
```
预期：编译通过，构建日志出现 lrelease 处理 `md-editor_en_US.ts`。

- [ ] **Step 7: 手动验证「启动即英文」**

临时把语言设为英文再启动，验证 AboutDialog：

```bash
# 写入偏好后启动，再通过菜单「帮助→关于」查看
printf '[appearance]\nlanguage=en_US\n' > ~/.config/md-editor/md-editor.conf 2>/dev/null || true
./build/src/md-editor
```
（更稳妥：先启动一次让 QSettings 建立，或直接编辑配置；用 GUI 打开「关于」确认窗口标题/描述为英文。）验证后删掉该偏好，避免影响后续测试：`sed -i '/language=en_US/d' ~/.config/md-editor/md-editor.conf`（或 QSettings 默认 native 路径）。

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt src/resources/i18n/md-editor_en_US.ts \
        src/ui/AboutDialog.h src/ui/AboutDialog.cpp
git commit -m "feat: 接入 LinguistTools 翻译工具链并以 AboutDialog 打通骨架"
```

---

### Task 3: SettingsDialog 包 tr() + retranslateUi + languageChanged 信号

**Files:**
- Modify: `src/ui/SettingsDialog.h`
- Modify: `src/ui/SettingsDialog.cpp`

**Interfaces:**
- Consumes: 无新依赖。
- Produces: 信号 `SettingsDialog::languageChanged(const QString &lang)`；`SettingsDialog::retranslateUi()`（public，MainWindow 切换后调用）。Task 10 消费这些。

- [ ] **Step 1: 头文件加信号与 retranslateUi 声明**

`src/ui/SettingsDialog.h`：在 signals 区加 `void languageChanged(const QString &lang);`；private 加 `void retranslateUi();`（并改为 public 以便 MainWindow 调用，或提供 public 包装——本计划设为 public，与 setupUi 区分：把 `void retranslateUi();` 放到 public 段）。

- [ ] **Step 2: setupUi 拆出字符串、所有文案包 tr()**

`SettingsDialog::setupUi()` 中所有中文文案（窗口标题、`QGroupBox("启动")`、`QCheckBox("启动时恢复上次会话")`、`addTab(... "通用"/"编辑器"/"外观"/"导出")`、`editorForm->addRow("字体:"...)` 等）一律改为 `tr("...")`。为便于 retranslateUi 重设，需把以下控件存为成员（头文件补充）：

- `QTabWidget *m_tabs;`
- `QGroupBox *m_startupGroup;`、`QCheckBox *m_restoreSessionCheck;`
- `QLabel *m_fontLabel, *m_fontSizeLabel, *m_lineNumbersLabel, *m_wordWrapLabel, *m_tabSizeLabel, *m_widthLabel;`
- `QGroupBox *m_marginGroup; QLabel *m_marginTopLbl, *m_marginBottomLbl, *m_marginLeftLbl, *m_marginRightLbl; QLabel *m_cmTopLbl, *m_cmBottomLbl, *m_cmLeftLbl, *m_cmRightLbl;`
- `QGroupBox *m_orientGroup; QLabel *m_portraitLbl, *m_landscapeLbl;`
- 表单标签用 `QFormLayout::labelForField()` 亦可，但直接存成员更清晰。`QFormLayout *m_editorForm, *m_appearanceForm, *m_paperForm;`

为控制篇幅，此处采用「存 QFormLayout + QLabel 标签成员」的最简做法：**用 `addRow` 时把标签 `QLabel` 存成员**（如 `m_fontLabel = new QLabel(tr("字体:"), editorTab); editorForm->addRow(m_fontLabel, m_fontCombo);`）。

QDialogButtonBox 改为显式按钮（本任务只改结构，不接语言切换）：保留 `QDialogButtonBox` 但用 `button(QDialogButtonBox::Ok)->setText(tr("确定"))` 与 `button(QDialogButtonBox::Cancel)->setText(tr("取消"))` 在 retranslateUi 里设置。若用 `QDialogButtonBox`，需存 `QDialogButtonBox *m_buttons;`。

- [ ] **Step 3: 实现 retranslateUi()**

在 `src/ui/SettingsDialog.cpp` 新增（字符串清单见下）：

```cpp
void SettingsDialog::retranslateUi()
{
    setWindowTitle(tr("设置"));

    m_startupGroup->setTitle(tr("启动"));
    m_restoreSessionCheck->setText(tr("启动时恢复上次会话"));

    m_fontLabel->setText(tr("字体:"));
    m_fontSizeLabel->setText(tr("字号:"));
    m_lineNumbersLabel->setText(tr("显示行号:"));
    m_wordWrapLabel->setText(tr("自动换行:"));
    m_tabSizeLabel->setText(tr("Tab 宽度:"));
    m_widthLabel->setText(tr("编辑区宽度:"));

    m_themeLabel->setText(tr("主题:"));
    m_langLabel->setText(tr("界面语言:"));
    m_customCssLabel->setText(tr("自定义 CSS:"));

    m_paperLabel->setText(tr("纸张:"));
    m_marginGroup->setTitle(tr("页边距"));
    m_marginTopLbl->setText(tr("上:"));
    m_marginBottomLbl->setText(tr("下:"));
    m_marginLeftLbl->setText(tr("左:"));
    m_marginRightLbl->setText(tr("右:"));
    m_cmTopLbl->setText(tr("厘米")); m_cmBottomLbl->setText(tr("厘米"));
    m_cmLeftLbl->setText(tr("厘米")); m_cmRightLbl->setText(tr("厘米"));
    m_orientGroup->setTitle(tr("方向"));
    m_portraitLbl->setText(tr("纵向(P)"));
    m_landscapeLbl->setText(tr("横向(L)"));

    m_tabs->setTabText(0, tr("通用"));
    m_tabs->setTabText(1, tr("编辑器"));
    m_tabs->setTabText(2, tr("外观"));
    m_tabs->setTabText(3, tr("导出"));

    // 主题下拉三项 + 语言下拉两项（顺序不变，仅改显示文本；data 保持 system/light/dark、zh_CN/en_US）
    m_themeCombo->setItemText(0, tr("跟随系统"));
    m_themeCombo->setItemText(1, tr("亮色"));
    m_themeCombo->setItemText(2, tr("暗色"));
    m_langCombo->setItemText(0, tr("简体中文"));
    m_langCombo->setItemText(1, tr("English"));

    if (QDialogButtonBox *bb = findChild<QDialogButtonBox *>()) {
        if (QAbstractButton *ok = bb->button(QDialogButtonBox::Ok)) ok->setText(tr("确定"));
        if (QAbstractButton *cb = bb->button(QDialogButtonBox::Cancel)) cb->setText(tr("取消"));
    }
}
```

注意：`setupUi()` 里所有 `addItem("跟随系统", "system")` 等保持 data 不变、文本由 retranslateUi 设；`setPlaceholderText("/* 可选… */")` 也改 `tr()`。

- [ ] **Step 4: onAccepted 发射语言信号**

`src/ui/SettingsDialog.cpp` 的 `onAccepted()` 末尾（`accept()` 前）加：

```cpp
    // 语言走 MainWindow 统一链路（切换 + 持久化），不再直写 QSettings
    emit languageChanged(m_langCombo->currentData().toString());
```

（语言持久化由 `LanguageManager::switchLanguage` 完成，`SettingsDialog` 不写 QSettings。）

- [ ] **Step 5: 补齐 .ts（SettingsDialog 上下文）**

在 `src/resources/i18n/md-editor_en_US.ts` 的 `</TS>` 前追加 `<context><name>SettingsDialog</name>` 块，翻译表：

| source（中文） | translation（英文） |
|---|---|
| 设置 | Settings |
| 启动 | Startup |
| 启动时恢复上次会话 | Restore last session on startup |
| 字体: | Font: |
| 字号: | Font Size: |
| 显示行号: | Line Numbers: |
| 自动换行: | Word Wrap: |
| Tab 宽度: | Tab Width: |
| 编辑区宽度: | Editor Width: |
| 主题: | Theme: |
| 界面语言: | Interface Language: |
| 自定义 CSS: | Custom CSS: |
| 纸张: | Paper: |
| 页边距 | Margins |
| 上: | Top: |
| 下: | Bottom: |
| 左: | Left: |
| 右: | Right: |
| 厘米 | cm |
| 方向 | Orientation |
| 纵向(P) | Portrait (P) |
| 横向(L) | Landscape (L) |
| 通用 | General |
| 编辑器 | Editor |
| 外观 | Appearance |
| 导出 | Export |
| 跟随系统 | Follow System |
| 亮色 | Light |
| 暗色 | Dark |
| 简体中文 | Simplified Chinese |
| English | English |
| 确定 | OK |
| 取消 | Cancel |

- [ ] **Step 6: 构建 + 手动验证设置页英文**

```bash
cmake --build build -j$(nproc)
```
启动应用，`appearance/language=en_US` 时打开设置页，确认各标签/分组/标签英文。

- [ ] **Step 7: Commit**

```bash
git add src/ui/SettingsDialog.h src/ui/SettingsDialog.cpp src/resources/i18n/md-editor_en_US.ts
git commit -m "feat: SettingsDialog 接入 tr/retranslateUi 并新增 languageChanged 信号"
```

---

### Task 4: MenuBar 包 tr() + retranslateUi

**Files:**
- Modify: `src/ui/MenuBar.h`
- Modify: `src/ui/MenuBar.cpp`

**Interfaces:**
- Consumes: 无。
- Produces: `MenuBar::retranslateUi()`。Task 10 调用。

- [ ] **Step 1: 头文件加动作成员 + retranslateUi 声明**

`src/ui/MenuBar.h` 加 `void retranslateUi();`（public），并加动作成员（替换 setupUi 里的局部变量）：

```cpp
    QAction *m_newAct, *m_openAct, *m_openFolderAct, *m_saveAct, *m_saveAsAct;
    QMenu *m_exportMenu;
    QAction *m_exportPdfAct, *m_exportDocxAct, *m_exportHtmlAct, *m_quitAct;
    QAction *m_undoAct, *m_redoAct, *m_findAct, *m_replaceAct;
    QAction *m_sidebarAct, *m_outlineAct, *m_sourceAct, *m_fullscreenAct;
    QMenu *m_themeMenu;
    QAction *m_linkAct, *m_imageAct, *m_tableAct, *m_mathAct, *m_codeAct,
            *m_codeBlockAct, *m_hrAct, *m_emojiAct, *m_footnoteAct;
    QAction *m_guideAct, *m_settingsAct, *m_aboutAct;
```

- [ ] **Step 2: setupUi 用成员替换局部 + 文案包 tr()**

`MenuBar::setupUi()` 里每个 `QAction *xxxAct = menu->addAction("中文(...)")` 改为 `m_xxxAct = menu->addAction(tr("中文(...)"))`（`m_exportMenu = m_fileMenu->addMenu(tr("导出"))` 等）。`m_themeActs` 已存在，其文本也改 `tr("跟随系统")`/`tr("亮色")`/`tr("暗色")`。

`updateRecentFilesMenu()` 里 `addAction("(无)")`、`addAction("清除最近文件列表")` 也改 `tr()`（动态重建，切语言后由 retranslateUi 调一次 `updateRecentFilesMenu()` 刷新）。

`setupUi()` 末尾改调 `retranslateUi();`（替换掉原来散落在 addAction/addMenu 里的字符串——因已改用成员+tr()，retranslateUi 负责首次赋值即可）。

- [ ] **Step 3: 实现 retranslateUi()**

```cpp
void MenuBar::retranslateUi()
{
    m_fileMenu->setTitle(tr("文件(&F)"));
    m_newAct->setText(tr("新建(&N)"));
    m_openAct->setText(tr("打开文件(&O)..."));
    m_openFolderAct->setText(tr("打开文件夹..."));
    m_recentFilesMenu->setTitle(tr("最近文件"));
    m_saveAct->setText(tr("保存(&S)"));
    m_saveAsAct->setText(tr("另存为..."));
    m_exportMenu->setTitle(tr("导出"));
    m_exportPdfAct->setText(tr("导出 PDF..."));
    m_exportDocxAct->setText(tr("导出 DOCX..."));
    m_exportHtmlAct->setText(tr("导出 HTML..."));
    m_quitAct->setText(tr("退出(&Q)"));

    m_editMenu->setTitle(tr("编辑(&E)"));
    m_undoAct->setText(tr("撤销(&U)"));
    m_redoAct->setText(tr("重做(&R)"));
    m_findAct->setText(tr("搜索(&F)"));
    m_replaceAct->setText(tr("替换(&H)"));

    m_viewMenu->setTitle(tr("视图(&V)"));
    m_sidebarAct->setText(tr("侧边栏"));
    m_outlineAct->setText(tr("大纲"));
    m_sourceAct->setText(tr("源码模式"));
    m_fullscreenAct->setText(tr("全屏"));
    m_themeMenu->setTitle(tr("主题"));
    // m_themeActs 顺序：0=system 1=light 2=dark
    m_themeActs[0]->setText(tr("跟随系统"));
    m_themeActs[1]->setText(tr("亮色"));
    m_themeActs[2]->setText(tr("暗色"));

    m_insertMenu->setTitle(tr("插入(&I)"));
    m_linkAct->setText(tr("链接..."));
    m_imageAct->setText(tr("图片..."));
    m_tableAct->setText(tr("表格..."));
    m_mathAct->setText(tr("插入公式(&M)"));
    m_codeAct->setText(tr("行内代码"));
    m_codeBlockAct->setText(tr("代码块"));
    m_hrAct->setText(tr("分割线"));
    m_emojiAct->setText(tr("表情符号..."));
    m_footnoteAct->setText(tr("脚注..."));

    m_helpMenu->setTitle(tr("帮助(&H)"));
    m_guideAct->setText(tr("Markdown 语法参考(&G)"));
    m_settingsAct->setText(tr("设置..."));
    m_aboutAct->setText(tr("关于..."));

    updateRecentFilesMenu();  // 「(无)」「清除最近文件列表」随语言刷新
}
```

- [ ] **Step 4: 补齐 .ts（MenuBar 上下文）**

`md-editor_en_US.ts` 加 `<context><name>MenuBar</name>`，翻译表：

| 中文 | 英文 |
|---|---|
| 文件(&F) | &File |
| 新建(&N) | &New |
| 打开文件(&O)... | &Open File... |
| 打开文件夹... | Open Folder... |
| 最近文件 | Recent Files |
| 保存(&S) | &Save |
| 另存为... | Save As... |
| 导出 | Export |
| 导出 PDF... | Export PDF... |
| 导出 DOCX... | Export DOCX... |
| 导出 HTML... | Export HTML... |
| 退出(&Q) | &Quit |
| 编辑(&E) | &Edit |
| 撤销(&U) | &Undo |
| 重做(&R) | &Redo |
| 搜索(&F) | &Find |
| 替换(&H) | &Replace |
| 视图(&V) | &View |
| 侧边栏 | Sidebar |
| 大纲 | Outline |
| 源码模式 | Source Mode |
| 全屏 | Fullscreen |
| 主题 | Theme |
| 跟随系统 | Follow System |
| 亮色 | Light |
| 暗色 | Dark |
| 插入(&I) | &Insert |
| 链接... | Link... |
| 图片... | Image... |
| 表格... | Table... |
| 插入公式(&M) | Insert &Formula |
| 行内代码 | Inline Code |
| 代码块 | Code Block |
| 分割线 | Horizontal Rule |
| 表情符号... | Emoji... |
| 脚注... | Footnote... |
| 帮助(&H) | &Help |
| Markdown 语法参考(&G) | &Markdown Guide |
| 设置... | Settings... |
| 关于... | About... |
| (无) | (None) |
| 清除最近文件列表 | Clear Recent Files |

- [ ] **Step 5: 构建 + Commit**

```bash
cmake --build build -j$(nproc)
git add src/ui/MenuBar.h src/ui/MenuBar.cpp src/resources/i18n/md-editor_en_US.ts
git commit -m "feat: MenuBar 接入 tr/retranslateUi"
```

---

### Task 5: ToolBar 包 tr() + retranslateUi

**Files:**
- Modify: `src/ui/ToolBar.h`
- Modify: `src/ui/ToolBar.cpp`

**Interfaces:**
- Consumes: 无。
- Produces: `ToolBar::retranslateUi()`。Task 10 调用。

- [ ] **Step 1: 头文件加成员 + retranslateUi 声明**

`src/ui/ToolBar.h` 加 `void retranslateUi();`（public）。对齐/上下标/导出/颜色下拉的菜单项需要重设文本，存成员：

```cpp
    QLabel *m_headingLabel;
    QMenu *m_exportMenu;
    QAction *m_alignLeftAct, *m_alignCenterAct, *m_alignRightAct;
    QAction *m_superscriptAct, *m_subscriptAct;
```

（`m_boldAction` 等已有成员；标题下拉 `m_headingCombo` 已有。）

- [ ] **Step 2: setupUi 拆字符串 + 文案包 tr()**

`makeAction` 的 `tip` 参数、`QLabel(" 标题 ")`、`m_headingCombo->addItem("正文",0)`、对齐/上下标 `addAlignItem/addSubsupItem` 的 `text`、导出菜单 `exportMenu->addAction("导出 PDF...")`、`buildColorMenu` 里的「无文字颜色/无背景颜色/更多颜色…/选择文字颜色/选择背景颜色」全部改 `tr()`。菜单项存成员（Step 1 已声明）。

`setupUi()` 末尾调 `retranslateUi();`。

- [ ] **Step 3: 实现 retranslateUi()**

```cpp
void ToolBar::retranslateUi()
{
    m_undoAction->setToolTip(tr("撤销 (Ctrl+Z)"));
    m_redoAction->setToolTip(tr("重做 (Ctrl+Y)"));

    m_headingLabel->setText(tr(" 标题 "));
    m_headingCombo->setItemText(0, tr("正文"));  // H1~H6 保持不变

    m_boldAction->setToolTip(tr("加粗 (Ctrl+B)"));
    m_italicAction->setToolTip(tr("斜体 (Ctrl+I)"));
    m_strikeAction->setToolTip(tr("删除线"));
    m_underlineAction->setToolTip(tr("下划线"));
    m_highlightAction->setToolTip(tr("高亮"));
    m_clearFormatAction->setToolTip(tr("清除样式"));
    m_textColorAction->setToolTip(tr("文字颜色"));
    m_bgColorAction->setToolTip(tr("背景颜色"));

    m_alignButton->setToolTip(tr("段落对齐"));
    m_alignLeftAct->setText(tr("左对齐(&L)"));
    m_alignCenterAct->setText(tr("居中对齐(&E)"));
    m_alignRightAct->setText(tr("右对齐(&R)"));

    m_subsupButton->setToolTip(tr("上标/下标"));
    m_superscriptAct->setText(tr("上标(&P)"));
    m_subscriptAct->setText(tr("下标(&B)"));

    m_bulletListAction->setToolTip(tr("无序列表"));
    m_orderedListAction->setToolTip(tr("有序列表"));
    m_taskListAction->setToolTip(tr("任务列表"));
    m_blockquoteAction->setToolTip(tr("引用"));

    findChild<QAction*>("linkAction")->setToolTip(tr("插入链接"));
    findChild<QAction*>("imageAction")->setToolTip(tr("插入图片"));
    findChild<QAction*>("tableAction")->setToolTip(tr("插入表格"));
    m_codeAction->setToolTip(tr("行内代码"));
    m_codeBlockAction->setToolTip(tr("代码块"));
    findChild<QAction*>("hrAction")->setToolTip(tr("分割线"));
    findChild<QAction*>("mathAction")->setToolTip(tr("插入公式 (Ctrl+M)"));
    findChild<QAction*>("diagramAction")->setToolTip(tr("插入 Mermaid 图表"));
    findChild<QAction*>("emojiAction")->setToolTip(tr("插入表情"));
    findChild<QAction*>("footnoteAction")->setToolTip(tr("插入脚注"));

    findChild<QAction*>("sourceAction")->setToolTip(tr("切换源码模式"));

    m_exportButton->setToolTip(tr("导出"));
    m_exportMenu->actions().at(0)->setText(tr("导出 PDF..."));
    m_exportMenu->actions().at(1)->setText(tr("导出 DOCX..."));
    m_exportMenu->actions().at(2)->setText(tr("导出 HTML..."));
}
```

注：`buildColorMenu` 每次新建，文案用 `tr()` 即可无需重设（`无文字颜色/无背景颜色/更多颜色…/选择文字颜色/选择背景颜色` 在创建时取当前语言）。`m_exportMenu` 须在 setupUi 里 `new QMenu(m_exportButton)` 后赋给成员（原为局部 `exportMenu`）。

- [ ] **Step 4: 补齐 .ts（ToolBar 上下文）**

`<context><name>ToolBar</name>` 翻译表：

| 中文 | 英文 |
|---|---|
| 撤销 (Ctrl+Z) | Undo (Ctrl+Z) |
| 重做 (Ctrl+Y) | Redo (Ctrl+Y) |
| 标题 | Heading |
| 正文 | Body |
| 加粗 (Ctrl+B) | Bold (Ctrl+B) |
| 斜体 (Ctrl+I) | Italic (Ctrl+I) |
| 删除线 | Strikethrough |
| 下划线 | Underline |
| 高亮 | Highlight |
| 清除样式 | Clear Format |
| 文字颜色 | Text Color |
| 背景颜色 | Background Color |
| 段落对齐 | Align |
| 左对齐(&L) | Align &Left |
| 居中对齐(&E) | Align &Center |
| 右对齐(&R) | Align &Right |
| 上标/下标 | Superscript/Subscript |
| 上标(&P) | Superscript |
| 下标(&B) | Subscript |
| 无序列表 | Bullet List |
| 有序列表 | Ordered List |
| 任务列表 | Task List |
| 引用 | Blockquote |
| 插入链接 | Insert Link |
| 插入图片 | Insert Image |
| 插入表格 | Insert Table |
| 行内代码 | Inline Code |
| 代码块 | Code Block |
| 分割线 | Horizontal Rule |
| 插入公式 (Ctrl+M) | Insert Formula (Ctrl+M) |
| 插入 Mermaid 图表 | Insert Mermaid Diagram |
| 插入表情 | Insert Emoji |
| 插入脚注 | Insert Footnote |
| 切换源码模式 | Toggle Source Mode |
| 导出 | Export |
| 导出 PDF... | Export PDF... |
| 导出 DOCX... | Export DOCX... |
| 导出 HTML... | Export HTML... |
| 无文字颜色 | No Text Color |
| 无背景颜色 | No Background Color |
| 更多颜色… | More Colors… |
| 选择文字颜色 | Choose Text Color |
| 选择背景颜色 | Choose Background Color |

- [ ] **Step 5: 构建 + Commit**

```bash
cmake --build build -j$(nproc)
git add src/ui/ToolBar.h src/ui/ToolBar.cpp src/resources/i18n/md-editor_en_US.ts
git commit -m "feat: ToolBar 接入 tr/retranslateUi"
```

---

### Task 6: StatusBar + SideBar + FileTree + OutlineTree 包 tr() + retranslateUi

**Files:**
- Modify: `src/ui/StatusBar.h/.cpp`
- Modify: `src/ui/SideBar.h/.cpp`
- Modify: `src/ui/FileTree.h/.cpp`
- Modify: `src/ui/OutlineTree.h/.cpp`

**Interfaces:**
- Consumes: 无。
- Produces: `StatusBar::retranslateUi()`、`SideBar::retranslateUi()`、`FileTree::retranslateUi()`、`OutlineTree::retranslateUi()`。Task 10 调用。

- [ ] **Step 1: StatusBar**

`StatusBar.h` 加 `void retranslateUi();`（public）与缓存成员 `int m_line = 1, m_col = 1, m_chars = 0, m_words = 0;`。`updateCursorPosition` / `updateWordCount` 里更新缓存并统一走渲染。

`StatusBar.cpp` 构造函数把初始 `new QLabel("行 1, 列 1", ...)` 改为 `new QLabel(this)` 等空标签，末尾调 `retranslateUi()`；`updateCursorPosition`/`updateWordCount` 存值后调 `retranslateUi()`：

```cpp
void StatusBar::retranslateUi()
{
    m_cursorLabel->setText(tr("行 %1, 列 %2").arg(m_line).arg(m_col));
    m_sourceModeLabel->setText(tr("源码模式"));
    m_wordLabel->setText(tr("字符: %1 / 词: %2").arg(m_chars).arg(m_words));
}

void StatusBar::updateCursorPosition(int line, int col, int /*tabId*/)
{
    m_line = line; m_col = col;
    m_cursorLabel->setText(tr("行 %1, 列 %2").arg(line).arg(col));
}

void StatusBar::updateWordCount(int chars, int words, int /*tabId*/)
{
    m_chars = chars; m_words = words;
    m_wordLabel->setText(tr("字符: %1 / 词: %2").arg(chars).arg(words));
}
```

- [ ] **Step 2: SideBar**

`SideBar.h` 加 `void retranslateUi();` 与成员 `QTabWidget *m_tabWidget; QPushButton *m_openFolderBtn;`（后者已有）。`setupUi` 里 `tabWidget->addTab(m_fileTree, tr("文件"))`、`tr("大纲")`、按钮 `tr("打开文件夹...")` 用成员 + tr()，末尾调 retranslateUi：

```cpp
void SideBar::retranslateUi()
{
    m_tabWidget->setTabText(0, tr("文件"));
    m_tabWidget->setTabText(1, tr("大纲"));
    m_openFolderBtn->setText(tr("打开文件夹..."));
}
```

- [ ] **Step 3: FileTree**

`FileTree.h` 加 `void retranslateUi();`。`FileTree.cpp` 的 `showContextMenu()` 里菜单/对话框文案改 `tr()`（动态创建，无需缓存重设）；`QInputDialog::getText` 的标题/标签也 `tr()`。retranslateUi 为空操作也可，但为统一接口提供一个空实现（或省略 retranslateUi，仅 tr()——本计划保留空 `retranslateUi(){}` 以统一 Task 10 调用，避免歧义）。文案清单：新建 Markdown 文件 / 重命名 / 删除 / 在文件管理器中打开 / 新建文件 / 文件名: / 新文件名: / 确认删除 / 确定要删除 '%1' 吗？。

- [ ] **Step 4: OutlineTree**

`OutlineTree.h` 加 `void retranslateUi();`。`OutlineTree.cpp` 构造里 `setHorizontalHeaderLabels({tr("大纲")})`；`updateHeadings` 里 `m_model->clear()` 后 header 会被清空，故在 `updateHeadings` 末尾或 `clear()` 后重设 `m_model->setHorizontalHeaderLabels({tr("大纲")})`。retranslateUi 实现：

```cpp
void OutlineTree::retranslateUi()
{
    m_model->setHorizontalHeaderLabels({tr("大纲")});
}
```

（注意：`updateHeadings` 里 `m_model->clear()` 会清 header，需在 clear 后重设；已在 retranslateUi 与 updateHeadings 双处设置即可。）

- [ ] **Step 5: 补齐 .ts**

加 `<context><name>StatusBar</name>`、`<context><name>SideBar</name>`、`<context><name>FileTree</name>`、`<context><name>OutlineTree</name>`。翻译表：

| context | 中文 | 英文 |
|---|---|---|
| StatusBar | 行 %1, 列 %2 | Line %1, Col %2 |
| StatusBar | 源码模式 | Source Mode |
| StatusBar | 字符: %1 / 词: %2 | Chars: %1 / Words: %2 |
| SideBar | 文件 | Files |
| SideBar | 大纲 | Outline |
| SideBar | 打开文件夹... | Open Folder... |
| FileTree | 新建 Markdown 文件 | New Markdown File |
| FileTree | 重命名 | Rename |
| FileTree | 删除 | Delete |
| FileTree | 在文件管理器中打开 | Open in File Manager |
| FileTree | 新建文件 | New File |
| FileTree | 文件名: | File name: |
| FileTree | 新文件名: | New file name: |
| FileTree | 确认删除 | Confirm Delete |
| FileTree | 确定要删除 '%1' 吗？ | Delete '%1'? |
| OutlineTree | 大纲 | Outline |

- [ ] **Step 6: 构建 + Commit**

```bash
cmake --build build -j$(nproc)
git add src/ui/StatusBar.h src/ui/StatusBar.cpp src/ui/SideBar.h src/ui/SideBar.cpp \
        src/ui/FileTree.h src/ui/FileTree.cpp src/ui/OutlineTree.h src/ui/OutlineTree.cpp \
        src/resources/i18n/md-editor_en_US.ts
git commit -m "feat: StatusBar/SideBar/FileTree/OutlineTree 接入 tr/retranslateUi"
```

---

### Task 7: TabWidget + SearchBar 包 tr() + retranslateUi

**Files:**
- Modify: `src/ui/TabWidget.h/.cpp`
- Modify: `src/ui/SearchBar.h/.cpp`

**Interfaces:**
- Consumes: 无。
- Produces: `TabWidget::retranslateUi()`、`SearchBar::retranslateUi()`。Task 10 调用。

- [ ] **Step 1: TabWidget**

`TabWidget.h` 加 `void retranslateUi();`。`TabWidget.cpp` 的 `generateUntitledName()` 改：

```cpp
QString TabWidget::generateUntitledName()
{
    int n = 1;
    for (int i = 0; i < count(); ++i) {
        // 未命名 tab 无文件路径（tooltip 空），跨语言计数，避免依赖「未命名-」前缀
        if (tabToolTip(i).isEmpty()) n++;
    }
    return tr("未命名-%1").arg(n);
}
```

`onTabCloseRequested` 的 `QMessageBox::question` 标题/文案改 `tr()`（动态，无需重设）。`retranslateUi()` 重命名未命名 tab（保留脏标记前缀）：

```cpp
void TabWidget::retranslateUi()
{
    // 仅重命名未命名 tab（无文件路径）；保留「● 」脏标记前缀
    int n = 1;
    for (int i = 0; i < count(); ++i) {
        if (tabToolTip(i).isEmpty()) {
            QString t = tr("未命名-%1").arg(n++);
            if (tabText(i).startsWith("● "))
                t = "● " + t;
            setTabText(i, t);
        }
    }
}
```

- [ ] **Step 2: SearchBar**

`SearchBar.h` 加 `void retranslateUi();`。`SearchBar.cpp` 构造里文案改 `tr()`（占位符/tooltip/按钮），末尾调 `retranslateUi()`。`mkToggle` 的 `tip` 参数、按钮 `text` 已在构造中用 `tr()`；retranslateUi 重设 tooltip 与占位符：

```cpp
void SearchBar::retranslateUi()
{
    m_findEdit->setPlaceholderText(tr("查找..."));
    m_caseBtn->setToolTip(tr("区分大小写"));
    m_regexBtn->setToolTip(tr("正则表达式"));
    m_wordBtn->setToolTip(tr("全词匹配"));
    m_prevBtn->setToolTip(tr("上一个 (Shift+F3)"));
    m_nextBtn->setToolTip(tr("下一个 (F3)"));
    m_closeBtn->setToolTip(tr("关闭 (Esc)"));
    m_replaceEdit->setPlaceholderText(tr("替换为..."));
    m_replaceBtn->setText(tr("替换"));
    m_replaceAllBtn->setText(tr("全部替换"));
}
```

（`Aa`/`.*`/`W|`/`↑`/`↓`/`✕` 为符号，不翻译。）

- [ ] **Step 3: 补齐 .ts**

`<context><name>TabWidget</name>`：未命名-%1 → Untitled-%1、未保存的更改 → Unsaved Changes、文件 '%1' 有未保存的更改。\n是否保存？ → File '%1' has unsaved changes.\nSave?。

`<context><name>SearchBar</name>`：查找... → Find...、区分大小写 → Match Case、正则表达式 → Regular Expression、全词匹配 → Whole Word、上一个 (Shift+F3) → Previous (Shift+F3)、下一个 (F3) → Next (F3)、关闭 (Esc) → Close (Esc)、替换为... → Replace with...、替换 → Replace、全部替换 → Replace All。

- [ ] **Step 4: 构建 + Commit**

```bash
cmake --build build -j$(nproc)
git add src/ui/TabWidget.h src/ui/TabWidget.cpp src/ui/SearchBar.h src/ui/SearchBar.cpp \
        src/resources/i18n/md-editor_en_US.ts
git commit -m "feat: TabWidget/SearchBar 接入 tr/retranslateUi"
```

---

### Task 8: EditorTab 右键菜单 + MainWindow 瞬态文案包 tr()

**Files:**
- Modify: `src/ui/EditorTab.cpp`（右键菜单 `setupContextMenu`）
- Modify: `src/ui/MainWindow.cpp`（瞬态文案 `tr()`）

**Interfaces:**
- Consumes: 无。
- Produces: 无新公开接口（MainWindow 编排见 Task 10）。

- [ ] **Step 1: EditorTab 右键菜单包 tr()**

`EditorWebView::setupContextMenu()` 里所有 `addAction("撤销(&U)")` 等改 `tr()`（动态重建，无需 retranslateUi）。文案清单：撤销(&U)/重做(&R)/剪切(&X)/复制(&C)/粘贴(&V)/全选(&A)/编辑超链接.../访问 %1/表格/上方插入行/下方插入行/删除当前行/左侧插入列/右侧插入列/删除当前列/删除表格。

- [ ] **Step 2: MainWindow 瞬态文案包 tr()**

`MainWindow.cpp` 中所有用户可见瞬态字符串（`QFileDialog::getOpenFileName(this, "打开文件", ...)`、`QMessageBox::question(this, "文件已变更", ...)`、`m_statusBar->showMessage("已保存", ...)`、`exportDefaultName` 外的导出标题等）改 `tr("...")`。这些在调用时求值，切语言后下次出现即正确。文案清单：

打开文件 / Markdown 文件 (*.md *.markdown);;所有文件 (*) / 打开文件夹 / 已打开文件夹: %1 / 打开失败 / 无法打开文件: %1 / 文件已变更 / 文件 %1 已被外部程序修改。\n是否重新加载？ / 导出 PDF / PDF 文件 (*.pdf) / 已导出: %1 / 导出失败 / 导出 DOCX / Word 文档 (*.docx) / 导出 HTML / HTML 文件 (*.html) / 插入图片 / 图片文件 (*.png *.jpg *.jpeg *.gif *.svg *.webp) / 请先保存文档再插入图片 / 图片数据为空 / 图片保存失败 / 已插入图片 / 源码模式暂不支持搜索 / 已保存 / 保存失败 / 另存为 / 未保存的更改 / '%1' 有未保存的更改。\n\n是否保存后关闭？ / Markdown 语法。

- [ ] **Step 3: 补齐 .ts**

加 `<context><name>MainWindow</name>` 与 `<context><name>EditorWebView</name>`（注意：右键菜单在 `EditorWebView` 类内，`tr()` 的上下文是 `EditorWebView`，不是 EditorTab）。翻译表：

MainWindow（见 Step 2 清单）对应英文：Open File / Markdown Files (*.md *.markdown);;All Files (*) / Open Folder / Opened Folder: %1 / Open Failed / Cannot open file: %1 / File Changed / File %1 was modified externally.\nReload? / Export PDF / PDF Files (*.pdf) / Exported: %1 / Export Failed / Export DOCX / Word Documents (*.docx) / Export HTML / HTML Files (*.html) / Insert Image / Image Files (*.png *.jpg *.jpeg *.gif *.svg *.webp) / Save the document before inserting an image / Image data is empty / Failed to save image / Image inserted / Search is not supported in source mode / Saved / Save Failed / Save As / Unsaved Changes / '%1' has unsaved changes.\n\nSave before closing? / Markdown Guide。

EditorWebView：撤销(&U)→&Undo、重做(&R)→&Redo、剪切(&X)→Cu&t、复制(&C)→&Copy、粘贴(&V)→&Paste、全选(&A)→Select &All、编辑超链接...→Edit Link...、访问 %1→Visit %1、表格→Table、上方插入行→Insert Row Above、下方插入行→Insert Row Below、删除当前行→Delete Row、左侧插入列→Insert Column Left、右侧插入列→Insert Column Right、删除当前列→Delete Column、删除表格→Delete Table。

- [ ] **Step 4: 构建 + Commit**

```bash
cmake --build build -j$(nproc)
git add src/ui/EditorTab.cpp src/ui/MainWindow.cpp src/resources/i18n/md-editor_en_US.ts
git commit -m "feat: EditorTab 右键菜单与 MainWindow 瞬态文案接入 tr"
```

---

### Task 9: editor.js 字典 + setLanguage + data-i18n

**Files:**
- Modify: `src/resources/web/editor.js`

**Interfaces:**
- Consumes: 无。
- Produces: `window.editorApi.setLanguage(lang)`（lang 为 `"zh_CN"`/`"en_US"`）；模块级 `i18n(key)` 辅助；`data-i18n` 属性约定。Task 10 的 `EditorTab::setEditorLanguage` 调用 `window.editorApi.setLanguage(...)`。

- [ ] **Step 1: 加 I18N 字典与辅助函数**

在 `window.editorApi = {`（约 950 行）之前插入：

```js
// 界面文案字典（源语言中文；emoji 分类名保持中文不在此列）
var I18N = {
  zh_CN: {
    'placeholder.empty': '开始编写 Markdown...',
    'common.cancel': '取消',
    'common.ok': '确定',
    'common.close': '关闭',
    'link.editTitle': '编辑超链接',
    'link.text': '链接文字',
    'link.url': '链接地址',
    'link.remove': '删除链接',
    'link.tip.click': '点击访问',
    'link.tip.ctrlClick': 'Ctrl+点击访问',
    'math.blockTag': '块级公式',
    'math.inlineTag': '行内公式',
    'math.latexLabel': 'LaTeX 源码',
    'math.preview': '预览',
    'math.previewHint': '（输入 LaTeX 公式）',
    'mermaid.editTitle': '编辑 Mermaid 图表',
    'mermaid.insertTitle': '插入 Mermaid 图表',
    'mermaid.srcLabel': 'Mermaid 源码（如 graph TD; A-->B）',
    'mermaid.hint': '支持 flowchart / sequence / class / state / pie / gantt 等，详见 mermaid.js.org',
    'mermaid.previewHint': '（输入 Mermaid 源码预览图表）',
    'mermaid.renderFail': '渲染失败：',
    'mermaid.empty': '（空图表，双击编辑）',
    'codeLang.search': '搜索语言...',
    'codeLang.auto': '自动检测',
    'codeLang.current': ' (当前)',
    'codeblock.copy': '复制代码',
    'codeblock.changeLang': '双击修改语言',
    'footnote.editId': '双击修改脚注标记',
    'emoji.search': '搜索表情（如 笑 / heart / +1）',
    'emoji.freq': '常用表情符号',
    'emoji.empty': '无匹配表情',
    'editor.failInit': '编辑器内核加载失败：未找到 window.TipTap。请检查 tiptap-all.js。'
  },
  en_US: {
    'placeholder.empty': 'Start writing Markdown...',
    'common.cancel': 'Cancel',
    'common.ok': 'OK',
    'common.close': 'Close',
    'link.editTitle': 'Edit Link',
    'link.text': 'Link Text',
    'link.url': 'Link URL',
    'link.remove': 'Remove Link',
    'link.tip.click': 'Click to visit',
    'link.tip.ctrlClick': 'Ctrl+Click to visit',
    'math.blockTag': 'Block Formula',
    'math.inlineTag': 'Inline Formula',
    'math.latexLabel': 'LaTeX Source',
    'math.preview': 'Preview',
    'math.previewHint': '(Enter a LaTeX formula)',
    'mermaid.editTitle': 'Edit Mermaid Diagram',
    'mermaid.insertTitle': 'Insert Mermaid Diagram',
    'mermaid.srcLabel': 'Mermaid Source (e.g. graph TD; A-->B)',
    'mermaid.hint': 'Supports flowchart / sequence / class / state / pie / gantt, etc. See mermaid.js.org',
    'mermaid.previewHint': '(Enter Mermaid source to preview)',
    'mermaid.renderFail': 'Render failed: ',
    'mermaid.empty': '(Empty diagram, double-click to edit)',
    'codeLang.search': 'Search language...',
    'codeLang.auto': 'Auto Detect',
    'codeLang.current': ' (current)',
    'codeblock.copy': 'Copy code',
    'codeblock.changeLang': 'Double-click to change language',
    'footnote.editId': 'Double-click to edit footnote marker',
    'emoji.search': 'Search emoji (e.g. smile / heart / +1)',
    'emoji.freq': 'Frequently used',
    'emoji.empty': 'No matching emoji',
    'editor.failInit': 'Editor core failed to load: window.TipTap not found. Check tiptap-all.js.'
  }
};

// 当前界面语言（与 C++ LanguageManager 同步；默认中文）
var _uiLang = 'zh_CN';

function i18n(key) {
  var t = I18N[_uiLang];
  if (t && t[key] !== undefined) return t[key];
  return I18N.zh_CN[key] !== undefined ? I18N.zh_CN[key] : key;
}

// 把元素文本设为 i18n(key) 并打 data-i18n 标记（setLanguage 时全量重刷）
function _setI18n(el, key) {
  el.setAttribute('data-i18n', key);
  el.textContent = i18n(key);
}
```

- [ ] **Step 2: 在 editorApi 对象加 `_lang` 成员与 setLanguage 方法**

在 `window.editorApi = {` 顶部成员区（`_readOnly: false,` 附近）加 `_lang: 'zh_CN',`。在 `setTheme` 方法附近新增：

```js
    setLanguage: function(lang) {
        this._lang = lang;
        _uiLang = lang;
        // 全量重刷带 data-i18n 标记的静态标签（NodeView tooltip/空态/弹窗按钮等）
        var els = document.querySelectorAll('[data-i18n]');
        for (var i = 0; i < els.length; i++) {
            var key = els[i].getAttribute('data-i18n');
            if (key) els[i].textContent = i18n(key);
        }
        // 占位符：TipTap Placeholder 配置为函数读取当前语言，派发一个空事务触发重渲染
        if (this.editor) {
            var ed = this.editor;
            ed.view.dispatch(ed.state.tr);  // 无内容变更，仅触发视图/装饰重算
        }
    },
```

- [ ] **Step 3: 占位符改用函数**

`_buildExtensions` 里 `TipTapPlaceholder.configure({ placeholder: '开始编写 Markdown...' })` 改为：

```js
            TipTapPlaceholder.configure({ placeholder: function() { return i18n('placeholder.empty'); } }),
```

- [ ] **Step 4: 弹窗/NodeView 文案改用 i18n + data-i18n**

把下列位置的 `textContent`/`title`/`placeholder` 中文硬编码改为 `i18n(...)`（静态标签用 `_setI18n` 打标记，便于 setLanguage 重刷）：

- `_createEditor` 加载失败分支：`container.textContent = i18n('editor.failInit')`；`init` 里同样。
- 代码块 NodeView（约 835-900 行）：`label.title = i18n('codeblock.changeLang')`、`copyBtn.title = i18n('codeblock.copy')`。
- Mermaid 空态（约 683 行）：`'<div class="mermaid-empty">' + i18n('mermaid.empty') + '</div>'`。
- 脚注 NodeView（约 590 行）：`dom.title = i18n('footnote.editId')`。
- `_mathEditor.buildDom`：`tag.textContent = isBlock ? i18n('math.blockTag') : i18n('math.inlineTag')`、`closeBtn.title = i18n('common.close')`、`label.textContent = i18n('math.latexLabel')`、`prevLabel.textContent = i18n('math.preview')`、`cancelBtn.textContent = i18n('common.cancel')`、`okBtn.textContent = i18n('common.ok')`、预览提示 `'（输入 LaTeX 公式）'` → `i18n('math.previewHint')`。
- `_mermaidEditor.buildDom`：`tag.textContent = this.mode === 'edit' ? i18n('mermaid.editTitle') : i18n('mermaid.insertTitle')`、`label.textContent = i18n('mermaid.srcLabel')`、`prevLabel.textContent = i18n('mermaid.preview')`、`hint.textContent = i18n('mermaid.hint')`、取消/确定按钮 `i18n('common.cancel')`/`i18n('common.ok')`、预览提示与「渲染失败：」→ `i18n('mermaid.previewHint')`/`i18n('mermaid.renderFail')`。
- `_linkEditor.buildDom`：`tag.textContent = i18n('link.editTitle')`、`textLabel.textContent = i18n('link.text')`、`urlLabel.textContent = i18n('link.url')`、`removeBtn.textContent = i18n('link.remove')`、`cancelBtn.textContent = i18n('common.cancel')`、`okBtn.textContent = i18n('common.ok')`。
- `_codeLangPicker.buildDom`：`searchInput.placeholder = i18n('codeLang.search')`；`render` 里 `自动检测` → `i18n('codeLang.auto')`、`+ ' (当前)'` → `+ i18n('codeLang.current')`。
- `_emojiPicker.buildDom`：`searchInput.placeholder = i18n('emoji.search')`、`freqLabel.textContent = i18n('emoji.freq')`；`render` 里 `无匹配表情` → `i18n('emoji.empty')`。emoji 分类标签 `b.textContent = cat.name` 保持不变（中文）。
- `_syncLinkHandlers` 悬浮提示（约 1755 行）：`var label = readOnly ? i18n('link.tip.click') : i18n('link.tip.ctrlClick');`。

- [ ] **Step 5: 构建 + 手动验证 JS 弹窗双语**

`editor.js` 打进 qrc，必须重编译：

```bash
cmake --build build -j$(nproc)
```

启动应用（英文模式），依次打开 链接编辑器 / 数学公式 / Mermaid / 代码语言选择器 / emoji 选择器，确认文案英文、emoji 分类名仍中文。源码模式下帮助页（Markdown 语法）标题已由 Task 8 的 MainWindow `tr()` 覆盖。

- [ ] **Step 6: Commit**

```bash
git add src/resources/web/editor.js
git commit -m "feat: editor.js I18N 字典与 setLanguage 运行时切换"
```

---

### Task 10: MainWindow 编排 + SettingsDialog 接线 + 新 tab 语言补推

**Files:**
- Modify: `src/ui/MainWindow.h`（加 `onLanguageSwitch` 槽 + 辅助方法声明）
- Modify: `src/ui/MainWindow.cpp`
- Modify: `src/ui/EditorTab.h/.cpp`（加 `setEditorLanguage`）

**Interfaces:**
- Consumes: `LanguageManager::switchLanguage`、`Application::languageManager()`、各组件 `retranslateUi()`（Task 3-7）、`SettingsDialog::languageChanged`（Task 3）、`window.editorApi.setLanguage`（Task 9）。
- Produces: 完整运行时切换链路；后续无依赖。

- [ ] **Step 1: EditorTab::setEditorLanguage**

`EditorTab.h` 加 `void setEditorLanguage(const QString &lang);`；`EditorTab.cpp` 实现：

```cpp
void EditorTab::setEditorLanguage(const QString &lang)
{
    if (m_pageLoaded) {
        runJsCommand(QString("window.editorApi.setLanguage('%1');").arg(lang));
    }
}
```

- [ ] **Step 2: MainWindow 加槽声明**

`MainWindow.h` private slots 加 `void onLanguageSwitch(const QString &lang);`；private 加 `void retranslateUi();`。

- [ ] **Step 3: 实现 onLanguageSwitch 与 retranslateUi**

`MainWindow.cpp` 新增：

```cpp
void MainWindow::retranslateUi()
{
    m_menuBar->retranslateUi();
    m_toolBar->retranslateUi();
    m_statusBar->retranslateUi();
    m_sideBar->retranslateUi();
    m_sideBar->fileTree()->retranslateUi();
    m_sideBar->outlineTree()->retranslateUi();
    m_tabWidget->retranslateUi();
    m_searchBar->retranslateUi();
}

void MainWindow::onLanguageSwitch(const QString &lang)
{
    Application::instance()->languageManager()->switchLanguage(lang);  // 装/卸 translator + 持久化 + 发信号
    retranslateUi();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->setEditorLanguage(lang);
    }
}
```

- [ ] **Step 4: 接通 SettingsDialog 信号**

`MainWindow::onShowSettings()` 里，在 `connect(&dialog, &SettingsDialog::themeModeChanged, ...)` 之后加：

```cpp
    connect(&dialog, &SettingsDialog::languageChanged,
            this, [this, &dialog](const QString &lang) {
        onLanguageSwitch(lang);
        dialog.retranslateUi();  // 对话框自身随语言切换即时刷新
    });
```

- [ ] **Step 5: 新 tab 语言补推（对齐主题既有缺口）**

`MainWindow::onEditorBridgeCreated` 里 `pageLoadFinished` 的 lambda（约 442-449 行）已补推主题；在其后加：

```cpp
            tab->setEditorLanguage(
                Application::instance()->languageManager()->currentLanguage());
```

- [ ] **Step 6: 构建 + 端到端手测**

```bash
cmake --build build -j$(nproc)
./build/src/md-editor
```
手测清单：① 设置页切 English → 菜单/工具栏/状态栏/侧边栏/搜索/设置/右键菜单/About 全英文 ② 打开各 JS 弹窗英文、emoji 分类中文 ③ 新建标签页继承英文 ④ 重启记住英文 ⑤ 切回中文全还原。

- [ ] **Step 7: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp src/ui/EditorTab.h src/ui/EditorTab.cpp
git commit -m "feat: MainWindow 编排运行时语言切换并补推新 tab 语言"
```

---

### Task 11: 终验——lupdate 重生成完整 .ts + 补齐翻译 + 版本推进

**Files:**
- Modify: `src/resources/i18n/md-editor_en_US.ts`（lupdate 重生成 + 补全英文）
- Modify: `CMakeLists.txt`（版本号，与用户确认）
- Modify: `CHANGELOG.md`

**Interfaces:**
- Consumes: 全部前序任务的 `tr()` 字符串。
- Produces: 完整翻译资源与发布版本。

- [ ] **Step 1: 用 lupdate 重生成 .ts（确保无遗漏）**

```bash
cd /home/daijd/claude-workspace/md-editor
lupdate src -ts src/resources/i18n/md-editor_en_US.ts -locations none
```
预期：`.ts` 包含所有组件的 `<context>`（AboutDialog/SettingsDialog/MenuBar/ToolBar/StatusBar/SideBar/FileTree/OutlineTree/TabWidget/SearchBar/EditorWebView/MainWindow）与全部 source 串。

- [ ] **Step 2: 逐条核对并补全英文 translation**

用 Task 3-8 提供的翻译表逐 context 核对 `translation` 字段，把 lupdate 生成的空 `translation type="unfinished"` 全部填上正确英文并去掉 `type="unfinished"`。

- [ ] **Step 3: 全量重建 + ctest + GUI 终测**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
./build/src/md-editor
```
逐项跑 Task 10 Step 6 的手测清单，重点覆盖中↔英切换、重启持久化、新建 tab 继承。

- [ ] **Step 4: 版本推进（与用户确认）**

与用户确认目标版本号（建议 `0.9.0`，因新增组件 + 全量重译，规模较大；但当前 0.8.2 攒批中，以用户为准）。确认后：
1. `CMakeLists.txt` 第 2 行 `project(md-editor VERSION x.y.z ...)` 改为目标版本。
2. `CHANGELOG.md` 顶部新增该版本条目，记录 LanguageManager、中英文切换、全组件 retranslateUi、editor.js 字典。
3. 打 tag：`git tag vx.y.z`。

- [ ] **Step 5: 确认二进制时效并提交**

```bash
stat -c '%Y %n' build/src/md-editor $(find src -name '*.cpp' -o -name '*.js' -o -name '*.ts' -o -name '*.qss' -o -name '*.md' -o -name '*.qrc') | sort -rn | head
git add -A
git commit -m "feat: 完成中英文运行时切换并推进版本"
```

---

## Self-Review 记录

- **Spec 覆盖**：LanguageManager（Task 1）、翻译资源/工具链（Task 2）、全组件 retranslateUi（Task 3-8）、editor.js 字典+setLanguage（Task 9）、设置接线+编排+新 tab 补推（Task 10）、版本（Task 11）。spec 的「Qt 内建按钮不翻译」「emoji 分类中文」已在 Global Constraints 与 Task 9 落实。
- **占位符扫描**：无 TBD/TODO；所有翻译表给出具体英文。
- **类型一致性**：`setLanguage`、`retranslateUi`、`onLanguageSwitch`、`setEditorLanguage`、`languageChanged` 命名跨任务一致；`currentLanguage()`/`switchLanguage()` 与 Task 1 定义一致。
