// =============================================================================
// vendored-lowlight-plugin.js
//
// vendor 来源：@tiptap/extension-code-block-lowlight 3.30.1（src/lowlight-plugin.ts）
// 引入原因（与 docs/vendor-qt-markdown-writer.md 同类决策）：
//   上游 plugin 在 state.init 对文档全部代码块同步 lowlight.highlight——
//   大文档（56 代码块 ~440ms）阻塞 setContent 关键路径；且 apply 无增量，
//   代码块内每击键全量重算所有块（打字卡顿）。
//
// 相对上游的改动（行为对齐项见 vendored-lowlight-plugin.test.js）：
//   1. init 不同步高亮：块收集入 pending，空闲分批补算；分批调度为
//      「视口优先 + CM6 Work 风格时间预算」：可视区内块 → 光标邻域块 →
//      其余按文档顺序，单片时间预算 + 片间停顿让路输入/渲染，单轮总预算
//      超限降级为纯文档顺序 2 块/片保底推进（对症切源码后「空→高亮渐显」）；
//      轮排干即复位（batch merge remain=0 收轮，下一轮首片仍经空闲回调起步）
//   2. apply 增量化：docChanged 只同步重算「与 step 相交」的块，其余块经
//      高亮结果缓存（语言+文本 → token 序列）命中复用；未算过的块异步补；
//      零块相交且无待算块的编辑（普通文字击键）走 DecorationSet.map 快路径
//   3. 批次经 tr.setMeta(pluginKey) 合入 state；文档变化以 epoch 计数作废在途批次
//   4. 语言可用性判定：上游额外查 highlight.js core 的 getLanguage（供手动
//      register 场景），本项目 lowlight 为自建实例，listLanguages/registered 足够
//
// 上游同步：升级依赖后比对 node_modules/@tiptap/extension-code-block-lowlight/
// src/lowlight-plugin.ts，重点核对 apply 触发条件与 getDecorations 语义。
// =============================================================================
(function() {
    'use strict';

    // 高亮 token 的相对区间（块内偏移，避免 Decoration 绝对位置进缓存）
    // 缓存 key: language + '\x00' + text；命中免重跑 lowlight.highlight
    var HIGHLIGHT_CACHE_LIMIT = 256;
    var _hlCache = new Map();   // key -> [{ len, cls }]

    // 空闲批次大小：单块 ~8-30ms，6 块保证单片不超预算太多、dispatch 粒度适中
    var BATCH_SIZE = 6;

    // CM6 Work 风格预算常量组（CodeMirror 6 @codemirror/language 的 parse work
    // 调度思想：单片时间预算 + 片间停顿让路输入/渲染；单位均 ms）
    var CHUNK_BUDGET_MS = 50;       // 单片高亮预算：片内 performance.now() 超此即止（保底 1 块）
    var CHUNK_GAP_MS = 100;         // 片间最小停顿：让路输入/渲染
    var TOTAL_BUDGET_MS = 3000;     // 单轮补全总预算：超限降级为纯文档顺序 2 块/片继续
                                     //（视口/光标邻域优先有其调度成本，且此时可视区早已
                                     // 着色——远块保底推进，永不饿死）
    var DEGRADED_CHUNK_BLOCKS = 2;  // 降级模式每片块数（纯文档顺序）
    var CURSOR_RADIUS = 2000;       // 光标邻域半径（文档位置字符数：|块 pos - 选头| ≤ 此值）

    // view() 钩子登记的当前 EditorView（批次 dispatch 需要；destroy 清空）
    var _view = null;
    // 有在途调度（空闲起步或片间停顿中；防重复调度）
    var _ticking = false;

    // 视口判定注入钩子（测试用）：null = production 默认（块元素矩形与视口求交）
    var _viewportProbe = null;
    // 轮内视口判定缓存：块 pos -> 是否可视。同一轮内复用，避免每片对全部待算
    // 块重复 getBoundingClientRect 强制布局；滚动位置变化即整体失效（读滚动是
    // 廉价状态读，不触发布局）
    var _viewVerdicts = new Map();
    var _viewCacheScrollKey = null;
    // 当前补全轮状态（轮 = 同一文档代次内一批 pending 从出现到排干）
    var _roundActive = false;
    var _roundStart = 0;
    var _roundEpoch = -1;
    var _roundDoc = null;

    // performance.now 优先（单调时钟），缺失环境回退 Date.now
    var _now = function() {
        return (window.performance && typeof window.performance.now === 'function')
            ? window.performance.now() : Date.now();
    };

    var _ric = function(fn) {
        if (window.requestIdleCallback) return window.requestIdleCallback(fn);
        return window.setTimeout(fn, 16);
    };

    // isInputPending 静默降级：QWebEngine 各版本 navigator.scheduling 可能缺失，
    // 读不到一律视为无输入挂起（不让路），行为回退到固定片间停顿
    function _inputPending() {
        try {
            var sch = window.navigator && window.navigator.scheduling;
            return !!(sch && typeof sch.isInputPending === 'function'
                && sch.isInputPending());
        } catch (e) { return false; }
    }

    // 视口判定（production 默认：块元素矩形与窗口视口求交；keep-warm 的
    // visibility:hidden DOM 布局仍有效，矩形可用；未挂载/零矩形判不在视口）
    function _blockInView(el) {
        if (_viewportProbe) return !!_viewportProbe(el);
        var r = el.getBoundingClientRect();
        var vh = (typeof window.innerHeight === 'number') ? window.innerHeight : 0;
        return r.bottom > 0 && r.top < vh;
    }

    // 滚动指纹：窗口或编辑器容器滚动变化即作废轮内视口缓存
    function _scrollKey(view) {
        var domScroll = (view && view.dom && view.dom.scrollTop) ? view.dom.scrollTop : 0;
        return (window.scrollY || 0) + '|' + domScroll;
    }

    // 取块 DOM 元素（view.nodeDOM(pos) = 块节点对应元素，与 domAtPos 取块等价
    // 但直接返回节点本身；NodeView 场景返回 nodeview 的顶层 DOM）
    function _blockDom(view, pos) {
        try {
            var el = view.nodeDOM(pos);
            return (el && el.getBoundingClientRect) ? el : null;
        } catch (e) { return null; }
    }

    // pending 稳定排序：可视区内块(0) → 光标邻域块(1) → 其余(2)；
    // 同优先级按 pos 升序（确定性，pending 本身按文档序收集，显式次键兜底）
    function _orderPending(view, pending) {
        var key = _scrollKey(view);
        if (key !== _viewCacheScrollKey) {
            _viewVerdicts.clear();
            _viewCacheScrollKey = key;
        }
        var head = view.state.selection.head;
        var items = [];
        for (var i = 0; i < pending.length; i++) {
            var p = pending[i];
            var rank = 2;
            if (!_viewVerdicts.has(p)) {
                var el = _blockDom(view, p);
                _viewVerdicts.set(p, !!(el && _blockInView(el)));
            }
            if (_viewVerdicts.get(p)) rank = 0;
            else if (Math.abs(p - head) <= CURSOR_RADIUS) rank = 1;
            items.push({ pos: p, rank: rank });
        }
        items.sort(function(a, b) { return a.rank - b.rank || a.pos - b.pos; });
        return items.map(function(it) { return it.pos; });
    }

    // 轮结束/重置：清轮状态与视口缓存（新轮重新探视口）
    function _endRound() {
        _roundActive = false;
        _roundStart = 0;
        _roundEpoch = -1;
        _roundDoc = null;
        _viewVerdicts.clear();
        _viewCacheScrollKey = null;
    }

    // 上游 parseNodes：highlight.js AST 展平为 {text, classes} 序列
    function parseNodes(nodes, className) {
        var out = [];
        for (var i = 0; i < nodes.length; i++) {
            var node = nodes[i];
            var classes = (className || []).concat(
                node.properties ? (node.properties.className || []) : []);
            if (node.children) {
                out = out.concat(parseNodes(node.children, classes));
            } else {
                out.push({ text: node.value, classes: classes });
            }
        }
        return out;
    }

    // 上游 getHighlightNodes：lowlight v1 用 .value，v2/v3 用 .children
    function getHighlightNodes(result) {
        return result.value || result.children || [];
    }

    // 单块高亮（走缓存）：返回相对区间 [{ len, cls }]，无类的纯文本省略
    function highlightBlock(node, name, lowlight, defaultLanguage) {
        var language = node.attrs.language || defaultLanguage;
        var cacheKey = (language || '?auto') + '\x00' + node.textContent;
        if (_hlCache.has(cacheKey)) {
            var hit = _hlCache.get(cacheKey);
            _hlCache.delete(cacheKey);   // LRU：提到末尾（最新）
            _hlCache.set(cacheKey, hit);
            return hit;
        }
        var languages = lowlight.listLanguages();
        var result = (language &&
                (languages.indexOf(language) >= 0 ||
                    (lowlight.registered && lowlight.registered(language))))
            ? lowlight.highlight(language, node.textContent)
            : lowlight.highlightAuto(node.textContent);
        var rel = [];
        var parsed = parseNodes(getHighlightNodes(result));
        for (var i = 0; i < parsed.length; i++) {
            var t = parsed[i];
            if (t.classes.length) rel.push({ len: t.text.length, cls: t.classes.join(' ') });
            else rel.push({ len: t.text.length, cls: '' });   // 占位保持相对偏移推进
        }
        if (_hlCache.size >= HIGHLIGHT_CACHE_LIMIT) {
            _hlCache.delete(_hlCache.keys().next().value);   // 淘汰最旧
        }
        _hlCache.set(cacheKey, rel);
        return rel;
    }

    // 相对区间 → 内联 Decoration（绝对位置，from 从块内容起点 pos+1 起推进）
    function buildDecos(pos, rel) {
        var decos = [];
        var from = pos + 1;
        for (var i = 0; i < rel.length; i++) {
            var to = from + rel[i].len;
            if (rel[i].cls) {
                decos.push(window.ProseMirrorView.Decoration.inline(from, to, {
                    class: rel[i].cls
                }));
            }
            from = to;
        }
        return decos;
    }

    // 收集文档中全部代码块 [{ pos, node }]
    function findBlocks(doc, name) {
        var blocks = [];
        doc.descendants(function(node, pos) {
            if (node.type.name === name) {
                blocks.push({ pos: pos, node: node });
                return false;   // 原子级深入无意义（代码块内容无嵌套块）
            }
            return true;
        });
        return blocks;
    }

    // PluginKey 单例：setContent 等场景销毁重建编辑器时 extension 会重新调
    // AsyncLowlightPlugin()，若每次 new key，在途批次闭包里的旧 key 在新编辑器
    // state 上 getState 返回 undefined，批次链路断裂。同一时刻只有一个编辑器，
    // key 全局唯一即可
    var _pluginKey = null;

    // 调度下一片批次（apply 批次合并后 pending 非空 / view() 启动时调用）：
    //   轮首片经空闲回调起步（不与加载/渲染抢跑，保持既有语义）；
    //   轮内后续片走片间停顿 setTimeout(CHUNK_GAP_MS)，isInputPending 为真时
    //   再让一片（无该 API 则跳过检查——保底降级，见 _inputPending）
    function scheduleTick(pluginKey) {
        if (_ticking || !_view) return;
        _ticking = true;
        if (_roundActive) {
            setTimeout(function gapTick() {
                if (_inputPending()) { setTimeout(gapTick, CHUNK_GAP_MS); return; }
                _ticking = false;
                runChunk(pluginKey);
            }, CHUNK_GAP_MS);
        } else {
            _ric(function() {
                _ticking = false;
                runChunk(pluginKey);
            });
        }
    }

    // 执行一片：视口优先排序取块 → 预算内高亮 → 单事务合入 state
    //（apply 批次合并分支在其 remain 非空时回调 scheduleTick 接续下一片）
    function runChunk(pluginKey) {
        var view = _view;
        if (!view || (view.isDestroyed && view.isDestroyed())) return;
        var st = pluginKey.getState(view.state);
        if (!st || !st.pending.length) { _endRound(); return; }
        // 轮生命周期：跨轮（编辑器重建/文档代次变更）即开新轮并重探视口
        if (!_roundActive || st.epoch !== _roundEpoch || view.state.doc !== _roundDoc) {
            _roundActive = true;
            _roundStart = _now();
            _roundEpoch = st.epoch;
            _roundDoc = view.state.doc;
            _viewVerdicts.clear();
        }
        // 总预算超限：降级为纯文档顺序 2 块/片（跳过视口排序成本，保底推进）
        var degraded = (_now() - _roundStart) > TOTAL_BUDGET_MS;
        var ordered = degraded ? st.pending : _orderPending(view, st.pending);
        var cap = degraded ? DEGRADED_CHUNK_BLOCKS : BATCH_SIZE;
        var t0 = _now();
        var decos = [];
        var taken = [];
        for (var i = 0; i < ordered.length && taken.length < cap; i++) {
            // 预算即止：片内耗时超 CHUNK_BUDGET_MS 停止取块（保底已完成 1 块）
            if (taken.length > 0 && _now() - t0 >= CHUNK_BUDGET_MS) break;
            var pos = ordered[i];
            var node = view.state.doc.nodeAt(pos);
            taken.push(pos);   // 上报本片位置（含已删块，apply 侧据此清出 pending）
            if (!node) continue;   // 批次间隙块被删：apply 侧 epoch 已作废此批
            decos = decos.concat(buildDecos(pos,
                highlightBlock(node, st.name, st.lowlight, st.defaultLanguage)));
        }
        view.dispatch(view.state.tr.setMeta(pluginKey, {
            kind: 'batch',
            epoch: st.epoch,
            positions: taken,
            decos: decos
        }));
    }

    // 测试钩子：注入 mock 视口判定（jsdom 无真视口）；传假值恢复 production 默认
    window.__setViewportProbe = function(fn) {
        _viewportProbe = (typeof fn === 'function') ? fn : null;
    };

    // 测试钩子：只读轮状态（排干复位契约断言用；_roundActive 是模块私有，
    // 生产路径外部不可见——见 vendored-lowlight-plugin.test.js 用例 7）
    window.__highlightRoundState = function() {
        return {
            roundActive: _roundActive,
            roundEpoch: _roundEpoch,
            verdicts: _viewVerdicts.size
        };
    };

    window.AsyncLowlightPlugin = function(options) {
        var name = options.name;
        var lowlight = options.lowlight;
        var defaultLanguage = options.defaultLanguage;

        if (!['highlight', 'highlightAuto', 'listLanguages']
                .every(function(api) { return typeof lowlight[api] === 'function'; })) {
            throw Error('You should provide an instance of lowlight to use the code-block-lowlight extension');
        }

        var PMState = window.ProseMirrorState;
        var PMView = window.ProseMirrorView;
        if (!_pluginKey) _pluginKey = new PMState.PluginKey('asyncLowlight');
        var pluginKey = _pluginKey;

        var plugin = new PMState.Plugin({
            key: pluginKey,

            state: {
                // 与上游差异点 1：init 不高亮，块收集入 pending 空闲分批
                //（setContent 是新建编辑器不走 apply，块必须在 init 收集）
                init: function(_, state) {
                    // 新编辑器 = 新文档代次：结束在途补全轮（清轮状态与视口缓存）
                    _endRound();
                    var pending = findBlocks(state.doc, name).map(function(b) {
                        return b.pos;
                    });
                    return {
                        decos: PMView.DecorationSet.empty,
                        pending: pending,  // 待算块 pos
                        epoch: 0,          // 文档代次，作废在途批次
                        name: name, lowlight: lowlight, defaultLanguage: defaultLanguage
                    };
                },

                apply: function(tr, state) {
                    var meta = tr.getMeta(pluginKey);

                    if (meta && meta.kind === 'batch') {
                        // 在途批次合并（epoch 不匹配说明期间文档已变，丢弃）
                        if (meta.epoch !== state.epoch) return state;
                        var remain = state.pending.filter(function(p) {
                            return meta.positions.indexOf(p) < 0;
                        });
                        var next = {
                            decos: meta.decos.length
                                ? state.decos.add(tr.doc, meta.decos)
                                : state.decos,
                            pending: remain,
                            epoch: state.epoch,
                            name: state.name, lowlight: state.lowlight,
                            defaultLanguage: state.defaultLanguage
                        };
                        if (remain.length) scheduleTick(pluginKey);
                        else _endRound();   // 排干即收轮：scheduleTick 以 _roundActive 分通道（轮首片空闲回调 / 轮内片间停顿），不复位则下一轮首片误走片间停顿
                        return next;
                    }

                    if (tr.docChanged) {
                        // 差异点 2：只同步重算与 step 相交的块；其余缓存命中或入 pending
                        var steps = tr.steps;
                        var blocks = findBlocks(tr.doc, name);
                        // 先判定零块相交（普通文字击键——step 与全部块无交集），
                        // 命中则走 map 快路径，跳过逐块缓存键拼接与全量重建
                        var touchedPos = [];
                        for (var i = 0; i < blocks.length; i++) {
                            var b = blocks[i];
                            var bFrom = b.pos, bTo = b.pos + b.node.nodeSize;
                            for (var s = 0; s < steps.length; s++) {
                                var st = steps[s];
                                if (st.from === undefined || st.to === undefined) continue;
                                // 部分相交即受影响（块内文本编辑 / 属性变更 / 整块替换）
                                if (st.from < bTo && st.to > bFrom) {
                                    touchedPos.push(b.pos);
                                    break;
                                }
                            }
                        }
                        if (touchedPos.length === 0 && state.pending.length === 0) {
                            // 快路径：无块相交且无待算块——既有装饰随事务平移（借空事务分支先例）
                            // pending 为空是前提：pending 块的 pos 是旧 doc 坐标，需重定位，
                            // 有 pending 时走慢路径（findBlocks 已重收集）保证正确性
                            return {
                                decos: state.decos.map(tr.mapping, tr.doc),
                                pending: [],
                                epoch: state.epoch,
                                name: name, lowlight: lowlight,
                                defaultLanguage: defaultLanguage
                            };
                        }
                        var decos = [];
                        var pending = [];
                        for (var i = 0; i < blocks.length; i++) {
                            var b = blocks[i];
                            var bFrom = b.pos, bTo = b.pos + b.node.nodeSize;
                            var touched = false;
                            for (var s = 0; s < steps.length; s++) {
                                var st = steps[s];
                                if (st.from === undefined || st.to === undefined) continue;
                                // 部分相交即受影响（块内文本编辑 / 属性变更 / 整块替换）
                                if (st.from < bTo && st.to > bFrom) { touched = true; break; }
                            }
                            if (!touched) {
                                // 未受影响：缓存命中（绝大多数块）→ 直接复用
                                var key = (b.node.attrs.language || defaultLanguage || '?auto')
                                    + '\x00' + b.node.textContent;
                                if (_hlCache.has(key)) {
                                    decos = decos.concat(buildDecos(b.pos, _hlCache.get(key)));
                                } else {
                                    pending.push(b.pos);   // 尚未异步算到：继续等空闲批次
                                }
                                continue;
                            }
                            // 受影响：同步重算（缓存更新在其中完成）
                            decos = decos.concat(buildDecos(b.pos,
                                highlightBlock(b.node, name, lowlight, defaultLanguage)));
                        }
                        return {
                            decos: PMView.DecorationSet.create(tr.doc, decos),
                            pending: pending,
                            epoch: state.epoch + 1,   // 作废在途批次
                            name: name, lowlight: lowlight,
                            defaultLanguage: defaultLanguage
                        };
                    }

                    return {
                        decos: state.decos.map(tr.mapping, tr.doc),
                        pending: state.pending,   // 空事务 pos 不变
                        epoch: state.epoch,
                        name: name, lowlight: lowlight,
                        defaultLanguage: defaultLanguage
                    };
                }
            },

            view: function(view) {
                        _view = view;
                scheduleTick(pluginKey);
                return function() {
                    if (_view === view) _view = null;
                };
            },

            props: {
                decorations: function(state) {
                    var st = pluginKey.getState(state);
                    return st ? st.decos : null;
                }
            }
        });

        return plugin;
    };
})();
