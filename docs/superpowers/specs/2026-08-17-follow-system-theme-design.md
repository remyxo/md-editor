# 「跟随系统」主题设计文档

- 日期：2026-08-17
- 状态：已确认（设计分节评审通过）
- 关联：`docs/superpowers/specs/2026-08-02-md-editor-design.md`（原始规划中已列「跟随系统」选项）

## 背景与目标

md-editor 目前提供亮色/暗色两种主题（`ThemeManager` 加载 QSS + Web 编辑器 `body.dark` 类联动）。本设计新增第三种模式「跟随系统」：应用主题跟随操作系统深浅色偏好，系统切换时**实时**跟随，无需重启。

同时修复调查中确认的两处既有缺口：

1. **新 tab 不推送主题**：暗色模式下新建/恢复的 tab 编辑区仍是亮色（`MainWindow.cpp` 的 `pageLoadFinished` 不含 theme 推送）
2. **设置对话框不联动 Web 编辑器**：设置页切主题只换 QSS/图标，编辑区不变（`SettingsDialog::onAccepted` 只调 `themeManager->switchTheme`）

## 核心概念：偏好模式 vs 实际主题

| 概念 | 取值 | 存储位置 |
|------|------|---------|
| 偏好模式（用户选择） | `system` / `light` / `dark` | QSettings `appearance/theme` |
| 实际主题（当前生效） | `light` / `dark` | ThemeManager 运行时状态 |

`ThemeManager` 只认实际主题名，保持纯 QSS 加载器职责不变。「system → 实际主题」的解析由 `MainWindow` 在调用前完成（方案讨论中已定：不把解析逻辑塞进 ThemeManager）。

## 架构

```
                    ┌─ 菜单「查看→主题」(三选一 QActionGroup)
用户入口 ────────────┤
                    └─ 设置对话框外观页（下拉框加第三项）

                          appearance/theme = "system"|"light"|"dark"
                                │
                 MainWindow::onThemeSwitch(mode)
                                │
              ┌─────────────────┼──────────────────────┐
              ▼                 ▼                      ▼
     ThemeManager        SystemThemeWatcher(新)    所有 tab
     switchTheme(        读 portal color-scheme   setEditorTheme()
       实际主题名)        + 订阅变化信号           (含新 tab 补推)
```

系统主题变化时，system 模式下重新走一遍现有 `onThemeSwitch` 应用链路（等效用户手动切换），复用全部现有切换逻辑，不另造第二条应用路径。

## 组件设计

### ① SystemThemeWatcher（新增，`src/app/`，与 ThemeManager 同层）

| 职责 | 说明 |
|------|------|
| 检测 | 启动时经 portal `ReadOne("org.freedesktop.appearance", "color-scheme")` 读一次：返回 `1` → dark，`2` → light |
| 监听 | 连接 portal `SettingChanged` 信号（namespace = `org.freedesktop.appearance`、key = `color-scheme`）实时感知 |
| 信号 | `systemThemeChanged(QString actualTheme)` — 只在值**变化**时发射 |
| 生命周期 | `MainWindow` 持有，随主窗口创建 |
| 降级 | 无 portal 环境（DBus 连接失败/接口缺失）静默降级为恒 light，不报错不崩溃 |

**可测性设计**：DBus 接口层构造注入——生产环境注入真实 portal 接口，单测注入 mock。Watcher 核心只做「DBus 返回值 → 主题名」翻译，翻译逻辑独立单测。

**技术选型依据**：`QStyleHints::colorScheme()` 是 Qt 6.5 才引入的 API，兼容基准 Qt 6.4.2 上不可用；D-Bus portal（`org.freedesktop.portal.Desktop`）在 GNOME/KDE 均标配（xdg-desktop-portal），6.4.2 与 6.8.x 行为一致，`Qt6::DBus` 在 `qt6-base-dev` 内无新增系统依赖。

### ② ThemeManager（不改核心）

`switchTheme` 仍只接受 `light`/`dark`。`currentTheme()` 返回实际主题（HTML 导出已用它，system 模式下自动跟对，无需特殊处理）。

### ③ 入口改造

- **菜单**（`MenuBar`）：主题组 2 项变 3 项（跟随系统/亮色/暗色），QActionGroup 互斥勾选，新增 `switchTheme(QString)` 复用现有信号
- **设置对话框**（`SettingsDialog`）：下拉框加「跟随系统」；`onAccepted` 不再自己调 `themeManager`，统一走 `MainWindow::onThemeSwitch`（顺带修复缺口②）

### ④ 既有缺口修复（缺口①）

`MainWindow` 在 `pageLoadFinished` 处补推 `setEditorTheme(当前实际主题)`，新 tab 加载完成即同步主题。

## 数据流

**路径 A — 用户切到某模式**（菜单或设置页）：
```
UI 选择 mode
→ MainWindow::onThemeSwitch(mode)
   → 实际主题 = (mode == "system") ? watcher->currentSystemTheme() : mode
   → ThemeManager.switchTheme(实际主题)
   → QSettings 写 mode
   → 所有 tab setEditorTheme(实际主题)
```

**路径 B — 系统主题变化**（仅 system 模式激活）：
```
portal SettingChanged
→ SystemThemeWatcher::systemThemeChanged(actualTheme)
→ MainWindow（若当前模式 == "system"）
   → 同路径 A 的应用链路，但不写 QSettings（模式未变）
```

**路径 C — 新 tab 加载完成**：
```
pageLoadFinished → MainWindow 补推 setEditorTheme(当前实际主题)
```

## 错误处理

| 场景 | 行为 |
|------|------|
| portal 服务不存在（少数 WM 环境） | 启动探测失败 → watcher 静默不激活，恒返回 light；「跟随系统」菜单项仍可选，行为等同亮色 |
| portal 读 color-scheme 单次调用失败 | 保留上次值，`qWarning` 日志，不崩溃 |
| SettingChanged 的 namespace/key 不匹配 | 过滤，不响应 |
| DBus 连接异常 | 同「服务不存在」：降级恒 light |

原则：**降级而非报错**。无 portal 环境通常是纯 WM/极简环境，弹窗报错无意义；light 与现有默认一致，行为可预期。

## 测试策略

| 层 | 测试 |
|----|------|
| 单元测试（ctest） | ① Watcher 翻译逻辑：`1`→dark、`2`→light、`0`/无值/异常→light ② mock 信号 → `systemThemeChanged` 只在值变化时发射 ③ system 模式解析：mode 与实际主题映射正确 |
| 手动验证清单 | ① GNOME 切深浅色 → QSS/图标/Web 编辑区三同步实时跟随 ② 无 portal 环境行为验证 ③ system 模式下新开 tab 主题正确 ④ 设置页切主题 Web 编辑区联动 ⑤ 菜单与设置页勾选状态互相同步 |

## 范围

**范围内**：SystemThemeWatcher 新组件、菜单与设置页第三选项、实时跟随、两处既有缺口修复、HTML 导出自动跟实际主题（零改动自然获得）。

**范围外**：PDF/DOCX 导出主题化（维持现状：PDF 硬编码亮色、DOCX 离屏 mermaid 恒亮）、界面英文翻译接入、其他设置项。

## 版本

按项目版本推进规则，属新增功能（新组件 + 新选项 + 缺口修复），建议 `+0.1.0`。当前处于 0.8.2 批量修 BUG 攒批阶段，本次不打 tag，随批推进。
