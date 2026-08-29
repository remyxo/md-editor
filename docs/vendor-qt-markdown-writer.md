# vendor QtMarkdownWriter 说明与 LGPL 合规

## 背景

`QTextDocument::toMarkdown()` 的序列化由 Qt 内部 `QTextMarkdownWriter` 完成（**不是 md4c**——md4c 只负责 setMarkdown 解析侧）。该 writer 存在两个无法绕过的问题：

1. **80 列折行硬编码**：`writeBlock()` 内局部常量 `ColumnLimit = 80`，无公开 API 关闭；
2. **折行强切吞字**：稀疏空格段落（中文高发）触发二次折行窗口 `wi < i` 时，`i = j + 1` 无条件跳过断点字符——**数据丢失，HTML→MD 合并层不可恢复**（信息已丢）。

vendor 该 writer 后可从源头去除折行（后续独立提交），并使序列化行为在 Qt 6.8.2 → 6.10.2 及未来版本间完全固定（本项目版本无关性原则）。

## 来源与版本

| 项 | 值 |
|----|----|
| 上游文件 | `src/gui/text/qtextmarkdownwriter.cpp` / `qtextmarkdownwriter_p.h` |
| 来源树 | Debian 打包源码 `qt6-base-6.8.2+dfsg`（对应上游 tag v6.8.2） |
| vendored 位置 | `src/core/qt/QtMarkdownWriter.{h,cpp}` |
| 当前状态 | **行为保持版**：折行逻辑原样保留，未接入 MarkdownHandler |

## 相对原版的改动（共 5 项）

1. 类名 `QTextMarkdownWriter` → `QtMarkdownWriter`（避免与 Qt 内部符号冲突）
2. 删除 4 个私有头依赖（前三个全文零使用；`qtextmarkdownimporter_p.h` 仅用其 `FeatureFrontMatter` 枚举值，以 `.cpp` 内常量 `kFrontMatterFeature = 0x100000` 替换，值已与上游核对）
3. 日志类别 `qt.text.markdown.writer` → `mdeditor.markdownwriter`
4. 删除 `QT_BEGIN_NAMESPACE` / `QT_END_NAMESPACE` / `Q_GUI_EXPORT` 等 Qt 构建体系宏；私有头 include 改为等价公共头（`<QtGui/QTextList>` 等）
5. `.cpp` 顶部定义 `QT_USE_QSTRINGBUILDER`——上游文件在 Qt 自身构建体系下启用该宏编译，其 `QChar + QChar` 表达式依赖它（QStringBuilder 语义与普通 `+` 一致，无行为差异）

除上述外，功能主体与上游**逐字节一致**（验证方法见下）。

## 等价性验证

对 sed 施加上述机械改动后的上游原文与 vendored 文件做 `diff -u`：仅剩头注释、include 风格、常量定义三处计划内差异，**功能主体零差异**（初版转写曾引入两处漂移——`maybeEscapeFirstChar` 多加列位守卫、qCDebug 少一个参数——均被 diff 核验抓住并修正）。

运行时等价：探针对每个语料分别用 `doc.toMarkdown(features)` 与 `QtMarkdownWriter::writeAll()` 序列化同一 `QTextDocument`，逐字节对比。语料覆盖：多重折行长段、CJK 稀疏空格（含吞字路径）、超长无空格 token、标题 1-6、无序/有序/任务/松散/嵌套列表、列表内折行缩进、嵌套引用+前缀折行、围栏/波浪/缩进/引用内代码块、表格、行内格式组合、链接/图片 title（含多行转义）、front matter（`kFrontMatterFeature` 路径）、光标构造的富格式（等宽/字体属性切换/AnchorHref+ToolTip/ImageTitle）、以及 lead×tail 长度扫描（CJK/ASCII/引用 × 12×11 组合）。

**结果：437 pass / 0 fail（Qt 6.8.x 本地运行时）。**

附注：`MarkdownDialectGitHub` 枚举本身含 `0x100000` 位，即 front matter 在 GitHub 方言下默认开启——两侧行为一致，非 vendor 引入的差异。

## 上游同步维护

与新版 Qt 对照时，对上游文件施加同样 5 项机械改动后 diff（脚本思路：`sed` 替换类名/include/类别/常量 → `diff -u`），任何非机械差异即为上游行为变更点，逐项评估是否吸收。

## LGPL 合规

### 义务清单（分发时）

vendored 文件 SPDX 头为 `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`——多许可，我方**选择其一**适用：

- **保留版权与许可声明**：文件头 Copyright/SPDX 原样保留（已做）
- **标注修改**：修改处以注释标明（已做，改动清单见上）
- **随分发提供所选许可证文本**（LICENSE / THIRD_PARTY_NOTICES）
- 若选 **LGPL-3.0-only**：单二进制静态包含时须满足 LGPL §4（提供目标文件以支持重链接，或以"作品使用 LGPL 库"的声明 + 源码可得方式）；若选 **GPL-3.0-only**：仅需上述保留声明 + 许可证文本 + 标注修改，**无重链接义务**（对桌面单二进制应用最省事）

### 待决策：项目整体许可证

项目当前**无 LICENSE 文件**。通过 GitHub Releases 分发 DEB 即构成分发，需确定：

1. **GPL-3.0**（推荐）：项目自身代码与 vendored 文件统一 GPL-3.0，无组合许可摩擦、无重链接义务
2. **LGPL-3.0**：项目自身保持弱传染，vendored 文件按 LGPL-3.0 分发，须满足 §4 重链接支持
3. **自有许可（如 MIT）+ vendored 文件 GPL-3.0**：MIT 与 GPL 兼容可组合分发，项目文件保持 MIT；需两份许可证文本与清晰边界标注

> 注：应用本身动态链接系统 Qt（LGPL），现有义务已存在（保留声明、允许重链接——DEB 动态链接系统 Qt 天然满足）；vendor 源文件只是把**文件级**义务引入仓库。

## 后续步骤（待审后执行）

1. ✅ 步骤 1-2：vendor 行为保持版 + 等价性验证（本文件所述）
2. ✅ 步骤 3：`setWrapEnabled(bool)` 去折行开关（默认 true 保持等价；`false` 时 `writeFrame` 对全部块传 `wrap=false`，复用标题/代码块的既有不折行路径，逐块单行输出）。性质测试：强切吞字用例字节保真且幂等、长列表项/引用单行、折行伪影空格丢失用例修复
3. 步骤 4：MarkdownHandler 两处 `toMarkdown()` 调用点（`fromHtml` 主链路 ：4305、脚注内容链路 ：3551）切换到 vendored writer
4. 步骤 5：`fromHtml` 的折行合并补丁（`isWrapContinuation` 等）退役，独立提交
5. 步骤 6：全量回归（ctest + 真实 TipTap 端到端 + 大文档性能）
