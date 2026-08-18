# Qt 转义兼容（MarkdownHandler 设计基准，6.4.2 → 6.10.2）

> **核心原则：用占位符保护实现版本无关转义。** 新增任何依赖 Qt `toMarkdown()`/`setHtml()` 行为的逻辑，必须以占位符保护为范式，覆盖 **6.4.2 → 6.8.x → 6.10.2** 全范围，**不得依赖单一版本的转义行为**（6.4.2 缺转义、6.8.2/6.10.2 更激进转义，差异见下文）。

修改 `MarkdownHandler`（尤其 `fromHtml` / `toHtml` 的转义相关逻辑）前**务必先读本文件**。

## 背景：版本差异

| 环境 | Qt 版本 | 来源 |
|------|---------|------|
| 开发机 | **6.8.2** | Debian 13 系统包 |
| CI / 发布 DEB | **6.10.2** | `ubuntu-26.04` apt 包 |
| 最低运行时依赖 | **6.4.2** | DEB 依赖 `>= 6.4`（`ubuntu-24.04` 用户仍存在） |

**发布到 GitHub Release 的 DEB 在 CI 上用 Qt 6.10.2 构建**，这是「出厂构建/测试基准」。但 DEB 运行时依赖 `>= 6.4`，6.4.2 用户仍是有效运行时——因此转义逻辑必须版本无关（占位符保护），6.4.2 / 6.8.2 / 6.10.2 任一版本都要通过同一套测试。本地 6.8.2 通过不等于出厂正确。

## 根因：toMarkdown（md4c）转义差异

`QTextDocument::toMarkdown()` 底层是 md4c。6.4.2 与 6.8.2 的转义激进程度显著不同（6.8.2 更激进；6.10.2 与 6.8.2 一致）。观测证据（同一 HTML 输入）：

| 输入 HTML | Qt 6.4.2 输出（缺转义） | Qt 6.8.2 输出（有转义） |
|-----------|------------------------|------------------------|
| `<h2>*A* 包裹</h2>` | `## *A* 包裹` | `## \*A* 包裹` |
| `<p>1. 这是正文</p>` | `1. 这是正文` | `1\. 这是正文` |
| `# <div...>`（前导文本） | `# ...`（不转义 `#`） | `\# ...`（转义前导 `#`） |

`MarkdownHandler::fromHtml` 曾把转义**完全交给 Qt**、自身只在标题行首**剥离**多余转义（从不补齐）。于是在 6.4.2 上出现**往返保真缺口**：

- 标题字面 `*A*` → 重载时被解析为**强调**
- 正文 `1. xxx` → 重载时被解析为**有序列表**

CI 之所以长期没暴露，是因为 workflow 在某个提交（`557ecb4`）之前**根本没跑 ctest**，测试只在本地 6.8.2 验证过。

## 解决模式：占位符保护（版本无关转义）

**核心思路：不要事后依赖 Qt 的转义，而在 `setHtml` 之前用占位符把字面特殊字符保护起来，`toMarkdown` 之后再还原为转义形式。** 占位符是字母数字（如 `ZZST0ZZ`），无任何 Markdown 语法形态，Qt 原样放行，因此转义结果与 Qt 版本无关。

```
setHtml 前：把字面 * / 行首列表标记 等替换为 ZZ* 占位符
   ↓ QTextDocument::setHtml + toMarkdown（占位符原样穿过）
逆序还原：占位符 → 转义形式（\* / 1\. / \- …）
```

这与既有 `**` / `==` / `&lt;` / `&gt;` 保护（step D）同构，是本文件的标准范式。

### 三处实例（`src/core/MarkdownHandler.cpp` fromHtml）

1. **字面单 `*`（step D）**：状态机识别 HTML 标签外的孤立 `*`（非 `**`）→ 占位符 → 还原 `\*`。
   - 关键：强调语法 `*` 来自 `<em>` 标签、在 `setHtml` **之后**由 Qt 生成；本步在 `setHtml` **之前**运行，故只命中真字面星号，不影响 `<em>→*`。
2. **`<p>` 行首列表标记（step F）**：`setHtml` 前匹配 `(<p\b[^>]*>)\s*(\d{1,9}[.)]|[+\-])(?=\s)` → 占位符 → 还原 `1\.` / `1\)` / `\-` / `\+`。
   - 真正的列表是 `<ol>/<ul>/<li>`，由 Qt 正常转换，不受影响；事后无法区分 `<p>` 字面标记与真列表，**必须**在 `setHtml` 前处理。
3. **div 标题降级恢复（step 5 mode a）**：按 `font-size`（xx-large→`#`、x-large→`##`、large→`###`）重新定级前缀，而非保留可能失配的旧 `#` 前缀。

## 6.10.2 验证结论（2026-08-18）

迁移至 Ubuntu 26.04 + Qt 6.10.2 时，用官方 Qt 6.10.2（`linux_gcc_64` qtbase）全量复跑非 WebEngine 测试：**164 项全通过**（`test_markdownhandler` 106 项 + 其余 6 个测试 58 项）。结论：占位符保护 + 正则后处理在 6.10.2 上行为与 6.4.2 / 6.8.2 一致，往返转换无变化。

唯一待验证项：QWebEngine/Chromium 从 102（6.4.2）→ 134（6.10.2），Mermaid/KaTeX 栅格化、PDF/DOCX 导出与 TipTap 编辑行为属 GUI 行为，需真机手测。

## 验证方法：容器复现

本地 6.8.2 通过**不够**，必须在目标版本上验证。用 Docker 复现 CI 环境（Docker Hub 直连常超时，用国内镜像）；出厂基准用 `ubuntu:26.04`（Qt 6.10.2），最低运行时用 `ubuntu:24.04`（Qt 6.4.2）：

```bash
# 拉取（镜像源按网络情况替换）
docker pull docker.m.daocloud.io/library/ubuntu:26.04
docker tag   docker.m.daocloud.io/library/ubuntu:26.04 ubuntu:26.04

# 启动并装依赖（精简镜像缺 ca-certificates，apt 须走 HTTP 镜像）
docker run --name ci-repro -v "$(pwd):/work" -w /work -it ubuntu:26.04
# 容器内：
sed -i 's#http://archive.ubuntu.com/ubuntu#http://mirrors.tuna.tsinghua.edu.cn/ubuntu#; s#http://security.ubuntu.com/ubuntu#http://mirrors.tuna.tsinghua.edu.cn/ubuntu#' /etc/apt/sources.list.d/ubuntu.sources
apt update && apt install -y ca-certificates qt6-base-dev qt6-webengine-dev qt6-webchannel-dev g++ pkg-config

# 只编译/运行 MarkdownHandler 测试（轻量，无需 webengine 运行时）
MOC=/usr/lib/qt6/libexec/moc
$MOC /work/tests/test_markdownhandler.cpp -o /tmp/test_markdownhandler.moc
g++ -std=c++17 -fPIC /work/tests/test_markdownhandler.cpp /work/src/core/MarkdownHandler.cpp \
  $(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6Test) -I /work/src/core -I /tmp -o /tmp/t
QT_QPA_PLATFORM=offscreen /tmp/t
```

> `QTextDocument` 需 `QGuiApplication`（offscreen 即可）；`MarkdownHandler` 无 `Q_OBJECT`，探针不需 moc，但测试文件含 `Q_OBJECT` 需 moc。

## 检查清单（新增转义/格式逻辑时）

- [ ] 该逻辑是否依赖 Qt `toMarkdown`/`setHtml` 的某种「自动」行为？若是，确认 6.4.2 也有该行为——**不要假设**，用容器观测。
- [ ] 能否事后（`toMarkdown` 之后）区分两种来源（如真列表 vs 字面标记）？若不能，**必须在 `setHtml` 前用占位符保护**。
- [ ] 占位符是否为纯字母数字、无 Markdown 语法形态？还原是否在逆序还原阶段（`for (i = size-1; i >= 0)`）？
- [ ] 本地 6.8.2 通过后，是否在 6.4.2（最低运行时）与 6.10.2（出厂基准）容器复跑同一测试？
