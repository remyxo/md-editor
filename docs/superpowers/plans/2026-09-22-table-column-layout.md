# 表格列宽智能布局实现计划（编辑器 + DOCX，算法单一来源）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 编辑器表格按内容智能分配列宽（短列不折行、超长串封顶断行），同一测量/分配算法与 HTML/PDF 导出共用单一来源，并把列宽比例按页宽缩放后流入 DOCX 导出。

**Architecture:** 把 `HtmlExporter::tableColumnNormalizeScript` 中的测量/分配逻辑抽到 qrc 共享文件 `web/table-column-layout.js`（编辑器页 `<script>` 引用、导出产物运行时读 qrc 内联）；编辑器侧 editor.js 用 TipTap onUpdate 回调去抖触发「克隆离屏测量 → 百分比写入 colgroup + fixed 布局」，不写 PM 文档属性（用户手动拖宽天然区分）；DOCX 侧经 editorApi.getExportHtml() 把 live colgroup 的 px 宽注入导出 HTML，DocxExporter 解析后按页内容宽等比缩放为 tblGrid/tcW + tblLayout fixed。

**Tech Stack:** Qt 6.8.x（本地）/C++17、QWebEngineView、TipTap v2 bundle（tiptap-all.js）、vitest 3（node 环境）、QtTest。

**Spec:** `docs/superpowers/specs/2026-09-22-table-column-layout-design.md`

## Global Constraints

- 所有注释与文档使用中文；代码标识符英文（CLAUDE.md 原则 1）。
- **改 qrc 资源（.js/.css/.html）或 C++ 后必须 `cmake --build build`**——qrc 打包进二进制，不重编译跑的是旧版。告知「已修复」前确认二进制时间戳新于源文件。
- 提交规范：`feat:` / `fix:` / `chore:` 前缀 + 中文描述；每个 Task 单独提交；**不 push**（pre-push 钩子会跑 clang/MSVC 预检，本批次结束时统一处理）。
- **不推进版本号**（CMakeLists.txt / CHANGELOG / tag）——仅用户明确要求时执行。
- 导出管线行为基线：v0.19.0 的 HTML/PDF 产物在「无 colgroup 注入」输入下必须逐字节不变（PDF 输入走 `getContent()`，本计划不改其内容）。
- 测试命令：C++ 全量 `ctest --test-dir build --output-on-failure`；web 单测 `cd src/resources/web && npx vitest run`。
- 用户手动拖过列宽的表格（首行 cell 带 `colwidth` 属性）**永不接管**。
- 源码模式下导出 DOCX：输入经 C++ `toHtml()` 无 colgroup → 退回等宽网格（现状行为，非回归）。

---

### Task 1: 共享算法模块 `table-column-layout.js`（抽提 + vitest）

**Files:**
- Create: `src/resources/web/table-column-layout.js`
- Create: `src/resources/web/table-column-layout.test.js`
- Modify: `src/resources/app.qrc:145`（`<file>web/vendored-lowlight-plugin.js</file>` 之后插一行）
- Modify: `src/resources/web/editor.html:49`（vendored-lowlight-plugin 之后、qwebchannel 之前插 script 标签）

**Interfaces:**
- Consumes: 无（首个任务）
- Produces（后续任务依赖的确切签名，全部挂 `window`）:
  - `__measureTableColumns(table, cfg) → { minW: number[], maxW: number[] } | null`——就地测量，cfg 可选 `{ fontSize, padding, fontFamily }`
  - `__allocateTableColumns(minW, maxW, avail, slack) → number[]`
  - `__replaceColgroupsWith(html, colgroupHtmls) → string`——按文档序替换所有 `<colgroup>…</colgroup>`，数量不匹配原样返回

- [ ] **Step 1: 写失败测试**

创建 `src/resources/web/table-column-layout.test.js`：

```js
// 表格列宽共享算法单测：分配纯数学（三分支/封顶/slack/退化输入）与
// colgroup 文档序替换。测量函数依赖真实布局，由 C++ 脚本构成断言与
// 真机端到端覆盖（见 test_htmlexporter / 计划 Task 5）。
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

// 模块挂全局（IIFE 尾参在 node 环境解析为 globalThis），eval 后取全局引用
const here = dirname(fileURLToPath(import.meta.url));
const mod = join(here, 'table-column-layout.js');
const src = readFileSync(mod, 'utf8');

// 直接 eval 到本作用域的 globalThis（模块 IIFE 尾参是 globalThis）
eval(src);
const allocate = globalThis.__allocateTableColumns;
const replace = globalThis.__replaceColgroupsWith;
const measureExists = typeof globalThis.__measureTableColumns === 'function';

describe('__allocateTableColumns', () => {
    it('暴露测量/分配/替换三个全局函数', () => {
        expect(measureExists).toBe(true);
        expect(typeof allocate).toBe('function');
        expect(typeof replace).toBe('function');
    });

    it('富余分支：sumMax ≤ avail 时按 max 比例放大填满', () => {
        // min=[40,40,40], max=[80,120,160]：第三列封顶 min(160, 3×40)=120
        // sumMax = 80+120+120 = 320 ≤ 680 → 全部 ×680/320
        const w = allocate([40, 40, 40], [80, 120, 160], 680, 0);
        expect(w[0]).toBeCloseTo(170, 6);
        expect(w[1]).toBeCloseTo(255, 6);
        expect(w[2]).toBeCloseTo(255, 6);
    });

    it('封顶：maxW 超过 3×minW 被截到 3×minW', () => {
        // min=[50,50], max=[100,200]→cap 后 [100,150]；avail=210：
        // floor=[min(100,150,105)=100, min(150,150,105)=105]，sumFloor=205 ≤ 210
        // k=(210-205)/(250-205)=1/9 → w=[100, 105+45×(1/9)=110]
        const w = allocate([50, 50], [100, 200], 210, 0);
        expect(w[0]).toBeCloseTo(100, 6);
        expect(w[1]).toBeCloseTo(110, 6);
    });

    it('slack 余量：各列 maxW 加 slack 后参与分配', () => {
        // 同上但 slack=2：maxW=[102,152]，floor=[102,105]，sumMax=254,sumFloor=207
        // k=(210-207)/(254-207)=3/47 → w=[102, 105+47×3/47=108]
        const w = allocate([50, 50], [100, 200], 210, 2);
        expect(w[0]).toBeCloseTo(102, 6);
        expect(w[1]).toBeCloseTo(108, 6);
    });

    it('退化输入：全 0 列归一为 1px 再均分', () => {
        const w = allocate([0, 0], [0, 0], 200, 0);
        expect(w).toHaveLength(2);
        expect(w[0]).toBeCloseTo(100, 6);
        expect(w[1]).toBeCloseTo(100, 6);
    });
});

describe('__replaceColgroupsWith', () => {
    it('按文档序替换各表 colgroup', () => {
        const html = '<table><colgroup><col></colgroup><tbody>1</tbody></table>' +
                     '<table><colgroup><col></colgroup><tbody>2</tbody></table>';
        const out = replace(html, [
            '<colgroup><col style="width:10px"></colgroup>',
            '<colgroup><col style="width:20px"><col style="width:30px"></colgroup>',
        ]);
        expect(out).toBe('<table><colgroup><col style="width:10px"></colgroup><tbody>1</tbody></table>' +
            '<table><colgroup><col style="width:20px"><col style="width:30px"></colgroup><tbody>2</tbody></table>');
    });

    it('数量不匹配时原样返回（防御）', () => {
        const html = '<table><colgroup><col></colgroup></table><table><colgroup><col></colgroup></table>';
        expect(replace(html, ['<colgroup><col style="width:1px"></colgroup>'])).toBe(html);
    });

    it('无 colgroup 时原样返回', () => {
        expect(replace('<p>x</p>', [])).toBe('<p>x</p>');
    });
});
```

- [ ] **Step 2: 运行确认失败**

```bash
cd /home/daijd/claude-workspace/md-editor/src/resources/web && npx vitest run table-column-layout.test.js
```
预期：FAIL——`readFileSync` 找不到 `table-column-layout.js`（模块文件尚不存在）。

- [ ] **Step 3: 实现共享模块**

创建 `src/resources/web/table-column-layout.js`。测量/分配主体**从 `HtmlExporter.cpp:224-275` 逐字搬移且保持紧凑风格**——`testTableColumnNormalizeScriptShape` 的既有子串断言（`table.style.width='min-content'`、`if(cfg.fontSize)…` 等）锚定的是无空格原文，重排成展开风格会假红。仅两处允许的差异：① colgroup 预剥离（新增行）；② `cfg.slack||0` → `(slack||0)`（参数化，对应断言在 Task 2 同步更新）：

```js
// 表格列宽测量/分配共享模块（单一来源）
// 使用方：① 编辑器 editor.js（克隆离屏测量 → 百分比 colgroup + fixed 布局）；
// ② HTML/PDF 导出（HtmlExporter::tableColumnNormalizeScript 运行时读 qrc
//    内联本文件后调用）。改本文件必须同时验证两条链路（导出：ctest
//    test_htmlexporter/test_pdfexporter；编辑器：真机目检，见计划 Task 5）。
// 注意：测量/分配主体与原 tableColumnNormalizeScript 逐字一致（紧凑风格），
// 子串断言锚定原文，勿重排版。
(function (global) {
'use strict';

// 就地测量表格各网格列的 min-content / max-content 宽（px，含 colspan 展开）。
// cfg 可选：{ fontSize, padding, fontFamily } 测量期临时样式（导出侧传打印
// 基准；编辑器不传 ⇒ 按实际渲染样式测量）。测量前剥掉表格已有 colgroup：
// 编辑器二次测量时克隆表带着上轮的百分比 col 会污染 auto 测量；导出侧旧
// colgroup 本来就在写回前被移除，行为不变。返回 null 表示表格不可测。
global.__measureTableColumns=function(table,cfg){
cfg=cfg||{};
var firstRow=table.querySelector('tr');
if(!firstRow||!firstRow.children.length)return null;
var oldCg=table.querySelector('colgroup');
if(oldCg)oldCg.remove();
var saved=[],all=table.querySelectorAll('td,th');
for(var i=0;i<all.length;i++){
saved.push([all[i],all[i].getAttribute('style')]);
if(cfg.fontSize)all[i].style.fontSize=cfg.fontSize;
if(cfg.padding)all[i].style.padding=cfg.padding;
all[i].style.overflowWrap='normal';
all[i].style.wordBreak='keep-all';
}
var savedTable=table.getAttribute('style');
table.style.borderCollapse='collapse';
if(cfg.fontFamily)table.style.fontFamily=cfg.fontFamily;
table.style.tableLayout='auto';
var expand=function(){
var out=[],ci=0;
for(var i=0;i<firstRow.children.length;i++){
var span=Math.max(1,parseInt(firstRow.children[i].getAttribute('colspan')||'1',10));
var w=firstRow.children[i].getBoundingClientRect().width/span;
for(var s=0;s<span;s++)out[ci++]=w;
}
return out;
};
table.style.width='min-content';
var minW=expand();
table.style.width='max-content';
var maxW=expand();
for(i=0;i<all.length;i++){
if(saved[i][1]===null)all[i].removeAttribute('style');
else all[i].setAttribute('style',saved[i][1]);
}
if(savedTable===null)table.removeAttribute('style');
else table.setAttribute('style',savedTable);
return {minW:minW,maxW:maxW};
};

// 分配各列宽（px）。maxW=min(maxW,3×minW) 封顶（长列吸收富余但不独吞）、
// +(slack||0) 余量（百分比折算与边框折叠吃掉零点几像素，短列卡在
// max-content 边界即折行——HTML 导出实测 0.3px 一字一行）、
// floor=min(max,3×min,avail/n)。三分支：sumMax≤avail 按 max 比例放大填满 /
// sumFloor≤avail 保底与期望间线性插值 / 兜底按保底比例压缩（防御分支：
// floor≤等份额 ⇒ sumFloor≤avail 恒成立，理论不可达，保留防参数异常）。
global.__allocateTableColumns=function(minW,maxW,avail,slack){
var n=minW.length,floor=[],sumMax=0,sumFloor=0;
for(var j=0;j<n;j++){
if(!(maxW[j]>0))maxW[j]=(minW[j]>0)?minW[j]:1;
if(!(minW[j]>0))minW[j]=maxW[j];
if(minW[j]>0)maxW[j]=Math.min(maxW[j],minW[j]*3);
if(maxW[j]>0)maxW[j]+=(slack||0);
floor[j]=Math.min(maxW[j],Math.min(minW[j]*3,avail/n));
sumMax+=maxW[j];sumFloor+=floor[j];
}
var w=[];
if(sumMax<=avail){
for(j=0;j<n;j++)w.push(maxW[j]*avail/sumMax);
}else if(sumFloor<=avail){
var k=(avail-sumFloor)/(sumMax-sumFloor);
for(j=0;j<n;j++)w.push(floor[j]+(maxW[j]-floor[j])*k);
}else{
for(j=0;j<n;j++)w.push(floor[j]*avail/sumFloor);
}
return w;
};

// 把序列化 HTML 中按文档序的各 <colgroup>…</colgroup> 替换为给定新 HTML
//（编辑器 getExportHtml 用：序列化空 colgroup → live 布局 px colgroup，供
// DocxExporter 解析 tblGrid）。数量不匹配时原样返回（防御，不半替换）。
global.__replaceColgroupsWith=function(html,colgroupHtmls){
var count=0;
html.replace(/<colgroup[\s\S]*?<\/colgroup>/g,function(){count++;return '';});
if(count!==colgroupHtmls.length)return html;
var idx=0;
return html.replace(/<colgroup[\s\S]*?<\/colgroup>/g,function(m){
return colgroupHtmls[idx++];
});
};
})(typeof window!=='undefined'?window:globalThis);
```

- [ ] **Step 4: 运行测试确认通过**

```bash
cd /home/daijd/claude-workspace/md-editor/src/resources/web && npx vitest run table-column-layout.test.js
```
预期：PASS（全部用例）。

- [ ] **Step 5: 注册 qrc + editor.html 引入**

`src/resources/app.qrc` 在 `<file>web/vendored-lowlight-plugin.js</file>` 行后插入：

```xml
        <file>web/table-column-layout.js</file>
```

`src/resources/web/editor.html` 在 vendored-lowlight-plugin 的 `<script>` 标签（49 行）之后插入：

```html
<!-- 表格列宽测量/分配共享模块（与 HTML/PDF 导出共用单一来源，需在 editor.js 前加载） -->
<script src="table-column-layout.js"></script>
```

- [ ] **Step 6: 既有 web 测试无回归**

```bash
cd /home/daijd/claude-workspace/md-editor/src/resources/web && npx vitest run
```
预期：全绿（新文件不影响既有用例）。

- [ ] **Step 7: 提交**

```bash
cd /home/daijd/claude-workspace/md-editor && git add src/resources/web/table-column-layout.js src/resources/web/table-column-layout.test.js src/resources/app.qrc src/resources/web/editor.html && git commit -m "feat: 抽提表格列宽测量/分配共享模块（编辑器与导出单一来源）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 2: HtmlExporter 归一脚本重构为「内联共享文件 + 写回壳」

**Files:**
- Modify: `src/core/HtmlExporter.cpp:181-307`（`tableColumnNormalizeScript` 整函数替换）
- Modify: `tests/test_htmlexporter.cpp:249-292`（`testTableColumnNormalizeScriptShape` 增补断言）

**Interfaces:**
- Consumes: Task 1 的 `__measureTableColumns(table, cfg)` / `__allocateTableColumns(minW, maxW, avail, slack)`；`ExportCommon::readResource(path)`（ExportCommon.h:19，已有）
- Produces: `HtmlExporter::tableColumnNormalizeScript()` 签名与返回语义不变（返回完整可执行 JS 字符串，可直接 `<script>` 内联）；`window.__normalizeTableColumns(cfg)` 行为不变——PdfExporter.cpp:434 的调用方与 `__fixTableColumns` 兼容层零改动

- [ ] **Step 1: 先加失败断言**

`tests/test_htmlexporter.cpp` 的 `testTableColumnNormalizeScriptShape()`：先改一条再追加三条。slack 断言原文锚定 `cfg.slack`（算法搬入共享模块后 slack 是 `__allocateTableColumns` 的独立参数，`cfg.slack` 子串不复存在）：

把

```cpp
        QVERIFY(js.contains("maxW[j]+=(cfg.slack||0)"));
```

改为

```cpp
        QVERIFY(js.contains("maxW[j]+=(slack||0)"));
```

再在末尾（`QVERIFY(js.contains("table.style.display='table';"));` 之后）追加：

```cpp
        // 单一来源：测量/分配抽至 web/table-column-layout.js，本函数运行时读
        // qrc 内联 + 写回壳调用（编辑器 editor.js 共用同一份算法）
        QVERIFY(js.contains("__measureTableColumns"));
        QVERIFY(js.contains("__allocateTableColumns"));
        // 测量前剥掉既有 colgroup：编辑器二次测量时克隆表带着上轮百分比 col
        // 会污染 auto 测量；导出侧旧 colgroup 本来就在写回前移除，行为不变
        QVERIFY(js.contains("oldCg.remove()"));
```

- [ ] **Step 2: 构建并运行确认失败**

```bash
cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc) && ctest --test-dir build -R test_htmlexporter --output-on-failure
```
预期：FAIL——新断言的 `__measureTableColumns` 不在脚本串中。

- [ ] **Step 3: 重构 tableColumnNormalizeScript**

`src/core/HtmlExporter.cpp` 整函数替换为（函数头注释保留原实测证据，改述来源关系）：

```cpp
QString HtmlExporter::tableColumnNormalizeScript()
{
    // 表格列宽归一：短列保真、长列吸收。HTML 导出与 PDF 导出共用同一实现
    //（PDF 侧经 __fixTableColumns 兼容层调用，见 PdfExporter 的 preparePrint）。
    //
    // 为何要显式分配：浏览器 auto 布局下 CJK 列可任意字间断行 ⇒ 其 min-content
    // 塌缩为单字宽，富余宽度按 max-content 比例尽归长列——「编号」列放不下三位
    // 数字（001 折两行）、「状态」列把「已完成」压成一字一行（用户实测 HTML 导出
    // 1.1 节 60 行表；PDF 侧同因，见 PdfExporter 该函数原注释）。
    //
    // 测量与分配算法抽至 web/table-column-layout.js（单一来源，编辑器 editor.js
    // 共用）：本函数运行时读 qrc 内联该文件，再定义写回壳 __normalizeTableColumns
    //（两趟测量 → floor/cap 三分支分配 → 写 <colgroup> 百分比 + 每行单元格内联
    // 百分比 + table-layout:fixed）。用百分比而非固定 px：HTML 产物在任意窗口
    // 宽度下按同一比例分配；PDF 页面宽固定，效果等同。列几何自描述在行单元格上
    //（不只靠 colgroup）：paged.js 跨页碎片是 cloneNode(false) 浅克隆、丢 colgroup
    // 子元素，只写 colgroup 会换页错列。
    //
    // cfg（可省）：avail 可用宽 px，缺省取 window.__pageContentWidthPx →
    // body.clientWidth；fontSize/padding/fontFamily 为测量期临时样式，PDF 传
    // 打印基准（10pt / 4px 8px / Noto Sans SC），HTML 导出不传 ⇒ 按产物实际
    // 渲染样式测量；slack 每列余量 px；toTable 量表自身盒宽再切 display:table。
    const QString shared = ExportCommon::readResource(QStringLiteral(":/web/table-column-layout.js"));
    return shared + QStringLiteral(
        "window.__normalizeTableColumns=function(cfg){"
        "cfg=cfg||{};"
        "var avail=(cfg.avail>0)?cfg.avail:((window.__pageContentWidthPx>0)?"
        "window.__pageContentWidthPx:(document.body?document.body.clientWidth:0));"
        "if(!(avail>0))return;"
        "document.querySelectorAll('table').forEach(function(table){"
        // 包装表（PDF nativeHf 的 table.hf）必须跳过：它包住整篇文档，其
        // querySelectorAll('td,th')/('tr') 覆盖全文档——本函数会给这些单元格写下
        // 「本表格的列宽百分比」（对包装表即每格 100%），污染随后所有内层表格的
        // intrinsic 测量（实测：短列被压成逐字竖排）
        "if(table.classList.contains('hf'))return;"
        // 每表可用宽：优先取表自身盒宽（HTML 导出下表格是 display:block，盒宽即
        // 容器内容宽；body.clientWidth 含内边距，会用宽 64px ⇒ 分配整体缩水、
        // 「已完成」仍差 5px 折行——实测）。PDF 侧不传 toTable：表格已是
        // display:table 且宽度随内容，量自身盒宽会拿到溢出宽，沿用页宽基准
        "var myAvail=avail;"
        "if(cfg.toTable){"
        "if(table.clientWidth>0)myAvail=table.clientWidth;"
        "table.style.display='table';"
        "table.style.overflowX='visible';"
        "}"
        "var m=window.__measureTableColumns(table,cfg);"
        "if(!m)return;"
        "var n=m.minW.length;"
        "var w=window.__allocateTableColumns(m.minW,m.maxW,myAvail,cfg.slack);"
        "var total=0;for(var j=0;j<n;j++)total+=w[j];"
        "if(!(total>0))return;"
        "var old=table.querySelector('colgroup');if(old)old.remove();"
        "var cg=document.createElement('colgroup');"
        "for(var c2=0;c2<n;c2++){"
        "var col=document.createElement('col');"
        "col.style.width=(w[c2]/total*100).toFixed(3)+'%';"
        "cg.appendChild(col);"
        "}"
        "table.insertBefore(cg,table.firstChild);"
        "var rows=table.querySelectorAll('tr');"
        "for(var r=0;r<rows.length;r++){"
        "var idx=0;"
        "for(var c=0;c<rows[r].children.length;c++){"
        "var sp=Math.max(1,parseInt(rows[r].children[c].getAttribute('colspan')||'1',10));"
        "var wp=0;for(var q=0;q<sp&&idx+q<n;q++)wp+=w[idx+q];"
        "rows[r].children[c].style.width=(wp/total*100).toFixed(3)+'%';"
        "idx+=sp;"
        "}"
        "}"
        "table.style.width='100%';"
        "table.style.minWidth='0';"
        "table.style.tableLayout='fixed';"
        "});"
        "};");
}
```

要点（实现者自查）：
1. 写回段（`col.style.width=…toFixed(3)+'%'`、逐行 `rows[r].children[c].style.width=…`、`tableLayout='fixed'`）与原版逐字一致——既有子串断言依赖；
2. `old.remove()` 后重建 colgroup 对应原版 284 行逻辑（测量函数内已先剥过一次，此处 `old` 恒为 null，防御性保留）；
3. 原版「restore 单元格样式」段已移入共享测量函数内部（save/restore 对称），壳中不再出现；
4. 需要时在文件顶部确认已 `#include "ExportCommon.h"`（该文件 361 行已用 `ExportCommon::readResource`，已包含）。

- [ ] **Step 4: 构建 + 全量导出测试**

```bash
cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc) && ctest --test-dir build -R "test_htmlexporter|test_pdfexporter" --output-on-failure
```
预期：全 PASS。`testExportHtmlNormalizesTableColumns` 的 QJSEngine 语法校验会逐个 `<script>` 块求值组合后的脚本——语法错误在此拦截。

- [ ] **Step 5: 提交**

```bash
cd /home/daijd/claude-workspace/md-editor && git add src/core/HtmlExporter.cpp tests/test_htmlexporter.cpp && git commit -m "refactor: 归一脚本内联共享算法模块（HtmlExporter 与编辑器单一来源）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 3: 编辑器接入（自动布局 + CSS + getExportHtml）

**Files:**
- Modify: `src/resources/web/editor.css:140-148`（表格单元格样式段）
- Modify: `src/resources/web/editor.js`（四处：onUpdate 触发点 :1824 后、init 触发点 :1884 后、setContent 触发点 :2040 附近、新方法插在 getContent :2004 附近）

**Interfaces:**
- Consumes: Task 1 的三个全局函数；TipTap `onUpdate` 回调（既有）；TableView 的 colgroup（既有 DOM）
- Produces: `window.editorApi.getExportHtml() → string`（Task 4 的 MainWindow 接线依赖）；`editorApi._layoutTables()` / `editorApi._scheduleTableLayout()`（内部）

- [ ] **Step 1: editor.css 补两条规则**

`src/resources/web/editor.css` 表格段（134-157 行区域）内，`.ProseMirror th, .ProseMirror td { … }` 规则内追加一行，并新增 tableWrapper 规则：

```css
.ProseMirror th, .ProseMirror td {
    border: 1px solid #ddd;
    padding: 8px 12px;
    text-align: left;
    /* 超长无断点串（URL/JWT）在封顶列宽内断行：不设则 auto/fixed 布局下
       该列 min-content=整串宽，列异常宽（实测 1.2 表列07） */
    overflow-wrap: anywhere;
}

/* 手动拖宽（colwidth px）超出视口的表格横向滚动，不溢出裁切 */
.ProseMirror .tableWrapper {
    overflow-x: auto;
}
```

- [ ] **Step 2: editor.js 新增三个方法**

在 `getContent: function() { … },`（editor.js:2004）之前插入（保持 api 对象成员风格，中文注释）：

```js
    // === 表格列宽自动布局 ===
    // 与导出侧共用 web/table-column-layout.js 的测量/分配（单一来源）。编辑器
    // 侧差异：① 克隆到 body 下离屏容器测量（套 .ProseMirror 类复用编辑器单元
    // 格样式；body.dark 由真实 body 带动暗色规则生效；不进 PM 根节点，避开
    // DOMObserver）；② 写百分比（窗口缩放/侧栏开合免重算）；③ 首行 cell 带
    // colwidth 属性 = 用户手动拖过列宽（columnResizing 写入），尊重用户不接管。
    _scheduleTableLayout: function() {
        var self = this;
        if (this._tableLayoutTimer) return;
        this._tableLayoutTimer = setTimeout(function() {
            self._tableLayoutTimer = null;
            self._layoutTables();
        }, 300);
    },

    _layoutTables: function() {
        if (!this.editor || !window.__measureTableColumns || !window.__allocateTableColumns) return;
        var tables = this.editor.view.dom.querySelectorAll('table');
        for (var t = 0; t < tables.length; t++) {
            var table = tables[t];
            var firstRow = table.querySelector('tr');
            if (!firstRow || !firstRow.children.length) continue;
            var hasUserWidth = false;
            for (var c = 0; c < firstRow.children.length; c++) {
                if (firstRow.children[c].getAttribute('colwidth')) { hasUserWidth = true; break; }
            }
            if (hasUserWidth) continue;
            var wrap = table.closest('.tableWrapper') || table.parentNode;
            var avail = wrap.clientWidth;
            if (!(avail > 0)) continue;
            var holder = document.createElement('div');
            holder.className = 'ProseMirror';
            holder.style.cssText = 'position:absolute;left:-99999px;top:0;visibility:hidden;width:' + avail + 'px';
            var clone = table.cloneNode(true);
            holder.appendChild(clone);
            document.body.appendChild(holder);
            try {
                var m = window.__measureTableColumns(clone, {});
                if (!m || !m.minW.length) continue;
                var w = window.__allocateTableColumns(m.minW, m.maxW, avail, 2);
                var total = 0;
                for (var j = 0; j < w.length; j++) total += w[j];
                if (!(total > 0)) continue;
                var cg = table.querySelector('colgroup');
                if (!cg) {
                    cg = document.createElement('colgroup');
                    table.insertBefore(cg, table.firstChild);
                }
                while (cg.children.length < w.length) cg.appendChild(document.createElement('col'));
                while (cg.children.length > w.length) cg.removeChild(cg.lastChild);
                for (var k = 0; k < w.length; k++)
                    cg.children[k].style.width = (w[k] / total * 100).toFixed(3) + '%';
                // TableView 的 ignoreMutation 已忽略 table/colgroup 变更
                //（vendored tiptap-extension-table.js:66-67），此写回不惊动 PM
                table.style.tableLayout = 'fixed';
                table.style.width = '100%';
                table.style.minWidth = '0';
            } finally {
                document.body.removeChild(holder);
            }
        }
    },

    // 导出 DOCX 专用 HTML：getHTML() 序列化的空 colgroup 按文档序替换为
    // live colgroup 的 px 宽版本（列几何换算为整数 px；DocxExporter 解析
    // width:Npx 生成 tblGrid）。保存路径 getContent() 不受影响。
    getExportHtml: function() {
        if (!this.editor) return '';
        var html = this.editor.getHTML();
        var live = this.editor.view.dom.querySelectorAll('table');
        if (!live.length || html.indexOf('<colgroup') < 0) return html;
        var cgs = [];
        for (var i = 0; i < live.length; i++) {
            var cg = live[i].querySelector('colgroup');
            if (!cg) { cgs.push(''); continue; }
            var colsHtml = '';
            for (var c = 0; c < cg.children.length; c++) {
                var colEl = cg.children[c];
                // col 元素在 fixed 布局下有解析后盒宽；退化路径：按样式串
                //（百分比/px）换算。NaN 一路 || 0 兜底
                var px = colEl.getBoundingClientRect().width
                    || (parseFloat(colEl.style.width) || 0) * live[i].clientWidth / 100
                    || 0;
                colsHtml += '<col style="width:' + Math.max(1, Math.round(px)) + 'px">';
            }
            cgs.push('<colgroup>' + colsHtml + '</colgroup>');
        }
        return window.__replaceColgroupsWith(html, cgs);
    },
```

- [ ] **Step 3: 三处触发点**

1. `onUpdate`（editor.js:1818-1831）内 `self._scheduleLineGutter();` 之后加一行：

```js
                self._scheduleTableLayout();
```

2. `init()`（editor.js:1875 起）内 `this.editor = this._createEditor(el);`（:1884）之后加：

```js
        this._layoutTables();
```

3. `setContent()`（editor.js:2006 起）内 `this._attachMathClickHandler(this.editor.view.dom);`（:2040 附近）之后加：

```js
        this._layoutTables();
```

说明：setContent 在 `_loading` 锁内重建编辑器，onUpdate 被抑制——加载/源码模式退出（exitSourceMode → setContent）的首次布局靠此显式调用；初始（同步、防闪坏布局），后续编辑走 onUpdate 去抖。

- [ ] **Step 4: 语法与构建**

```bash
cd /home/daijd/claude-workspace/md-editor/src/resources/web && node --check editor.js && cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc)
```
预期：语法通过、编译成功（qrc 重打包，css/html/js 变更生效）。

- [ ] **Step 5: 真机验证（编辑器侧）**

```bash
cd /home/daijd/claude-workspace/md-editor && ./scripts/run-editor.sh &
```

打开 `~/Desktop/复杂场景综合测试.md`，逐项目检：
1. **1.1 表**：「状态」列「已完成」单行不折；「编号」列「001」单行；「备注」列吸收余量正常折行；
2. **1.2 表**：列07 的超长 JWT 在本列内断行，整表不出横向滚动条，其余「正常」列基本单行；
3. **1.3 表**：10 列均衡，无逐字竖排；
4. 在 1.1 表任一单元格输入文字 → 约 300ms 后列宽自适应；
5. 拖动任一列边缘（columnResizing 把手）→ 松手后**不再**被自动布局覆盖（继续输入其他单元格也不覆盖该表）；
6. 缩放窗口/开合侧栏 → 列宽按比例保持；
7. 切换暗色主题 → 表格正常；
8. 切源码模式再切回 → 表格布局恢复智能列宽。

任何一项不过：回到实现修正，**不得带病提交**。

- [ ] **Step 6: 提交**

```bash
cd /home/daijd/claude-workspace/md-editor && git add src/resources/web/editor.css src/resources/web/editor.js && git commit -m "feat: 编辑器表格列宽智能布局（克隆离屏测量+百分比固定列）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 4: DOCX 链路（getExportHtml 接线 + 按页宽缩放 tblGrid/tcW/tblLayout）

**Files:**
- Modify: `src/core/DocxExporter.cpp`（ParseState :475 加字段；settings 块 :1609 后赋值；`parseTable` :1146-1257 重构）
- Modify: `src/ui/MainWindow.cpp:908-921`（`getEditorHtmlAsync` 加 `forDocx` 参数）；`MainWindow.cpp:1013`（DOCX 调用点传 true）
- Test: `tests/test_docxexporter.cpp`（新增用例）

**Interfaces:**
- Consumes: Task 3 的 `window.editorApi.getExportHtml() → string`（序列化 HTML 内 `<colgroup><col style="width:Npx">…`）；既有 `exportDocumentXml(html)` 测试助手（test_docxexporter.cpp:23）
- Produces: DOCX 输出中带 colgroup 的表格输出 `<w:tblLayout w:type="fixed"/>` + 缩放后 `<w:gridCol>` + 逐格 `<w:tcW w:w="…" w:type="dxa"/>`；无 colgroup 表格输出与现状逐字节一致（MarkdownHandler::toHtml 路径零影响）

- [ ] **Step 1: 写失败测试**

`tests/test_docxexporter.cpp` 内（沿用 `exportDocumentXml` 助手与既有 QSettings 设置模式，参照文件内其他用例的 settings 写法；若既有用例未设 settings，则本用例先 `QSettings s("md-editor","md-editor"); s.setValue(...)` 显式固定 A4 纵向 1.8cm 四边距，测试结束不必恢复——QTemporaryDir 隔离产物，settings 是进程级共享，按文件内既有用例的惯例执行）：

```cpp
    void testTableColgroupWidthsScaleToPage() {
        // 带列宽 colgroup 的表格（编辑器 getExportHtml 注入）：按页内容宽
        // 等比缩放（A4 纵向 1.8cm 边距 ⇒ 内容 174mm = 9865 twips），
        // 比例 100:300 ⇒ 2466 / 7399（末列吸收取整余量）；tblLayout fixed；
        // 逐格 tcW 与所跨列缩放宽一致
        QSettings s("md-editor", "md-editor");
        s.setValue("export/paperSize", static_cast<int>(QPageSize::A4));
        s.setValue("export/orientation", static_cast<int>(QPageLayout::Portrait));
        s.setValue("export/marginTop", 1.8);
        s.setValue("export/marginBottom", 1.8);
        s.setValue("export/marginLeft", 1.8);
        s.setValue("export/marginRight", 1.8);
        const QString xml = exportDocumentXml(
            "<table><colgroup><col style=\"width:100px\"><col style=\"width:300px\">"
            "</colgroup><tbody><tr><th>A</th><th>B</th></tr>"
            "<tr><td>x</td><td>y</td></tr></tbody></table>");
        // 100px/300px → dxa 1500/4500，sum 6000；f = 9865/6000 ⇒ 2466.25→2466，
        // 末列 9865-2466=7399
        QVERIFY(xml.contains("<w:tblLayout w:type=\"fixed\"/>"));
        QVERIFY(xml.contains("<w:gridCol w:w=\"2466\"/>"));
        QVERIFY(xml.contains("<w:gridCol w:w=\"7399\"/>"));
        QVERIFY(xml.contains("<w:tcW w:w=\"2466\" w:type=\"dxa\"/>"));
        QVERIFY(xml.contains("<w:tcW w:w=\"7399\" w:type=\"dxa\"/>"));
        // 表宽钳到页内容宽
        QVERIFY(xml.contains("<w:tblW w:w=\"9865\" w:type=\"dxa\"/>"));
    }
```

- [ ] **Step 2: 构建运行确认失败**

```bash
cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc) && ctest --test-dir build -R test_docxexporter --output-on-failure
```
预期：新用例 FAIL（无 tblLayout / 缩放值）。

- [ ] **Step 3: DocxExporter 实现**

1. `ParseState`（:475）加成员（放 sectPrXml 附近）：

```cpp
    int contentWidthTwips = 0;                            // 页内容宽（twips）：表格列宽等比缩放基准
```

2. settings 块内（:1609 `const double contentMm = …` 与 :1610 maxImageWidthEmu 赋值处之后）加：

```cpp
        state.contentWidthTwips = contentMm > 0 ? mm2twips(contentMm) : 0;
```

注意：`mm2twips` lambda 定义在 :1616，赋值语句须放在其后（紧随 `state.maxImageHeightEmu` 赋值之后即可）。

3. `parseTable`（:1146-1257）重构——行累积与最终拼装分离，colgroup 解析完成即定缩放列宽（TipTap 序列化保证 colgroup 先于行；`MarkdownHandler::toHtml` 无 colgroup 走原路径）：

```cpp
static QString parseTable(QXmlStreamReader &r, ParseState &s)
{
    // 行内容先累积，tblPr/tblGrid 在收尾拼装：带 colgroup（编辑器 getExportHtml
    // 注入）的表格按页内容宽等比缩放列宽并写 tblLayout fixed + 逐格 tcW；
    // 无 colgroup 的表格（toHtml 产物）保持原有 tblW auto + 默认 4680 等宽网格
    QString rowsXml;
    QString tblGrid;
    QList<int> colDxa;      // 缩放后各列宽（twips）；空 = 无 colgroup
    int colCount = 0;
    bool firstRow = true;
    int headerCols = 0;     // 表头行列数（无 colgroup 时默认 tblGrid 用，含 colspan 求和）

    while (!r.atEnd()) {
        auto tt = r.readNext();
        if (tt == QXmlStreamReader::EndElement
            && r.name().toString().compare(QLatin1String("table"), Qt::CaseInsensitive) == 0)
            break;
        if (tt != QXmlStreamReader::StartElement) continue;
        QString tag = r.name().toString().toLower();

        if (tag == "colgroup") {
            // 解析 colgroup：px → dxa（1px ≈ 15 dxa at 96dpi），随后按页内容宽
            // 等比缩放（编辑器视口宽 ≠ Word 页宽，直接用会溢出/缩水）。
            // 属性名锚定（(?:^|;)）：TipTap 序列化表格无 colwidth 时每列只有
            // min-width:25px，裸 width 子串匹配会把 25 当列宽
            while (!r.atEnd()) {
                auto ctt = r.readNext();
                if (ctt == QXmlStreamReader::EndElement
                    && r.name().toString().compare(QLatin1String("colgroup"), Qt::CaseInsensitive) == 0)
                    break;
                if (ctt == QXmlStreamReader::StartElement
                    && r.name().toString().compare(QLatin1String("col"), Qt::CaseInsensitive) == 0) {
                    QString w = r.attributes().value("style").toString();
                    QRegularExpression re("(?:^|;)\\s*width\\s*:\\s*(\\d+)px");
                    auto m = re.match(w);
                    int dxa = 2340; // 默认宽
                    if (m.hasMatch()) dxa = m.captured(1).toInt() * 15;
                    colDxa.append(dxa);
                    colCount++;
                }
            }
            // 等比缩放到页内容宽：f = content/sum，前 n-1 列 qRound，末列吸收
            // 取整余量（gridCol 总和与 tblW 严格一致）；双向缩放（编辑器窄表
            // 放大铺满页宽，比例不变）
            if (!colDxa.isEmpty() && s.contentWidthTwips > 0) {
                double sum = 0;
                for (int d : colDxa) sum += d;
                if (sum > 0) {
                    const double f = s.contentWidthTwips / sum;
                    int acc = 0;
                    for (int i = 0; i + 1 < colDxa.size(); ++i) {
                        colDxa[i] = qRound(colDxa[i] * f);
                        acc += colDxa[i];
                    }
                    colDxa.last() = s.contentWidthTwips - acc;
                    if (colDxa.last() < 1) colDxa.last() = 1;
                }
            }
            for (int d : colDxa)
                tblGrid += QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(d);
        } else if (tag == "tbody") {
            continue; // 穿透 tbody
        } else if (tag == "tr") {
            rowsXml += QStringLiteral("<w:tr>");
            while (!r.atEnd()) {
                auto rtt = r.readNext();
                if (rtt == QXmlStreamReader::EndElement
                    && r.name().toString().compare(QLatin1String("tr"), Qt::CaseInsensitive) == 0)
                    break;
                if (rtt != QXmlStreamReader::StartElement) continue;
                QString cellTag = r.name().toString().toLower();

                if (cellTag == "th" || cellTag == "td") {
                    bool isTh = (cellTag == "th");
                    // 检查对齐与合并跨列（属性须在 parseInline 消费元素前读取）
                    QString cellSty = r.attributes().value("style").toString();
                    QString cellAlign = extractAlign(cellSty);
                    QString cellSpan = r.attributes().value("colspan").toString();
                    const int span = cellSpan.toInt();
                    if (firstRow)
                        headerCols += (span > 1 ? span : 1);
                    // 单元格宽：带 colgroup 用缩放后列宽（跨列求和，与 gridCol
                    // 严格一致）；无 colgroup 保持原 2340
                    int cellW = 2340;
                    if (!colDxa.isEmpty()) {
                        int colIdx = 0;
                        // 当前行之前的单元格累计列进度：行内流式解析，直接边写边推
                        // （见下方 colCursor 维护）
                        cellW = 0;
                        for (int q = 0; q < span && colCursor + q < colDxa.size(); ++q)
                            cellW += colDxa[colCursor + q];
                        if (cellW < 1) cellW = 2340;
                        colCursor += qMax(1, span);
                    }
                    rowsXml += QStringLiteral("<w:tc>");
                    // 单元格属性：tcPr 无 w:jc 子元素（CT_TcPr 只认 tcW/shd/vAlign 等），
                    // 水平对齐是段落属性，写入下方单元格段落的 <w:pPr>
                    rowsXml += QStringLiteral("<w:tcPr>");
                    rowsXml += QStringLiteral("<w:tcW w:w=\"%1\" w:type=\"dxa\"/>").arg(cellW);
                    if (span > 1) rowsXml += QStringLiteral("<w:gridSpan w:val=\"%1\"/>").arg(cellSpan);
                    if (isTh) rowsXml += QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"D9E2F3\"/>");
                    rowsXml += QStringLiteral("</w:tcPr>");

                    // 单元格内容（内联解析）
                    QString cellInner = parseInline(r, cellTag, s);
                    QString pPr = cellAlign.isEmpty() ? QString()
                        : QStringLiteral("<w:pPr><w:jc w:val=\"%1\"/></w:pPr>").arg(cellAlign);
                    rowsXml += QStringLiteral("<w:p>") + pPr + cellInner + QStringLiteral("</w:p>");
                    rowsXml += QStringLiteral("</w:tc>");
                }
            }
            rowsXml += QStringLiteral("</w:tr>");
            firstRow = false;
            colCursor = 0;   // 行结束复位（colspan 错位的表格按行独立计）
        }
    }

    // 无 colgroup：按表头列数生成默认等宽网格（现状行为）
    if (tblGrid.isEmpty()) {
        QString cols;
        for (int i = 0; i < (headerCols > 0 ? headerCols : 2); ++i)
            cols += QStringLiteral("<w:gridCol w:w=\"4680\"/>");
        tblGrid = cols;
    }

    // tblPr：带 colgroup ⇒ fixed + 表宽=页内容宽（CT_TblPr 顺序：tblStyle→tblW
    // →tblBorders→tblLayout）；无 colgroup ⇒ 原样（tblW auto，Word 自动布局）
    QString tblPr = QStringLiteral(
        "<w:tblPr>"
        "<w:tblStyle w:val=\"TableGrid\"/>"
        "<w:tblW w:w=\"%1\" w:type=\"%2\"/>"
        "<w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "</w:tblBorders>%3"
        "</w:tblPr>");
    if (colDxa.isEmpty()) {
        tblPr = tblPr.arg(0).arg(QStringLiteral("auto"), QString());
    } else {
        tblPr = tblPr.arg(s.contentWidthTwips).arg(QStringLiteral("dxa"),
                      QStringLiteral("<w:tblLayout w:type=\"fixed\"/>"));
    }

    // OOXML schema：tblGrid 必须先于行
    return QStringLiteral("<w:tbl>") + tblPr
         + QStringLiteral("<w:tblGrid>") + tblGrid + QStringLiteral("</w:tblGrid>")
         + rowsXml + QStringLiteral("</w:tbl>");
}
```

并在 `parseTable` 函数体开头（`QString rowsXml;` 之前）声明行内列游标：

```cpp
    int colCursor = 0;      // 当前行已消费列进度（colspan 推进；带 colgroup 时 tcW 取列用）
```

实现者自查：
1. 无 colgroup 路径的输出与原实现**逐字节一致**（tblW auto / 4680 网格 / tcW 2340）——既有用例（test_docxexporter.cpp:511-526 断言 4 个 gridCol）是回归锁；
2. `qMax`/`qRound` 已被 Qt 头覆盖（文件内已用）；
3. 原实现 `result` 的 trPos 前插 tblGrid 逻辑删除（新拼装顺序天然正确）。

- [ ] **Step 4: MainWindow 接线**

`MainWindow.cpp:908-921` 的 `getEditorHtmlAsync` 加参数（默认 false，PDF/HTML 调用点不动）：

```cpp
static void getEditorHtmlAsync(EditorTab *tab,
                               std::function<void(const QString &)> cb,
                               bool forDocx = false)
{
    if (!tab || !tab->webView())
        return;
    const bool src = tab->isSourceMode();
    // DOCX 导出取 getExportHtml()：live colgroup 的 px 列宽注入序列化 HTML，
    // 供 DocxExporter 生成 tblGrid；源码模式 getSourceContent 经 toHtml 无
    // colgroup，退回等宽网格（现状行为）
    const QString call = src ? QStringLiteral("window.editorApi.getSourceContent();")
                             : (forDocx ? QStringLiteral("window.editorApi.getExportHtml();")
                                        : QStringLiteral("window.editorApi.getContent();"));
    tab->webView()->page()->runJavaScript(call,
        [src, cb](const QVariant &result) {
            QString s = result.toString();
            cb(src ? MarkdownHandler::toHtml(s) : s);
        });
}
```

`onExportDocx`（:1013）调用点改为：

```cpp
    getEditorHtmlAsync(tab, [this, path, docDir](const QString &html) {
```
→ 在 lambda 闭合后补第三参：

```cpp
    getEditorHtmlAsync(tab, [this, path, docDir](const QString &html) { … },
                       /*forDocx=*/true);
```

- [ ] **Step 5: 构建 + DOCX 测试**

```bash
cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc) && ctest --test-dir build -R test_docxexporter --output-on-failure
```
预期：新用例 PASS 且既有用例（含 511-526 无 colgroup 默认网格）全 PASS。

- [ ] **Step 6: 真机验证（DOCX 侧）**

```bash
cd /home/daijd/claude-workspace/md-editor && ./scripts/run-editor.sh &
```

WYSIWYG 模式打开 `~/Desktop/复杂场景综合测试.md` → 文件 → 导出 DOCX：
1. 本机 LibreOffice/WPS 打开初检：1.1 表列宽比例与编辑器一致（备注宽、状态窄而单行）；1.2 表 12 列均在页内、列07 内断行；无表格溢出页宽；
2. **macOS Word 双击验证**（既定 A/B 样本法流程，WPS/LibreOffice 测不出 schema 违规）：把本版导出的 docx 与上一版各拷一份到 macOS，双击打开，对照列宽版式与可开回性。此步需要用户配合，执行到此处时**先停下告知用户**，勿自行宣称完成。

- [ ] **Step 7: 提交**

```bash
cd /home/daijd/claude-workspace/md-editor && git add src/core/DocxExporter.cpp src/ui/MainWindow.cpp tests/test_docxexporter.cpp && git commit -m "feat: DOCX 表格列宽按页宽缩放（getExportHtml 侧信道注入 live colgroup）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 5: 全链路回归、文档更新与收尾

**Files:**
- Modify: `CLAUDE.md`（关键文件表加一行）
- 无代码改动

**Interfaces:**
- Consumes: Task 1-4 全部产出
- Produces: 验证记录 + 文档更新

- [ ] **Step 1: 全量自动化回归**

```bash
cd /home/daijd/claude-workspace/md-editor && cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure && cd src/resources/web && npx vitest run
```
预期：C++ 全量与 web 全量全绿。

- [ ] **Step 2: HTML/PDF 导出无漂移抽查**

运行 app，打开 `~/Desktop/复杂场景综合测试.md`，分别导出 HTML 与 PDF：
1. HTML：表格列宽与 0.19.0 产物观感一致（归一脚本仍生效）；
2. PDF：**页数与 0.19.0 基线一致**（输入走 getContent 未变、算法未变，分页不应移动）。

- [ ] **Step 3: CLAUDE.md 关键文件表补行**

「关键文件」表中 `src/resources/web/editor.js` 行附近插入：

```markdown
| `src/resources/web/table-column-layout.js` | 表格列宽测量/分配共享算法（编辑器与 HTML/PDF 导出单一来源；DOCX 侧经 getExportHtml 注入 live colgroup） |
```

- [ ] **Step 4: 提交**

```bash
cd /home/daijd/claude-workspace/md-editor && git add CLAUDE.md && git commit -m "chore: 关键文件表补 table-column-layout.js（表格列宽共享算法）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

- [ ] **Step 5: 汇总与待办移交**

向用户汇报：已完成项、真机验证结果（含 Word 验证是否已做）、未尽事项（若 Word 验证待用户配合，明确列为待办）。**不推进版本号、不打 tag、不 push**——等用户指示。

---

## Self-Review 记录

- **Spec 覆盖**：spec §1（共享模块）→ Task 1/2；§2（编辑器接入+CSS）→ Task 3；§3（DOCX 链路）→ Task 4；§4（测试矩阵）→ 各 Task 步骤 + Task 5；§5（风险边界：用户拖宽跳过/克隆测量/只读模式/源码模式退回）→ Task 3 实现、Global Constraints。无缺口。
- **类型一致性**：`__measureTableColumns/__allocateTableColumns/__replaceColgroupsWith`（Task 1 定义，Task 2/3 按同名调用）；`getExportHtml`（Task 3 定义、Task 4 接线）；`ParseState::contentWidthTwips`（Task 4 内定义与使用一致）。
- **占位符扫描**：所有代码步骤均含完整代码；无 TBD/TODO。
