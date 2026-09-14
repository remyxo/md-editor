# HTML 属性转义与实体解码（往返保真统一规则）

> 2026-09-13 审查阶段 1 确立的对称规则。核心原则：**凡是拼进 HTML 属性值的用户内容，
> 写侧必须经 `escapeHtmlAttr` 转义；凡是正则从属性值捕获回来的内容，读侧必须经
> `unescapeHtmlEntities` 解码**。两侧不对称时，`&`/`<`/`"` 类内容每保存一轮就叠加
> 一层转义（`&` → `&amp;` → `&amp;amp;` → …），永不收敛；或属性在第一个引号处断裂，
> 后半段静默丢失。

## 根因背景

编辑器的保存链路是 `toHtml(MD→HTML) → TipTap 编辑器 → getHTML() → fromHtml(HTML→MD)`。
其中 **TipTap/浏览器的 `innerHTML` 序列化会对属性值做实体转义**（`&`→`&amp;`、
`<`→`&lt;`、`"`→`&quot;`）——这是平台恒定行为，与我们的写侧是否转义无关：

- 写侧**不转义**（旧块级公式）：`data-latex="\text{"hi"}"` 属性在第一个 `"` 处断裂，
  公式后半段永久丢失（P1-3）。
- 读侧**不解码**（旧 data-latex 捕获）：TipTap 序列化引入的 `&amp;` 被当字面内容写进
  `.md`，下一轮再叠一层（P1-4）。

两侧对称后，一轮往返字节一致；历史损坏文件（已含 `&amp;` 的存盘）载入时经解码自愈，
保存一次即永久修复。

## 覆盖点清单（MarkdownHandler.cpp）

| 载体 | 写侧（toHtml） | 读侧（fromHtml） |
|---|---|---|
| 块级/行内公式 `data-latex` | `escapeHtmlAttr` | 捕获值过 `unescapeHtmlEntities` |
| 代码围栏语言 `class="language-…"` | `escapeHtmlAttr` | 语言捕获解码（同上） |
| 脚注 `data-id`（引用/定义） | `escapeHtmlAttr` | `unescapeHtmlEntities`（引用与定义同序） |
| 图片 `alt`/`src`、链接文本/href | 既有转义 | 既有 `unescapeHtmlEntities` |
| 标题内强调 | 经 ZZ 占位保护通道 | 占位还原（不走属性） |

判定方法：对任何新引入的「字符串拼进 HTML 属性」的代码点，grep 对照本表；两侧
必须成对出现。`escapeHtmlAttr`（391 行附近）转义 `&`/`<`/`>`/`"`；
`unescapeHtmlEntities` 是其逆，仅用于属性捕获值，**不得**用于元素文本（文本由
Qt/TipTap 各自的序列化规则负责）。

## 真实链路测试夹具（tests/tiptap_serialize.h）

C++ 直连 `toHtml → fromHtml` 会**绕过 TipTap 序列化步**，测不出本类缺陷——旧测试
全绿而线上必坏的原因。夹具在两步之间插入 `tiptapSerialize()`：对 toHtml 产物施加
与 TipTap `getHTML()` 相同的属性值实体转义（`&`→`&amp;`、`<`→`&lt;`、`"`→`&quot;`），
再喂给 fromHtml。`test_roundtrip_matrix`（500+ 例）已全量经此链路；新增往返用例
一律走该夹具，禁止 C++ 直连断言。

## 典型症状速查

| 症状 | 诊断 |
|---|---|
| 属性值在引号处截断（`[^a"b]` → `[^a]`） | 写侧缺 `escapeHtmlAttr` |
| 存盘内容含 `&amp;`/`&lt;` 且逐轮叠加 | 读侧缺 `unescapeHtmlEntities` |
| 公式 `aligned` 环境的 `&` 列分隔符丢失 | 同上（对齐列是最常见触发者） |
| 历史文件打开显示 `&amp;=` 字面量 | 载入侧解码自愈即可，保存一次永久修复 |

## 相关

- 阶段 1 修复提交 `03624fc`（属性转义 + 实体解码 + 空 alt 图片 + 标题内强调）
- 阶段 3 DOCX 侧同型规则：脚注 id 经 `xmlEscape` 进 `w:t`/属性
- [`qt-642-escaping-compat.md`](qt-642-escaping-compat.md)——Qt 版本差异侧的转义兼容（占位符保护范式）
