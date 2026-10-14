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
| 当前状态 | **已接入**：`MarkdownHandler::fromHtml` 两处序列化点均走 vendored writer 且 `setWrapEnabled(false)`，80 列折行已从源头移除 |

## 相对原版的改动（共 7 项）

1. 类名 `QTextMarkdownWriter` → `QtMarkdownWriter`（避免与 Qt 内部符号冲突）
2. 删除 4 个私有头依赖（前三个全文零使用；`qtextmarkdownimporter_p.h` 仅用其 `FeatureFrontMatter` 枚举值，以 `.cpp` 内常量 `kFrontMatterFeature = 0x100000` 替换，值已与上游核对）
3. 日志类别 `qt.text.markdown.writer` → `mdeditor.markdownwriter`，且默认级别降为 `QtWarningMsg`——Qt 内置规则默认关闭 `qt.*` 类别的 debug（原类别因此静默），改名脱离该规则后若不降级，`qCDebug` 会每次序列化逐块刷日志（journald/终端逐条写均阻塞 GUI 线程）。排查时用 `QT_LOGGING_RULES="mdeditor.markdownwriter.debug=true"` 显式打开
4. 删除 `QT_BEGIN_NAMESPACE` / `QT_END_NAMESPACE` / `Q_GUI_EXPORT` 等 Qt 构建体系宏；私有头 include 改为等价公共头（`<QtGui/QTextList>` 等）
5. `.cpp` 顶部定义 `QT_USE_QSTRINGBUILDER`——上游文件在 Qt 自身构建体系下启用该宏编译，其 `QChar + QChar` 表达式依赖它（QStringBuilder 语义与普通 `+` 一致，无行为差异）
6. **`setWrapEnabled(bool)`**：`writeFrame` 传给 `writeBlock` 的 wrap 参数追加 `m_wrapEnabled` 与运算（默认 `true`，与上游完全等价——等价性测试即回归防线；`false` 时全部块单行输出）
7. **删除自动链接 `<url>` 输出分支（2026-10-14）**：上游在「链接文本==地址且有 scheme」时输出 `<url>` 尖括号自动链接，vendored 一律输出显式 `[text](href)`。原因：本项目 toHtml 管线按字面 HTML 保护 `<…>`（不转链接），`<url>` 形式再载入即丢链接性（gfm 语料 hr-links 族两趟发散的根源），且编辑器 TipTap link mark 只存 href+文本、经 HTML 后两种写法不可区分——显式形式是项目链接规范，用户所写 `[url](url)` 往返不得退化。与上游的**有意分歧**由 `test_qtmarkdownwriter` 的 `testIntentionalDivergenceAutolink` 固化（其余语料等价性不受影响：`testEquivalenceInline` 探针已将自动链接语料移出等价面，分歧范围单独锁定）

除上述外，功能主体与上游**逐字节一致**（验证方法见下）。

## 等价性验证

对 sed 施加上述机械改动后的上游原文与 vendored 文件做 `diff -u`：仅剩头注释、include 风格、常量定义三处计划内差异，**功能主体零差异**（初版转写曾引入两处漂移——`maybeEscapeFirstChar` 多加列位守卫、qCDebug 少一个参数——均被 diff 核验抓住并修正）。

运行时等价：探针对每个语料分别用 `doc.toMarkdown(features)` 与 `QtMarkdownWriter::writeAll()` 序列化同一 `QTextDocument`，逐字节对比。语料覆盖：多重折行长段、CJK 稀疏空格（含吞字路径）、超长无空格 token、标题 1-6、无序/有序/任务/松散/嵌套列表、列表内折行缩进、嵌套引用+前缀折行、围栏/波浪/缩进/引用内代码块、表格、行内格式组合、链接/图片 title（含多行转义）、front matter（`kFrontMatterFeature` 路径）、光标构造的富格式（等宽/字体属性切换/AnchorHref+ToolTip/ImageTitle）、以及 lead×tail 长度扫描（CJK/ASCII/引用 × 12×11 组合）。

**结果：437 pass / 0 fail（Qt 6.8.x 本地运行时）。**

附注：`MarkdownDialectGitHub` 枚举本身含 `0x100000` 位，即 front matter 在 GitHub 方言下默认开启——两侧行为一致，非 vendor 引入的差异。

## 上游同步维护

与新版 Qt 对照时，对上游文件施加同样 6 项机械改动后 diff（脚本思路：`sed` 替换类名/include/类别/常量 → `diff -u`），任何非机械差异即为上游行为变更点，逐项评估是否吸收（改动 7 为有意功能分歧，diff 中表现为链接序列化一处计划内差异，不吸收）。等价性测试以 `setWrapEnabled` 默认值 `true` 运行，即上游行为回归防线（改动 7 的分歧范围由 `testIntentionalDivergenceAutolink` 单独锁定）。

## LGPL 合规

### 义务清单（分发时）

vendored 文件 SPDX 头为 `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`——多许可，我方**选择其一**适用：

- **保留版权与许可声明**：文件头 Copyright/SPDX 原样保留（已做）
- **标注修改**：修改处以注释标明（已做，改动清单见上）
- **随分发提供所选许可证文本**（LICENSE / THIRD_PARTY_NOTICES）
- 若选 **LGPL-3.0-only**：单二进制静态包含时须满足 LGPL §4（提供目标文件以支持重链接，或以"作品使用 LGPL 库"的声明 + 源码可得方式）；若选 **GPL-3.0-only**：仅需上述保留声明 + 许可证文本 + 标注修改，**无重链接义务**（对桌面单二进制应用最省事）

### 已决策：项目整体采用 GPL-3.0（2026-08-29）

项目原无 LICENSE 文件。经评估三选项（GPL-3.0 / LGPL-3.0 / MIT+GPL 边界标注），已选定 **GPL-3.0**：项目自身代码与 vendored 文件统一许可，无组合摩擦、无 §4 重链接义务，仓库根已添加 `LICENSE`（GPL-3.0 全文）。义务履行：文件头 Copyright/SPDX 原样保留、修改处注释标明（改动清单见上）、许可证文本随分发提供。

> 注：应用本身动态链接系统 Qt（LGPL），既有义务不变（保留声明、允许重链接——DEB 动态链接系统 Qt 天然满足）；vendor 源文件只是把**文件级**义务引入仓库。

## 后续步骤（已全部完成）

1. ✅ 步骤 1-2：vendor 行为保持版 + 等价性验证（本文件所述）
2. ✅ 步骤 3：`setWrapEnabled(bool)` 去折行开关（默认 true 保持等价；`false` 时 `writeFrame` 对全部块传 `wrap=false`，复用标题/代码块的既有不折行路径，逐块单行输出）。性质测试：强切吞字用例字节保真且幂等、长列表项/引用单行、折行伪影空格丢失用例修复
3. ✅ 步骤 4：MarkdownHandler 两处 `toMarkdown()` 调用点（`fromHtml` 主链路、脚注内容链路）切换到 vendored writer 的 `serializeMarkdownNoWrap()` 辅助函数
4. ✅ 步骤 5：`fromHtml` 的折行合并补丁（`isWrapContinuation` 等）退役——安全前提已验证：所有 `<br>` 变体（含 data-brk）在 setHtml 前均已被 ZZBK 占位符保护，toMarkdown 输出中的段内 `\n` 只可能是折行伪影；折行既除，合并循环为纯死代码
5. ✅ 步骤 6：全量回归——ctest 12/12 通过且**零期望值修改**（合并层输出本就等价于单行输出）；新增 `testNoFoldByteStableRoundTrip` 回归（吞字/strong 闭合空格/长列表/长引用/管道字符用例首轮字节保真）；178KB 大文档完整往返 158ms

## 遗留的良性漂移（与折行无关，收敛型）

去折行后仍有以下首轮规范化，均为 setHtml/setMarkdown 解析侧行为，二次往返起稳定：

- `2.` 开头的段落首字符转义为 `2\.`
- `==高亮==` 部分形态规范化为 `\=\=` 分支形态
- `>` 引用符在特定上下文输出为 `\>`
- 段落末尾单个尾随空格被 setHtml 裁剪

## 已知上游缺陷（2026-09-13 全面代码审查发现，保持上游行为不改码）

vendoring 原则零本地补丁，以下问题记录备查；触发条件均极边缘，本项目当前用法（wrap 恒 false）下无实际影响：

1. **`adjacentBackticksCount` 空串越界**（上游 `QtMarkdownWriter.cpp:309`）：`s.at(len - 1)` 在 `len==0` 时为 `at(-1)`（release 下 UB，debug 断言）。仅当出现「空文本 mono fragment」时可达，罕见。
2. **`escapeSpecialCharacters` 逐次 insert 的 O(K·n) 尾部搬移**（上游 `:345-362`）：超长行 + 大量 `<`/`*` 前缀字符时平方级。与 wrap 无关、始终执行，但本项目文档规模未见感知。
3. **wrap 分支在本项目恒不执行**（`writeFrame` 的 `!table && m_wrapEnabled`）：折行分支（含历史吞字点 `i = j + 1`）与 `nearestWordWrapIndex` 为死代码。若将来有人改回 `wrap=true`，先重读「吞字」历史再动。
