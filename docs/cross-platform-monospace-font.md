# 跨平台等宽字体与行内代码往返（Cross-Platform FixedFont）

**症状**：Windows/macOS 上打开含行内代码（`` `x` ``）的文档，切换到源码模式后反引号丢失、内容裸露甚至被转义（`` `**A**` `` 变成 `\*\*A\*\*`）。Linux 上一切正常——**Linux 测试通过不等于跨平台正确**。

## 根因链

Qt 在三个环节用 `QFontDatabase::systemFont(QFontDatabase::FixedFont)` 作为代码字体，其族名**平台相关**：

| 平台 | FixedFont 族名 |
|------|---------------|
| Linux (fontconfig) | `"monospace"`（别名原样保留） |
| Windows | `"Courier New"` |
| macOS | `"Menlo"` |

完整链路（0.11.0 及之前在 Windows/macOS 上断裂于第 4 步）：

1. **Markdown 导入**（`qtextmarkdownimporter.cpp`）：`` `x` `` → code span，字体设为 `systemFont(FixedFont)`
2. **HTML 导出**（`QTextDocument::toHtml`）：code span → `<span style=" font-family:'Courier New';">x</span>`（字体名按平台不同）
3. **`MarkdownHandler::toHtml` 后处理**：把 monospace span 转为语义 `<code>` 标签——旧代码正则硬编码只认 `'monospace'`，**Windows/macOS 的字体名不匹配** → span 直通
4. **TipTap 加载**：只认 `<code>` 标签，不认 span → **code mark 在文件加载那一刻就丢失**（切源码模式只是显形）
5. **切源码**：`getContent()` 无 `<code>` → `fromHtml` 的 ZZCD 占位符保护不触发 → 内容裸露走 `escapeSpecialCharacters`（`\*\*A\*\*` 转义的来源）

`fromHtml` 侧的 `<span monospace>` 兜底正则同样硬编码，是同一问题的对偶。

## 修复（0.11.1）

`MarkdownHandler.cpp` 的 `monoSpanFamilyPattern()`（toHtml 后处理与 fromHtml 兜底共用）：

- 硬编码纳入三大平台已知名：`monospace` / `Courier New` / `Menlo`
- 动态追加 `QFontDatabase::systemFont(QFontDatabase::FixedFont).families()`（覆盖自定义系统字体配置，与 Qt 写入值精确对偶）

附带修复：表格空格规范化不再因单元格含反引号而整行跳过，改为 `splitTableCells()` 反引号感知拆分（行内代码内的 `|` 与转义 `\|` 不作为分隔符），表格统一 `| cell |` 风格。

## 教训

- **QFont/QFontDatabase 的行为是运行时环境相关的**（平台、系统字体配置）。任何依赖字体属性判定语义的逻辑（此处是 Qt 用字体族名承载 code 语义）跨平台时必须动态对齐
- 排查此类问题时，`QFontInfo(QFont("monospace")).fixedPitch()` 之类的查询在 offscreen/无显示环境下结果可能不同，需区分测试环境与真实 GUI 环境
- 单元测试无法在 Linux 上触发 Windows 字体名 → 测试直接构造 `font-family:'Courier New'` 的 HTML 验证转换逻辑（`testFromHtmlPlatformMonoSpanToInlineCode`）
