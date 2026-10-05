# vendor CodeBlockLowlight 插件说明

## 背景

`@tiptap/extension-code-block-lowlight` 的源码分两层：Extension 壳（选项/命令/node view 注册）与内部 ProseMirror plugin（`src/lowlight-plugin.ts`，bundle 内压缩符号）。该 plugin 存在两个性能问题，均出在高亮时机与粒度：

1. **state.init 同步全量高亮**：编辑器创建时对文档**全部**代码块逐个 `lowlight.highlight`——大文档（markdown-guide.md 684 行 / 56 代码块）实测 ~440ms，阻塞 setContent 关键路径（启动恢复、有改动退出源码重建、外部重载均受害）；
2. **apply 无增量**：每次 docChanged（含光标进出代码块触发的空事务之外的任何编辑）都**重新高亮全部块**——代码块内每击键全量重算 ~300ms（56 块文档），打字卡顿隐患。

Extension 壳无需改动，只 vendor plugin 层。

## 来源与版本

| 项 | 值 |
|----|----|
| 上游文件 | `@tiptap/extension-code-block-lowlight` `src/lowlight-plugin.ts` |
| 上游版本 | 3.30.1（见 `package-lock.json`） |
| vendored 位置 | `src/resources/web/vendored-lowlight-plugin.js` |
| 挂载方式 | `editor.html` 中 `tiptap-all.js` 之后 `<script>` 加载；`app.qrc` 已登记 |
| 替换机制 | `editor.js` 的 `CodeBlockLowlightWithNodeView.addProseMirrorPlugins()` 过滤上游 plugin 后压入 `window.AsyncLowlightPlugin(...)` |

**保底降级**：vendored 文件未加载、或上游 plugin key 前缀变化导致过滤不中时，`addProseMirrorPlugins` 原样返回 `parent()`——退回上游同步行为（性能差但功能正确）。上游 key 以 `lowlight` 开头（PluginKey 构造参数），过滤条件 `p.key.indexOf('lowlight') === 0`。

## 相对上游的改动（共 6 项，W4-T7 起第 6 项含排干复位）

1. **init 不同步高亮**：块收集入 `pending`（pos 列表），空闲分批补算（`requestIdleCallback` 降级 `setTimeout(16)`），分批调度见第 6 项（视口优先 + 时间预算）。批次大小 6（单块 ~8-30ms）。**init 收集是必须的**：setContent 销毁重建编辑器不走 apply，新文档的块只能在 init 里发现。init 同时结束在途补全轮（清轮状态与视口缓存——新文档代次开新轮）。
2. **apply 增量化**：docChanged 时只同步重算「与 step 相交」的块（块内编辑/属性变更/整块替换），未相交块经高亮缓存命中复用，缓存 miss（尚未异步算到）入 pending。撤销/重做等全文档 step 的操作仍会重算受影响块，其余走缓存。
3. **批次经 `tr.setMeta(pluginKey)` 合入 state**：decoration 增量由 plugin state 自身管理；文档变化以 `epoch` 计数作废在途批次（旧批次 dispatch 时 epoch 不匹配即丢弃）。
4. **语言可用性判定**：上游额外查 highlight.js core 的 `getLanguage`（供手动 register 场景），本项目 lowlight 为 `tiptap-entry.js` 自建实例（`registerLanguage` 预注册常用语言），`listLanguages` + `registered` 判定足够。
5. **零相交编辑走 DecorationSet.map 快路径**：docChanged 前置零块相交判定——step 与全部块无交集且 pending 为空时（pending 为空是前提：pending 块的 pos 是旧 doc 坐标，非空必须走慢路径经 findBlocks 重收集），既有装饰经 `state.decos.map(tr.mapping, tr.doc)` 随事务平移（借空事务分支先例），跳过逐块缓存键拼接与全量 DecorationSet 重建，epoch 不变（慢路径会 epoch+1，可作路径判定断言）。**真实覆盖**：快路径实际命中「折叠插入类（from==to）零块相交编辑」——普通打字等在块外插入文本的场景；删除/大跨度替换因 step 区间（旧 doc 坐标）与 tr.doc 块位（新 doc 坐标）的既有假相交语义（增量改造的相交判定本就如此比较）会判相交而走慢路径——偏保守的安全方向（多算不丢高亮）。
6. **空闲分批调度「视口优先 + CM6 Work 风格时间预算」**（对症切源码后「空→高亮渐显」——文档顺序批次最后才轮到可视区在文档尾部的块）：
   - **取块顺序**：每片取块前对 pending 稳定排序——可视区内块（`view.nodeDOM(pos)` 取块元素，`getBoundingClientRect` 与 `window.innerHeight` 求交；keep-warm 的 `visibility:hidden` DOM 布局仍有效）→ 光标邻域块（|块 pos − 选头| ≤ 2000）→ 其余按文档顺序；同优先级按 pos 升序（确定性，测试可断言顺序）。
   - **轮内视口判定缓存**：块 pos → 是否可视，同一轮复用（避免每片对全部待算块重复 getBoundingClientRect 强制布局）；窗口/编辑器容器滚动位置变化即整体失效（读滚动是廉价状态读）。跨轮（epoch 变更/编辑器重建）清空重探。
   - **时间预算**（CodeMirror 6 `@codemirror/language` parse work 调度思想）：单片高亮预算 `CHUNK_BUDGET_MS=50`（片内 `performance.now()` 超此即止，保底 1 块）；片间最小停顿 `CHUNK_GAP_MS=100`（让路输入/渲染）；单轮总预算 `TOTAL_BUDGET_MS=3000`，超限降级为**纯文档顺序 2 块/片**继续（跳过视口排序成本，远块保底推进，永不饿死）。
   - **片间让路**：`navigator.scheduling?.isInputPending?.()` 为真时再让一片；该 API 缺失（QWebEngine 版本差异）时静默降级为固定片间停顿。
   - **轮首片仍经空闲回调起步**（`_ric`，不与加载/渲染抢跑，保持既有语义）；轮内后续片走片间停顿（`setTimeout(CHUNK_GAP_MS)`）。轮 = 同一文档代次内一批 pending 从出现到排干。**排干即收轮**（W4-T7）：批次合并 `remain=0` 时调 `_endRound()` 复位轮状态与视口判定缓存——`scheduleTick` 以 `_roundActive` 分通道（轮首片空闲回调 / 轮内片间停顿），排干不复位会让下一轮首片误走片间停顿通道。此前仅 `init`（新文档代次）与 `runChunk` 的 pending 空早退两处收轮，batch merge 排干这最后一条退出路径残留 `_roundActive=true`（现行生产路径未观察到实害——排干后唯一的 scheduleTick 入口是编辑器重建，而重建前的 `init` 本就收轮；此改动是契约守护 + 为「慢路径补 scheduleTick」待办扫清前提）。
   - **测试钩子**：`window.__setViewportProbe(fn)` 注入 mock 视口判定（jsdom 无真视口，矩形恒零会全部判为视口外）；传假值恢复 production 默认。`window.__highlightRoundState()`（W4-T7）只读轮状态 `{roundActive, roundEpoch, verdicts}`（排干复位契约断言用，`_roundActive` 是模块私有、生产路径外部不可见）。

### 缓存与单例（上游没有的全局状态）

- **高亮缓存** `_hlCache`：`Map<lang + '\x00' + text, [{len, cls}]>`，存**相对区间**（块内偏移）而非绝对位置 Decoration——缓存可跨文档代次复用（撤销、重建编辑器都命中）。LRU 上限 256。
- **PluginKey 单例** `_pluginKey`：**必须模块级单例**。setContent 销毁重建编辑器时 extension 重新调 `AsyncLowlightPlugin()`，若每次 `new PluginKey`，在途批次闭包里的旧 key 在新编辑器 state 上 `getState` 返回 undefined，批次链路断裂（高亮永远补不全）。同一时刻只有一个编辑器实例，key 全局唯一即可。
- **`_view` 登记**：plugin 的 `view()` 钩子登记当前 EditorView（空闲批次需要 dispatch），destroy 时清空。

## 行为契约（回归防线）

`vendored-lowlight-plugin.test.js` 7 用例（vitest + jsdom，eval 加载真实 bundle）：

1. setContent 同步路径**零高亮调用**（不阻塞加载关键路径）
2. 空闲分批补全：语言块（`highlight`）与无语言块（`highlightAuto`）均出现 hljs span（等 pending 排干后断言调用计数——时间预算/片间停顿下首片可能只完成部分块，中途快照计数无意义）
3. 编辑增量：单块击键只重算该块（其他块缓存命中，`lowlight` 零调用），且新内容**同步**高亮（不等空闲批次——击键反馈必须即时）
4. 撤销后高亮保持（缓存命中重建 decoration，重算 ≤1 块）
5. 普通文本击键（非代码块段落输入）走 DecorationSet.map 快路径——epoch 不变断言路径（慢路径会 epoch+1）、装饰整体平移量恒定断言保位（高亮零调用）
6. 视口优先与时间预算（fake timer 确定性驱动）：`__setViewportProbe` 注入 mock 后，**视口内块先于视口外块着色**；片间停顿窗口内不推进；`isInputPending` 为真再让一片；总预算超限降级纯文档顺序 2 块/片；`navigator.scheduling` 缺失时静默降级照常排干
7. 排干复位（fake timer 确定性驱动，W4-T7）：3 块文档首片即排干（batch merge `remain=0` 正中分支）后，`__highlightRoundState()` 断言 `roundActive=false`、视口判定缓存清空、轮代次复位——下一轮首片可经空闲回调起步

### 曾评估未采纳：源码模式期间暂停高亮调度（W4-T7）

Wave 2 账本登记的 switch_ms 双峰（~70ms 主切换 + ~130ms 高亮调度片抢跑）拟在进入源码期间暂停 gapTick 推进。W4-T7 于 HEAD 实测（4k bench 文档 4799 行 / 162 围栏块——首帧时刻 pending 156，闸门释放首片已着色 6；9 轮，`scripts/measure-mode-switch.sh`）：switch_ms **单峰**（中位 688ms，样本 683–719ms），进源码 ~631–670ms（worker 转换往返主导）、快速退出 ~48–53ms——W4-T1/T4 转换 worker 化已吸收双峰，暂停无可测收益，**裁撤**（用例 7 改作排干复位回归锚）。附注：整段源码停留期冻结高亮还会废掉 viewport_hl_ms 观测口径（该指标要求源码停留期内调度片持续推进），与 W2-T3 设计意图相逆。

**guide 档抽查（收尾补测，双峰核销在原始观测档位闭环）**：W2 双峰 ~70ms 量级显系小文档观测（4k 同步时代 enter 亦需数百 ms），故以同一工具对 markdown-guide.md（687 行 / 49 围栏块，其中 mermaid 10 走独立节点不经 lowlight）补测 8 轮（两个独立会话、无并发负载）：switch_ms **无双模结构**——7/8 轮落 127.5–139.5ms 单带，仅首会话首轮 79.5ms 低离群（cpp 分解显示该轮差异全在 JS填充 13ms、其余 7 轮 59–74ms，无伴随结构，成因未定位）。两点如实注记：① guide 主带 ~130ms 与账本**次峰**同值域，账本**主峰** ~70ms 今日仅 1/8 离群——账本原始样本已随 wave2 计划目录灭失，绝对值不可对销，本存档以同工具同字段同口径的**结构判定**（无双模交替）为核销口径；② 金丝雀 `queuedAtRelease=0 / pendingAtRelease=6` 属探针文件头的「在途口径」（小文档加载快，首片早于探针注入跑掉，后续片经 setTimeout 在途推进、闸门只拦 ric 不拦 setTimeout）——即测量期间调度片本就可与切换窗口交错（W2 归因次峰的机制条件在测），HEAD 下仍单带，次峰结构不复现。cpp 分解：guide enter 主项是 **JS填充 ~60ms**（源码视图填充自身成本；fromHtml worker 仅 30–34ms、退出快速路径 ~12ms）。

**当时拟用的挂接点方案（存档备查，未实现、不占用接口面）**：JS 侧挂接优于 C++ 侧——模式状态机在 editor.js（`enterSourceMode` :2482 / `exitSourceMode` :2591 慢路径 `setContent` 之后 / `syncSourceViewContent` :2716 三处）；C++ 侧 `onToggleSourceMode` 回调链跨 3 段 runJavaScript + worker 往返，置位点会散落且易遗漏 `syncSourceViewContent` 路径。API 形态拟 `window.__setHighlightPaused(bool)`（`__` 前缀内部协议，`__preparePrint` 先例）。若未来重评：4k 档收益已被 worker 化吸收（同口径复测单峰）；guide 档在调度片在途条件下亦无双模结构（上一段 8 轮抽查），两档均无可测收益来源。

### 已知暗角 / 待办

- **docChanged 慢路径 push pending 后无人调度批次**：慢路径把缓存 miss 的块收进 pending 后未调 `scheduleTick`（仅批次合并分支与 `view()` 钩子调度）——初始批次排干后新诞生的代码块（粘贴插入、撤销恢复未算过的块）需等其内部被直接编辑（touched 同步重算）才有高亮。先于快路径改造存在、非回归；待办：慢路径 pending 非空时 return 前补一次 `scheduleTick`（W4-T7 排干复位落地后，该待办修复引入的新一轮首片将正确经空闲回调起步，不会被上轮残留的 `_roundActive` 带偏）。

## 上游同步维护

升级 `@tiptap/extension-code-block-lowlight` 后，对照 `node_modules/@tiptap/extension-code-block-lowlight/src/lowlight-plugin.ts`：

- 重点核对 **apply 的触发条件**（上游是否仍无条件重算）与 **getDecorations 语义**（token → Decoration 的映射、class 拼接方式）
- 上游若修了增量问题（或 key 前缀变化），考虑退役 vendored 版本——`editor.js` 的过滤逻辑会自动降级回上游行为，退出路径已内置
- 重建 bundle（`scripts/build-tiptap.sh`）后跑上述 7 用例

## 实测收益（markdown-guide.md：684 行 / 56 代码块 / 10 mermaid 图）

| 指标 | 改前 | 改后 |
|------|------|------|
| setContent 内部 lowlight 耗时 | 288-436ms | **0ms**（同步路径零调用） |
| setContent 内部总耗时 | ~969ms | ~457ms |
| 启动累计（tab 创建→内容上屏） | ~3289ms | ~1469ms（与方案 A 叠加） |
| 代码块内击键全量重算 | ~300ms/键 | 仅当前块（~8-30ms） |

「有改动退出源码」（setContent 全量重建场景）同根受益。
