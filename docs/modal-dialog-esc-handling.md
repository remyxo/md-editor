# 模态编辑面板 ESC 关闭机制

## 问题概述

Mermaid 编辑对话框和数学公式编辑对话框中，按 ESC 键无法关闭对话框。

---

## 事件传播链路分析

```
硬件按键 → Linux kernel → Qt 事件循环
                              ↓
                      QShortcut (MainWindow)  ← 在此处被截获！
                              ↓
                      activated 信号触发
                              ↓
                      隐藏搜索栏（原有逻辑）
                              ↓
                      [事件已被消费，不再向下传递]
                              
QWebEngineView            → 永远收不到 Escape
Chromium / Blink          → 永远收不到 Escape  
JavaScript keydown 事件   → 永远收不到 Escape
```

### 根因

`MainWindow.cpp:106` 注册了全局 Escape 快捷键：

```cpp
QShortcut *escShortcut = new QShortcut(QKeySequence("Esc"), this);
connect(escShortcut, &QShortcut::activated, this, [this]() {
    m_searchBar->setVisible(false);  // 隐藏搜索栏
});
```

Qt 的 `QShortcut` 机制在 `MainWindow`（顶层窗口）级别截获 Escape 键。`QShortcut::activated` 触发后，事件被 Qt 消费，**不再向下传递** 给子控件 `QWebEngineView`，继而 Chromium/Blink 和 JavaScript 层都永远收不到该按键。

### 失败的尝试（全在 JS 层，事件根本到不了）

1. **`<textarea>` 上绑定 `keydown` 监听 Escape** — 无效，按键事件被 Qt 层截获。

2. **弹出层面板 `<div>` 上绑定 `keydown` 监听 Escape**（兜底） — 无效，同上。

3. **`document.addEventListener('keydown', handler, true)` capture phase** — 无效，`document` 在 QWebEngineView 内部的 DOM 树中，事件从未到达。

**关键认知**：当 Qt 的 `QShortcut` 消耗了按键事件后，QWebEngineView 内部的一切 JavaScript 事件处理都无从触发。JS 层的任何修复都是白费。

---

## 最终方案：C++ Bridge 通知 + Esc 分流

### 架构

```
JS 层                       C++ Bridge                C++ MainWindow
──────────────────────────────────────────────────────────────────
对话框打开                   
  ↓                          
bridge.modalDialogChanged(true) ──→ emit signal ──→ m_modalDialogOpen = true
                                                      
用户按 Escape                                       
  ↓                                                 
QShortcut::activated                                
  ↓                                                 
检查 m_modalDialogOpen                              
  ├─ true  → runJavaScript 关闭 JS 对话框            
  └─ false → 隐藏搜索栏（原有逻辑）                    
                                                      
对话框关闭                   
  ↓                          
bridge.modalDialogChanged(false) ──→ emit signal ──→ m_modalDialogOpen = false
```

### 修改涉及的文件

| 文件 | 修改内容 |
|------|---------|
| `src/core/EditorBridge.h` | 新增 slot `modalDialogChanged(bool)` + signal `modalDialogStateChanged(bool, int tabId)` |
| `src/core/EditorBridge.cpp` | slot 实现：接收 JS 调用，emit signal |
| `src/ui/MainWindow.h` | 新增 `bool m_modalDialogOpen = false` 成员 |
| `src/ui/MainWindow.cpp` | connect signal → 更新 flag；修改 Esc shortcut 按 flag 分流 |
| `src/resources/web/editor.js` | `_mathEditor` 和 `_mermaidEditor` 的 `open()`/`create()`/`close()` 中调用 `bridge.modalDialogChanged(...)` |

### MainWindow.cpp Esc 分流逻辑

```cpp
QShortcut *escShortcut = new QShortcut(QKeySequence("Esc"), this);
connect(escShortcut, &QShortcut::activated, this, [this]() {
    if (m_modalDialogOpen) {
        // 优先：关闭 JS 侧的模态编辑面板
        runEditorCommand("window.editorApi._mermaidEditor.close();"
                         "window.editorApi._mathEditor.close();");
    } else {
        // 原有逻辑：隐藏搜索栏
        m_searchBar->setVisible(false);
    }
});
```

### editor.js 通知点（4 处 × 2 个编辑器 = 8 处）

**Mermaid 编辑器：**
- `_mermaidEditor.open()` — `bridge.modalDialogChanged(true)`
- `_mermaidEditor.create()` — `bridge.modalDialogChanged(true)`
- `_mermaidEditor.close()` — `bridge.modalDialogChanged(false)`（第一时间，不等 setTimeout）

**数学公式编辑器：**
- `_mathEditor.open()` — `bridge.modalDialogChanged(true)`
- `_mathEditor.create()` — `bridge.modalDialogChanged(true)`
- `_mathEditor.close()` — `bridge.modalDialogChanged(false)`（第一时间）

### modalDialogChanged(false) 的时序

必须在 `close()` 开头立即调用，**不能**放在 `setTimeout` 内部（`close()` 内部有 ~200ms 的延迟恢复逻辑）。否则用户在对话框关闭动画期间再按 ESC，`m_modalDialogOpen` 仍然是 `true`，会尝试再次关闭已销毁的对话框，引发错误。

---

## 模态对话框的行为要求

| 关闭方式 | 是否允许 | 机制 |
|----------|---------|------|
| ESC 键 | ✅ | Qt `QShortcut` → bridge 通知 → JS `close()` |
| × 关闭按钮 | ✅ | 标题栏关闭按钮 → JS `close()` |
| 取消按钮 | ✅ | 底部取消按钮 → JS `close()` |
| 确定按钮 | ✅ | 底部确定按钮 → JS `confirm()` → `close()` |
| 点击遮罩（overlay） | ❌ | 遮罩仅做视觉遮挡 + 滚轮拦截，**不响应点击** |
| 点击对话框外部 | ❌ | 遮罩阻止与底层编辑区交互 |

**遮罩（overlay）的正确行为：**
- 覆盖整个视口（`position: fixed; inset: 0`），阻止与底层编辑区的鼠标交互
- 拦截滚轮事件，防止滚动穿透到下层编辑区
- **不要**添加 click 事件关闭对话框——这会破坏模态对话框的用户预期

---

## 教训

### 1. Qt QShortcut 会拦截子控件的事件

`QShortcut(parent)` 在 `parent` 的上下文中注册全局快捷键。当快捷键匹配时，事件被 Qt 消费，**不会传递给任何子控件**。对于 `QWebEngineView` 这种内嵌浏览器，JS 事件处理完全无效。

### 2. 调试 Qt + WebEngine 组合问题时的检查顺序

1. 先确认按键是否到达 QWebEngineView（检查 Qt 层 `QShortcut` / `eventFilter` / `keyPressEvent`）
2. 再确认是否到达 Chromium（检查 `QWebEnginePage::event` 或 JS `console.log`）
3. 最后才排查 JS 层问题

**不要先入为主地在 JS 层调试按键问题。** 在 Qt+WebEngine 桌面应用中，顶层窗口的快捷键优先级最高。

### 3. 新增快捷键时需检查冲突

将来在 `MainWindow` 或任何 QWebEngineView 的父控件中新增 `QShortcut` 时，必须检查是否与 QWebEngineView 的预期按键冲突。修改现有快捷键前，应确认是否会影响 JS 层的对话框或编辑器功能。
