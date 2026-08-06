# 开发笔记

## 阅读模式下链接点击的实现

### 需求

阅读模式（`setEditable(false)`）中，点击 Markdown 文档内的超链接应通过系统默认浏览器打开 URL。

### 失败的尝试

1. **CSS `pointer-events: auto`** — ProseMirror 在 `contentEditable=false` 时通过 JavaScript 事件处理器拦截了所有鼠标事件，CSS 无法覆盖。

2. **DOM0 级 `onclick`** — 在链接元素上直接设置 `onclick` 属性，ProseMirror 的 `mousedown` 处理器在事件到达前已阻止默认行为。

3. **`addEventListener('click', ..., true)` 捕获阶段** — 即使使用捕获阶段，ProseMirror 的事件处理机制仍然阻止了链接导航。

4. **QWebEngine `acceptNavigationRequest`** — `QWebEnginePage::acceptNavigationRequest` 是虚函数，可以被重写，但 ProseMirror 拦截了链接点击后根本不产生导航请求（`NavigationTypeLinkClicked`），该函数收不到任何通知。

5. **静态 HTML 渲染（`innerHTML` 到一个独立 `<div>`）** — 此方案确实有效（链接变为原生 HTML，可点击），但 `LocalContentCanAccessRemoteUrls = false`（QWebEngine 的安全设置）阻止了从本地页面到远程 URL 的导航。

### 最终方案

**三层协作：**

```
阅读模式链接点击
  → JS: mousedown 捕获阶段 (capture=true) 检查点击目标是否为 <a>
    → e.preventDefault() + e.stopPropagation()
    → EditorBridge.openUrl(url)  // QWebChannel → C++
      → QDesktopServices::openUrl(url)  // 绕过 QWebEngine 导航限制
```

**关键点：**
1. **`mousedown` 而非 `click`** — ProseMirror 的链接拦截发生在 `mousedown` 级别，所以必须在 `mousedown` 阶段用捕获模式（`capture=true`）抢先处理。
2. **`QWebChannel → C++ → QDesktopServices`** — 不通过 QWebEngine 的导航系统打开 URL，直接用系统级 API，彻底绕过 `LocalContentCanAccessRemoteUrls` 等安全限制。
3. **`setEditable(false)` 而非静态 HTML** — 保留 ProseMirror 的原生渲染效果，阅读模式和编辑模式外观一致。

### 教训

- QWebEngine 的链接导航受多重安全策略限制（`LocalContentCanAccessRemoteUrls`、`acceptNavigationRequest`），在富文本编辑器场景下不可靠。
- ProseMirror 的事件拦截很深，CSS 和常规 DOM 事件无法突破。`mousedown` + `capture=true` 是最早的拦截点。
- 当浏览器/WebEngine 层面解决不了时，走 C++ 桥接 + 系统 API 是可靠的兜底方案。

---

## 工具栏图标的主题切换

### 问题

亮色/暗色主题切换时，工具栏 SVG 图标颜色不跟随变化。

### 失败的尝试

1. **SVG `stroke="currentColor"` + QSS `color`** — Qt6 的 QToolButton 渲染 SVG 图标时，`currentColor` 从 `QPalette::ButtonText` 获取，而非从 QSS 的 `color` 属性。QSS 无法控制 SVG 图标颜色。

2. **QPalette 设置** — 在 `qApp->setStyleSheet()` 之后设置 palette 会被 QSS 覆盖，先设置后加载 QSS 又会丢失。

### 最终方案

为亮色/暗色主题分别准备硬编码颜色的 SVG 文件（`icons/light/` 和 `icons/dark/`），主题切换时通过 `QAction::setIcon()` 替换整个图标文件。

**图标颜色不依赖 `currentColor`、QSS、QPalette 三者中的任何一个。**

### 教训

Qt 的 QSS、QPalette、SVG `currentColor` 三者之间的优先级关系复杂且不可靠。对于需要精确控制颜色的 UI 元素，硬编码是最稳妥的方案。
