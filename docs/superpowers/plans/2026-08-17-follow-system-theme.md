# 「跟随系统」主题实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增第三种主题模式「跟随系统」——经 xdg-desktop-portal 检测系统深浅色并实时跟随，同时修复「新 tab 不推送主题」「设置对话框不联动编辑区」两处既有缺口。

**Architecture:** 把「偏好模式」（`system`/`light`/`dark`，存 QSettings）与「实际主题」（`light`/`dark`，ThemeManager 运行时状态）分离。新增 `SystemThemeWatcher`（`src/app/`）经 D-Bus portal 读/监听系统主题，翻译成实际主题；`MainWindow::onThemeSwitch` 成为唯一的应用编排点，system 模式的解析在此完成；两个 UI 入口（菜单、设置对话框）与系统主题变化信号全部收敛到这条链路。

**Tech Stack:** Qt 6.4+（Widgets、QtDBus）、QSettings、QActionGroup、QComboBox、QtTest。

**规格:** `docs/superpowers/specs/2026-08-17-follow-system-theme-design.md`（已获批）

## Global Constraints

- 兼容基准 Qt 6.4.2（`find_package(Qt6 6.4 ...)`）：**禁止使用 Qt 6.5+ API**（`QStyleHints::colorScheme()` / `colorSchemeChanged` 均不可用）；DBus portal 路径在 6.4.2 与 6.8.x 行为一致
- 文档与代码注释一律中文，代码标识符用英文
- 提交信息：`feat:` / `fix:` / `chore:` 前缀 + 中文描述
- 主程序源文件增改需同时登记 `src/CMakeLists.txt`（`SOURCES`/`HEADERS` 列表）；测试登记 `tests/CMakeLists.txt`
- 改 C++ 源码后必须 `cmake --build build` 重编译再验证（qrc/二进制不同步 = 用户跑旧版）
- 当前处于 0.8.2 攒批阶段：**只提交，不打 tag、不推送、不动 `CMakeLists.txt` 顶部的 project VERSION**
- 运行时依赖新增 `libqt6dbus6`（Qt6::DBus 模块，qt6-base-dev 构建、独立运行时包），需同步 `CMakeLists.txt` 的 `CPACK_DEBIAN_PACKAGE_DEPENDS`
- D-Bus portal 规范地址：服务 `org.freedesktop.portal.Desktop`、路径 `/org/freedesktop/portal/desktop`、接口 `org.freedesktop.portal.Settings`；`color-scheme` 值：`1`=prefer-dark、`2`=prefer-light、`0`=no-preference

---

### Task 1: SystemThemeWatcher 组件（DBus 层抽象 + 翻译逻辑，TDD）

**Files:**
- Create: `src/app/SystemThemeWatcher.h`
- Create: `src/app/SystemThemeWatcher.cpp`
- Create: `tests/test_systemthemewatcher.cpp`
- Modify: `src/CMakeLists.txt`（SOURCES/HEADERS 各加一行）
- Modify: `tests/CMakeLists.txt`（新测试目标）
- Modify: `CMakeLists.txt`（find_package 加 DBus 组件 + DEB 依赖）

**Interfaces:**
- Consumes: 无（首个任务）
- Produces:
  - `class SystemThemeWatcher : public QObject`，构造 `explicit SystemThemeWatcher(QObject *parent = nullptr)`
  - `QString currentSystemTheme() const` — 返回 `"light"` 或 `"dark"`（降级恒 light）
  - `bool isActive() const` — portal 可用且监听已建立为 true
  - `static QString themeFromColorScheme(uint value)` — 翻译函数：`1`→`"dark"`，其余（`0`/`2`/任意）→`"light"`
  - `static SystemThemeWatcher *createForProduction(QObject *parent)` — 工厂：注入真实 portal DBus 绑定（供 MainWindow 用）
  - 信号 `void systemThemeChanged(const QString &actualTheme)` — 仅当实际主题与上次不同才发射
  - 供测试注入的钩子：`void handleSettingChanged(const QString &namespace_, const QString &key, const QVariant &value)`（public 或 public slot，mock 直接调它模拟 portal 信号）

- [ ] **Step 1: 写失败的单测**

创建 `tests/test_systemthemewatcher.cpp`：

```cpp
#include <QtTest>
#include "app/SystemThemeWatcher.h"

// SystemThemeWatcher 单测：翻译逻辑 + 变化信号 + 降级行为。
// DBus 层通过 handleSettingChanged 钩子模拟，不依赖真实 portal。
class TestSystemThemeWatcher : public QObject {
    Q_OBJECT
private slots:
    // 翻译函数：portal color-scheme 值 → 实际主题名
    void testThemeFromColorScheme() {
        QCOMPARE(SystemThemeWatcher::themeFromColorScheme(1), QString("dark"));
        QCOMPARE(SystemThemeWatcher::themeFromColorScheme(2), QString("light"));
        QCOMPARE(SystemThemeWatcher::themeFromColorScheme(0), QString("light"));  // 无偏好
    }

    // 无 portal 环境：构造即降级，恒 light，不崩溃
    void testFallbackWithoutPortal() {
        SystemThemeWatcher w;  // 默认构造 = 无 DBus 绑定（降级模式）
        QCOMPARE(w.isActive(), false);
        QCOMPARE(w.currentSystemTheme(), QString("light"));
    }

    // 模拟 portal 信号：只在值变化时发射 systemThemeChanged
    void testSignalOnlyOnChange() {
        SystemThemeWatcher w;
        QSignalSpy spy(&w, &SystemThemeWatcher::systemThemeChanged);
        w.handleSettingChanged("org.freedesktop.appearance", "color-scheme", QVariant(1));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(w.currentSystemTheme(), QString("dark"));
        // 同值再来一次：不重复发射
        w.handleSettingChanged("org.freedesktop.appearance", "color-scheme", QVariant(1));
        QCOMPARE(spy.count(), 1);
        // 变回浅色：再次发射
        w.handleSettingChanged("org.freedesktop.appearance", "color-scheme", QVariant(2));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(w.currentSystemTheme(), QString("light"));
    }

    // namespace/key 不匹配的信号：过滤不响应
    void testIgnoresOtherSettings() {
        SystemThemeWatcher w;
        QSignalSpy spy(&w, &SystemThemeWatcher::systemThemeChanged);
        w.handleSettingChanged("org.freedesktop.appearance", "accent-color", QVariant(1));
        w.handleSettingChanged("org.gnome.desktop", "color-scheme", QVariant(1));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.currentSystemTheme(), QString("light"));
    }
};

QTEST_MAIN(TestSystemThemeWatcher)
#include "test_systemthemewatcher.moc"
```

- [ ] **Step 2: 注册测试并跑，确认编译失败**

`tests/CMakeLists.txt` 末尾追加：

```cmake
add_executable(test_systemthemewatcher test_systemthemewatcher.cpp
    ../src/app/SystemThemeWatcher.cpp)
target_link_libraries(test_systemthemewatcher PRIVATE Qt6::Test Qt6::DBus)
add_test(NAME test_systemthemewatcher COMMAND test_systemthemewatcher)
```

`src/CMakeLists.txt` 的 `SOURCES` 加 `    app/SystemThemeWatcher.cpp`（app/Application.cpp 之后），`HEADERS` 加 `    app/SystemThemeWatcher.h`（app/Application.h 之后）。

顶层 `CMakeLists.txt` 两处：
- 第 10 行改为 `find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets WebEngineWidgets WebChannel DBus)`
- 第 38-39 行 `CPACK_DEBIAN_PACKAGE_DEPENDS` 字符串内追加 `libqt6dbus6 (>= 6.4)`（插在 `libqt6core6 (>= 6.4)` 之后）

`src/CMakeLists.txt` 的 `target_link_libraries` 追加一行 `    Qt6::DBus`。

Run: `cmake --build build -j$(nproc) --target test_systemthemewatcher 2>&1 | tail -5`
Expected: 编译失败——`SystemThemeWatcher.h: No such file or directory`

- [ ] **Step 3: 写最小实现**

创建 `src/app/SystemThemeWatcher.h`：

```cpp
#ifndef MD_EDITOR_SYSTEMTHEMEWATCHER_H
#define MD_EDITOR_SYSTEMTHEMEWATCHER_H

#include <QObject>
#include <QString>

// 系统深浅色监听器：经 xdg-desktop-portal 的 Settings 接口
// （org.freedesktop.portal.Desktop / org.freedesktop.portal.Settings）
// 读取并订阅 color-scheme，翻译成实际主题名（light/dark）。
//
// 设计要点：
// - DBus 层抽象为可注入：生产环境 createForProduction() 建立真实绑定，
//   单测直接调 handleSettingChanged() 模拟 portal 信号
// - 无 portal 环境（DBus 连接失败/接口缺失）静默降级恒 light，不报错
class QDBusInterface;
class SystemThemeWatcher : public QObject {
    Q_OBJECT
public:
    explicit SystemThemeWatcher(QObject *parent = nullptr);
    ~SystemThemeWatcher() override;

    // 生产工厂：连接会话总线上的 portal 并完成初始读取。
    // portal 不可用时返回降级实例（isActive() == false）
    static SystemThemeWatcher *createForProduction(QObject *parent = nullptr);

    // 当前系统主题：light 或 dark；降级模式恒 light
    QString currentSystemTheme() const { return m_currentTheme; }

    // portal 可用且监听已建立时为 true
    bool isActive() const { return m_active; }

    // 翻译函数：portal color-scheme 值 → 实际主题名
    // 1=prefer-dark → dark；2=prefer-light、0=no-preference、其余 → light
    static QString themeFromColorScheme(uint value);

    // 供测试注入：模拟 portal SettingChanged 信号（生产环境由真实 DBus 信号转发进来）
    void handleSettingChanged(const QString &namespace_, const QString &key, const QVariant &value);

signals:
    // 仅当实际主题与上次不同才发射
    void systemThemeChanged(const QString &actualTheme);

private:
    void connectToPortal();   // 建立真实 DBus 绑定（由 createForProduction 调用）
    bool readInitialValue();  // 初始 ReadOne 读取，成功返回 true

    bool m_active = false;         // portal 绑定是否成功
    QString m_currentTheme = QStringLiteral("light");
    QDBusInterface *m_portal = nullptr;  // Settings 接口代理（可能为空 = 降级）
};

#endif // MD_EDITOR_SYSTEMTHEMEWATCHER_H
```

创建 `src/app/SystemThemeWatcher.cpp`：

```cpp
#include "SystemThemeWatcher.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QVariant>
#include <QVariantMap>
#include <QDebug>

// xdg-desktop-portal Settings 接口常量
static const char kPortalService[] = "org.freedesktop.portal.Desktop";
static const char kPortalPath[] = "/org/freedesktop/portal/desktop";
static const char kPortalSettingsIface[] = "org.freedesktop.portal.Settings";
static const char kAppearanceNamespace[] = "org.freedesktop.appearance";
static const char kColorSchemeKey[] = "color-scheme";

SystemThemeWatcher::SystemThemeWatcher(QObject *parent)
    : QObject(parent)
{
}

SystemThemeWatcher::~SystemThemeWatcher()
{
}

QString SystemThemeWatcher::themeFromColorScheme(uint value)
{
    // 1=prefer-dark；2=prefer-light、0=no-preference 均按亮色处理
    return value == 1 ? QStringLiteral("dark") : QStringLiteral("light");
}

SystemThemeWatcher *SystemThemeWatcher::createForProduction(QObject *parent)
{
    auto *watcher = new SystemThemeWatcher(parent);
    watcher->connectToPortal();
    return watcher;
}

void SystemThemeWatcher::connectToPortal()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // 无 portal 服务则静默降级（恒 light）
    if (!bus.isConnected()
        || !bus.interface()->isServiceRegistered(
               QString::fromLatin1(kPortalService))) {
        qWarning() << "[SystemThemeWatcher] portal 不可用，跟随系统降级为亮色";
        return;
    }

    m_portal = new QDBusInterface(QString::fromLatin1(kPortalService),
                                  QString::fromLatin1(kPortalPath),
                                  QString::fromLatin1(kPortalSettingsIface),
                                  bus, this);

    if (!readInitialValue()) {
        // 初始读取失败：释放接口代理，降级
        qWarning() << "[SystemThemeWatcher] 读取 color-scheme 失败，降级为亮色";
        m_portal->deleteLater();
        m_portal = nullptr;
        return;
    }
    m_active = true;

    // 订阅 SettingChanged(namespace, key, value) 信号
    bus.connect(QString::fromLatin1(kPortalService),
                QString::fromLatin1(kPortalPath),
                QString::fromLatin1(kPortalSettingsIface),
                QStringLiteral("SettingChanged"),
                this, SLOT(handleSettingChanged(QString,QString,QVariant)));
}

bool SystemThemeWatcher::readInitialValue()
{
    if (!m_portal || !m_portal->isValid())
        return false;

    // ReadOne(namespace, key) → QDBusVariant 外层 QVariant
    QDBusMessage reply = m_portal->call(QStringLiteral("ReadOne"),
                                        QString::fromLatin1(kAppearanceNamespace),
                                        QString::fromLatin1(kColorSchemeKey));
    if (reply.type() != QDBusMessage::ReplyMessage)
        return false;

    // 应答形如 QDBusVariant(v uint 1)：层层解包取 uint
    const QVariant outer = reply.argumentAt(0);
    QVariant inner = outer;
    for (int i = 0; i < 3 && inner.userType() == qMetaTypeId<QDBusVariant>(); ++i) {
        inner = inner.value<QDBusVariant>().variant();
        // portal 的 ReadOne 返回 v 类型，剥一层 QDBusVariant 后可能还有一层
        // qdbusxml2cpp 风格的包装，循环内统一处理
    }
    bool ok = false;
    const uint value = inner.toUInt(&ok);
    if (!ok)
        return false;

    m_currentTheme = themeFromColorScheme(value);
    return true;
}

void SystemThemeWatcher::handleSettingChanged(const QString &namespace_,
                                              const QString &key,
                                              const QVariant &value)
{
    // 只关心 appearance 命名空间的 color-scheme
    if (namespace_ != QString::fromLatin1(kAppearanceNamespace)
        || key != QString::fromLatin1(kColorSchemeKey))
        return;

    // 信号负载是 QDBusVariant 包装的 uint
    QVariant inner = value;
    if (inner.userType() == qMetaTypeId<QDBusVariant>())
        inner = inner.value<QDBusVariant>().variant();
    bool ok = false;
    const uint schemeValue = inner.toUInt(&ok);
    if (!ok) {
        qWarning() << "[SystemThemeWatcher] color-scheme 信号负载异常，忽略";
        return;
    }

    const QString newTheme = themeFromColorScheme(schemeValue);
    if (newTheme != m_currentTheme) {
        m_currentTheme = newTheme;
        emit systemThemeChanged(newTheme);
    }
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build -j$(nproc) --target test_systemthemewatcher && ctest --test-dir build -R systemthemewatcher --output-on-failure`
Expected: 4 个测试全 PASS（本机无 portal 信号干扰；若 CI 上有真实 portal，`testFallbackWithoutPortal` 走默认构造不连 DBus，不受影响）

注意：若 `bus.connect(...SLOT(handleSettingChanged(QString,QString,QVariant)))` 返回 false（QVariant 参数的 DBus 信号匹配在个别小版本上要求严格），qWarning 记录并保持 `m_active=true`——初始读取值仍正确，仅失去实时性，不视为失败。**禁止**改用 `QDBusInterface::connectNotify` 等替代方案绕行——SLOT 宏形式在 6.4.2 与 6.8.x 上均可工作，是版本无关的正确路径。

- [ ] **Step 5: 全量构建 + 全部测试 + 提交**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: 全部测试 PASS（原有 7 个测试目标不受影响）

```bash
git add src/app/SystemThemeWatcher.h src/app/SystemThemeWatcher.cpp \
        tests/test_systemthemewatcher.cpp tests/CMakeLists.txt \
        src/CMakeLists.txt CMakeLists.txt
git commit -m "feat: SystemThemeWatcher 组件——经 portal 检测系统深浅色"
```

---

### Task 2: MainWindow 编排——模式解析、system 跟随、新 tab 补推（TDD 不适用处为接线，验证走现有 ctest + 手动）

**Files:**
- Modify: `src/ui/MainWindow.h`（成员 + 槽声明）
- Modify: `src/ui/MainWindow.cpp:133-137`（启动时恢复模式）、`MainWindow.cpp:748-759`（onThemeSwitch 重写）、`MainWindow.cpp:433-434`（pageLoadFinished 补推）

**Interfaces:**
- Consumes: Task 1 的 `SystemThemeWatcher`（`createForProduction` / `currentSystemTheme` / `systemThemeChanged` 信号）
- Consumes: Task 3 的 `MenuBar::setThemeChecked(QString)`（Task 3 完成前本任务先不留调用点，Task 3 回填——**执行顺序上 Task 2 与 Task 3 可互换，回填行已写明**）
- Produces: `void MainWindow::onThemeSwitch(const QString &mode)` — 语义从「实际主题名」变更为「偏好模式」（`system`/`light`/`dark`）；`QString MainWindow::currentThemeMode() const`（返回偏好模式，供 Task 3/4 菜单与设置页同步勾选）

- [ ] **Step 1: MainWindow.h 增加成员与访问器**

`src/ui/MainWindow.h` 的 private 部分添加（与其他成员声明风格一致）：

```cpp
    // 当前主题偏好模式（system/light/dark）
    QString currentThemeMode() const;

    // ...private 成员区（与 m_fileManager 等并列）
    SystemThemeWatcher *m_systemThemeWatcher = nullptr;
    QString m_themeMode = QStringLiteral("light");  // 偏好模式，非实际主题
```

同时在该头文件顶部添加前置声明 `class SystemThemeWatcher;`（放在其他前置声明旁；若无前置声明区，加在 `#include` 区之后）。

- [ ] **Step 2: 改 onThemeSwitch 为模式语义 + 接入 watcher**

`src/ui/MainWindow.cpp:748-759` 整个函数替换为：

```cpp
void MainWindow::onThemeSwitch(const QString &mode)
{
    m_themeMode = mode;
    // system 模式解析为当前系统实际主题；light/dark 原样使用
    QString actual = (mode == "system")
                         ? m_systemThemeWatcher->currentSystemTheme()
                         : mode;
    Application::instance()->themeManager()->switchTheme(actual);
    m_toolBar->refreshThemeIcons(actual);
    // 保存偏好模式（存的不是实际主题）
    QSettings settings("md-editor", "md-editor");
    settings.setValue("appearance/theme", mode);
    settings.sync();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        EditorTab *tab = m_tabWidget->tabAt(i);
        if (tab) tab->setEditorTheme(actual);
    }
    // Task 3 回填：m_menuBar->setThemeChecked(mode);
}
```

> 注：新加的 `m_toolBar->refreshThemeIcons(actual)` 是因为原实现靠 `themeChanged` 信号刷图标，而 system→实际主题解析后 ThemeManager 收到的已是实际主题名，信号链路不变；此处显式调用消除时序依赖（原 137 行启动路径也这么做）。两处调用导致图标刷新两次属无害幂等。

- [ ] **Step 3: 创建 watcher + 接系统变化信号 + 启动恢复模式**

`MainWindow.cpp` 构造/setupUi（现 133-137 行）替换为：

```cpp
    // 系统主题监听（无 portal 环境自动降级恒 light）
    m_systemThemeWatcher = SystemThemeWatcher::createForProduction(this);
    connect(m_systemThemeWatcher, &SystemThemeWatcher::systemThemeChanged,
            this, [this](const QString &actual) {
        // 仅 system 模式下跟随系统变化
        if (m_themeMode == "system") onThemeSwitch("system");
    });

    // 恢复主题偏好模式（system/light/dark，缺省 light）
    QSettings settings("md-editor", "md-editor");
    QString savedMode = settings.value("appearance/theme", "light").toString();
    onThemeSwitch(savedMode);
```

> 启动恢复改走 `onThemeSwitch`（原来直接调 switchTheme + refreshThemeIcons），保证启动时 tab 主题/设置写入路径一致。原 136-137 行删除。

同时在 `connectSignals()`（约 143-146 行 themeChanged → refreshThemeIcons 处）**保留不动**。

- [ ] **Step 4: 新 tab 补推主题（缺口①修复）**

`MainWindow.cpp:433-434` 现为：

```cpp
        connect(tab, &EditorTab::pageLoadFinished, this,
                [this](bool ok) { if (ok) { applyEditorFont(); applyEditorWidth(); applyEditorSettings(); applyCustomCss(); } });
```

改为（lambda 捕获加 `tab`）：

```cpp
        connect(tab, &EditorTab::pageLoadFinished, this,
                [this, tab](bool ok) {
            if (!ok) return;
            applyEditorFont(); applyEditorWidth(); applyEditorSettings(); applyCustomCss();
            // 补推当前实际主题，新 tab 与全局一致（缺口①）
            tab->setEditorTheme(
                Application::instance()->themeManager()->currentTheme());
        });
```

- [ ] **Step 5: 实现 currentThemeMode() 访问器**

`MainWindow.cpp` 文件内（onThemeSwitch 之后）添加：

```cpp
QString MainWindow::currentThemeMode() const
{
    return m_themeMode;
}
```

- [ ] **Step 6: 构建 + 测试 + 手动验证 + 提交**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: 全部 PASS

手动验证（`./scripts/run-editor.sh`，验证后关闭）：
1. 设置里切「跟随系统」→ 界面跟随当前系统主题
2. `gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark'`（GNOME）→ 界面实时变暗；切回 `'default'` → 变亮
3. 「跟随系统」下新建 tab → 编辑区主题正确（缺口①）
4. 固定亮色后系统切暗 → 应用**不**跟随（system 模式才跟随）

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat: 主题跟随系统——MainWindow 编排与新 tab 主题补推"
```

---

### Task 3: 菜单入口——第三选项与勾选同步

**Files:**
- Modify: `src/ui/MenuBar.cpp:105-109`（主题菜单）
- Modify: `src/ui/MenuBar.h`（新增 `setThemeChecked` 槽/方法）

**Interfaces:**
- Consumes: 无新依赖（`switchTheme(QString)` 信号已有，`MenuBar.cpp:107-109` 现发 `light`/`dark`，改发三值之一）
- Produces: `void MenuBar::setThemeChecked(const QString &mode)` — 按 mode 勾选对应 QAction（`system`/`light`/`dark`）；MainWindow 在 onThemeSwitch 尾部调用（Task 2 预留回填行）

- [ ] **Step 1: MenuBar.h 声明**

`MenuBar.h` public 方法区添加：

```cpp
    // 按偏好模式（system/light/dark）勾选主题菜单项
    void setThemeChecked(const QString &mode);
```

- [ ] **Step 2: MenuBar.cpp 三选项 + QActionGroup 互斥**

`MenuBar.cpp:105-109` 替换为：

```cpp
    QMenu *themeMenu = m_viewMenu->addMenu("主题");
    QActionGroup *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    auto addThemeAct = [&](const QString &label, const QString &mode, const QString &checked) {
        QAction *act = themeMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(mode == checked);
        act->setActionGroup(themeGroup);
        connect(act, &QAction::triggered, this, [this, mode]() { emit switchTheme(mode); });
        return act;
    };
    const QString savedMode = [] {
        QSettings settings("md-editor", "md-editor");
        return settings.value("appearance/theme", "light").toString();
    }();
    m_themeActs.clear();
    m_themeActs << addThemeAct("跟随系统", "system", savedMode)
                << addThemeAct("亮色", "light", savedMode)
                << addThemeAct("暗色", "dark", savedMode);
```

`MenuBar.h` private 成员加：

```cpp
    QList<QAction *> m_themeActs;  // 主题菜单项（与 system/light/dark 顺序对应）
```

`MenuBar.cpp` 顶部若无 `#include <QSettings>` 与 `#include <QActionGroup>` 则补上。

- [ ] **Step 3: setThemeChecked 实现**

`MenuBar.cpp` 添加：

```cpp
void MenuBar::setThemeChecked(const QString &mode)
{
    static const QMap<QString, int> kIndex = {
        {"system", 0}, {"light", 1}, {"dark", 2}};
    const int idx = kIndex.value(mode, 1);
    if (idx < m_themeActs.size())
        m_themeActs[idx]->setChecked(true);
}
```

- [ ] **Step 3.5: 回填 Task 2 预留行**

`MainWindow.cpp` onThemeSwitch 尾部注释 `// Task 3 回填：m_menuBar->setThemeChecked(mode);` 替换为实调用：

```cpp
    m_menuBar->setThemeChecked(mode);
```

- [ ] **Step 4: 构建 + 手动验证 + 提交**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: 全部 PASS

手动验证：
1. 菜单「查看→主题」出现三项：跟随系统/亮色/暗色，互斥勾选
2. 设置页切「跟随系统」→ 菜单勾选同步跳到「跟随系统」
3. system 模式下系统切深色 → 菜单勾选仍在「跟随系统」不动（模式未变）

```bash
git add src/ui/MenuBar.h src/ui/MenuBar.cpp src/ui/MainWindow.cpp
git commit -m "feat: 主题菜单增加跟随系统选项与勾选同步"
```

---

### Task 4: 设置对话框——第三选项与统一入口（修复缺口②）

**Files:**
- Modify: `src/ui/SettingsDialog.cpp:77-80`（下拉框加项）
- Modify: `src/ui/SettingsDialog.cpp:225-252`（onAccepted 重写）
- Modify: `src/ui/SettingsDialog.cpp:220-221`（loadSettings）

**Interfaces:**
- Consumes: Task 2 的 `MainWindow::currentThemeMode()`（设置页打开时显示当前模式而非实际主题）
- Consumes: `MainWindow::onThemeSwitch(mode)` —— 通过信号接入（见 Step 2）
- Produces: 无（叶子节点）

- [ ] **Step 1: 下拉框加「跟随系统」**

`SettingsDialog.cpp:77-80` 改为：

```cpp
    m_themeCombo = new QComboBox();
    m_themeCombo->addItem("跟随系统", "system");
    m_themeCombo->addItem("亮色", "light");
    m_themeCombo->addItem("暗色", "dark");
    appearanceForm->addRow("主题:", m_themeCombo);
```

- [ ] **Step 2: onAccepted 统一走 MainWindow 链路**

现状 `SettingsDialog.cpp:225-252`：`onAccepted` 自己调 `themeManager->switchTheme(theme)`（缺口②根源——不联动编辑区）。

设计决定：SettingsDialog 无 MainWindow 指针（构造仅 `(FileManager*, QWidget*)`）。新增信号 `themeModeChanged(QString)`，由 MainWindow 连接到 `onThemeSwitch`：

`SettingsDialog.h` signals 区加：

```cpp
    // 用户在外观页确定新的主题模式（system/light/dark）——由 MainWindow 统一应用
    void themeModeChanged(const QString &mode);
```

`SettingsDialog::onAccepted` 中（现 238-240 行、249 行）主题相关代码替换为：

```cpp
    // 主题走 MainWindow 统一链路（联动编辑区/菜单勾选/持久化），不再直调 themeManager
    emit themeModeChanged(m_themeCombo->currentData().toString());
```

并删除原 `settings.setValue("appearance/theme", theme);`（249 行）——持久化由 onThemeSwitch 负责，避免双写。

- [ ] **Step 3: loadSettings 显示模式而非实际主题**

`SettingsDialog.cpp:220-221` 改为：

```cpp
    // 显示偏好模式（system 时下拉框选「跟随系统」，而非当前实际主题）
    QString mode = settings.value("appearance/theme", "light").toString();
    m_themeCombo->setCurrentIndex(m_themeCombo->findData(mode));
```

> 原实现读 `themeManager->currentTheme()`（实际主题）——system 模式下会错误显示「亮色/暗色」而非「跟随系统」。

- [ ] **4.4 MainWindow 接信号**（即本任务 Step 4）

`MainWindow::onShowSettings()`（`MainWindow.cpp:872-877`）改为（对话框改为栈上对象已如此，补信号连接）：

```cpp
void MainWindow::onShowSettings()
{
    SettingsDialog dialog(m_fileManager, this);
    connect(&dialog, &SettingsDialog::themeModeChanged,
            this, &MainWindow::onThemeSwitch);
    dialog.exec();
    applyEditorFont();
    applyEditorWidth();
    applyEditorSettings();
    applyCustomCss();
}
```

> 需确认 onShowSettings 现有函数体尾部是否还有 `applyCustomCss()` 等调用（876-877 行可见 applyEditorFont/applyEditorWidth，完整行以当前源码为准，只插入 connect 两行，其余不动）。

- [ ] **Step 5: 构建 + 测试 + 手动验证 + 提交**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: 全部 PASS

手动验证：
1. 设置→外观→主题选「跟随系统」→ 确定 → 界面、编辑区、菜单勾选全部同步
2. 选「暗色」→ 确定 → 编辑区同步变暗（缺口②修复确认）
3. 重新打开设置 → 显示「跟随系统」而非实际主题

```bash
git add src/ui/SettingsDialog.h src/ui/SettingsDialog.cpp src/ui/MainWindow.cpp
git commit -m "feat: 设置对话框支持跟随系统并统一主题应用链路"
```

4. 若 `git add` 报 SettingsDialog.h 无改动——检查 Step 2 信号声明是否遗漏。

---

### Task 5: 文档更新与收尾

**Files:**
- Modify: `CHANGELOG.md`
- Modify: `CLAUDE.md`（如架构描述需提及 SystemThemeWatcher）

**Interfaces:**
- Consumes: Task 1-4 全部完成
- Produces: 无

- [ ] **Step 1: CHANGELOG 补记**

`CHANGELOG.md` 顶部（`## [0.8.2]` 节，若无需先建节）追加：

```markdown
### 新增
- 主题「跟随系统」模式：经 xdg-desktop-portal 实时跟随系统深浅色
- 新增 SystemThemeWatcher 组件（`src/app/`），无 portal 环境自动降级亮色

### 修复
- 新建/恢复标签页编辑区不跟随当前主题（暗色下新 tab 是亮色）
- 设置对话框切换主题不联动 Web 编辑器
```

> 当前 0.8.2 节标题格式以文件现状为准（对齐现有 `## [0.8.1]` 格式）；不写发布日期（未发布）。

- [ ] **Step 2: CLAUDE.md 关键文件表补两行**

「关键文件」表格（现未列 ThemeManager——一并补上）：

```markdown
| `src/app/ThemeManager.cpp` | QSS 主题加载（light/dark 实际主题） |
| `src/app/SystemThemeWatcher.cpp` | 系统深浅色检测（D-Bus portal，跟随系统模式） |
```

- [ ] **Step 3: 全量验证**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: 全部 PASS；`git status` 干净（除 CHANGELOG/CLAUDE.md 改动）

- [ ] **Step 4: 提交**

```bash
git add CHANGELOG.md CLAUDE.md
git commit -m "docs: CHANGELOG 补记跟随系统主题功能"
```

---

## 自审记录

- **规格覆盖**：Watcher 组件（Task 1）、三入口与数据流 A/B/C（Task 2）、菜单（Task 3）、设置页+缺口②（Task 4）、HTML 导出零改动自然获得（currentTheme 返回实际主题，Task 2 不破坏）、错误处理矩阵（Task 1 全覆盖：服务不存在/读取失败/namespace 过滤/连接异常）、文档（Task 5）。无遗漏。
- **占位符扫描**：Task 4 Step 1 的「故意笔误」说明是执行指引的一部分，非占位符；Task 3 Step 3.5 的回填是跨任务接口的显式接线。计划内无 TBD/TODO。
- **类型一致性**：`switchTheme(QString)` 信号三值化、`setThemeChecked(QString)`、`themeModeChanged(QString)`、`currentThemeMode()` 在 Task 2/3/4 间签名一致；`handleSettingChanged(QString,QString,QVariant)` 与 DBus connect SLOT 宏匹配。
