// =============================================================================
// pdf-native-paginate.js — nativeHf 轻量分页器 + 逐页脚注规划（PDF 快路径）
//
// 迁自 PdfExporter.cpp 内嵌 JS（breakAt/pinOl/preSplit/BLK/__paginateNativeHf
// 原样迁出，行为不变），并新增脚注快路径（2026-10-13）：
//   · __planFootnotes：探针语境批量测脚注定义高 → 摘除被引用 def → 引用点
//     批量测 rect（顺序铁律：__fixTableColumns 之后、__paginateNativeHf 之前
//     ——def 内的表格也要列宽归一，且摘除 def 后下游上移、引用 rect 必须在
//     摘除后的稳定流里测）
//   · planPages：纯规划器（无 DOM，vitest 直调）——单遍前向贪心：放置单元时
//     先并入其首次出现的 def（脚注与引用同页硬约束）再判容量，放不下则撤回
//     def 随单元推到下页；文档序单调、天然零振荡，无需不动点迭代
//   · 逐页预算 = 版心高 − 表续页重复表头高 − 该页脚注区高；无解仅两类
//     （单 def+分隔线超整页 / 页首单元连同自身 def 超整页）→ __hfFnTall，
//     C++ 侧提前回退 paged.js（慢但正确）
//   · 引用归属页 = ref.top 对页顶边界表二分（覆盖被推单元与 natCap 跨页
//     不可断单元两种情形）；墨迹越线页 × 脚注区同页 → __hfFnStraddle 整类
//     回退（重叠与归属都不可靠，不赌边界）
// 盖章（.hf-fn 覆盖层）在 PdfExporter.cpp 的 __stampHf；排版常量与探针
// （.hf-fn-probe）由模板 CSS 同源下发——测量语境必须与盖章语境同款，否则
// 测得的高度对不上盖章实际渲染高度（.85em 容器 ≠ .8em 脚注区）。
// =============================================================================
(function () {
    'use strict';

    // 就地拆 li：断点落在 li 内部时，把该 li 从断点处一分为二，后半段成为
    // 新 li（无符号、挂 break-before:page）。ol 的序号靠预钉的 data-hf-n
    // 在新增 li 后回填 value 保持
    var breakAt = function (node) {
        var li = node && node.closest ? node.closest('li') : null;
        if (!li || node.tagName === 'LI') { node.style.breakBefore = 'page'; return; }
        var anchor = node, guard = 0;
        while (anchor.parentElement && anchor.parentElement !== li && guard++ < 64)
            anchor = anchor.parentElement;
        if (anchor.parentElement !== li) { node.style.breakBefore = 'page'; return; }
        if (anchor === li.firstElementChild) { li.style.breakBefore = 'page'; return; }
        var cont = document.createElement('li');
        cont.style.listStyle = 'none';
        cont.style.breakBefore = 'page';
        var par = li.parentNode, moved = [];
        for (var sn = anchor; sn; sn = sn.nextSibling) moved.push(sn);
        for (var mi = 0; mi < moved.length; mi++) cont.appendChild(moved[mi]);
        par.insertBefore(cont, li.nextSibling);
        if (par.tagName === 'OL') {
            for (var nx = cont.nextElementSibling; nx; nx = nx.nextElementSibling)
                if (nx.tagName === 'LI' && nx.getAttribute('data-hf-n')
                    && !nx.hasAttribute('value')) nx.value = nx.getAttribute('data-hf-n');
        }
    };

    // ol 序号预钉：拆分前记录每个 li 的当前序号
    var pinOl = function (root) {
        var ols = root.querySelectorAll('ol');
        for (var i = 0; i < ols.length; i++) {
            var ol = ols[i];
            var n = parseInt(ol.getAttribute('start') || '1', 10) || 1;
            for (var j = 0; j < ol.children.length; j++) {
                var li = ol.children[j];
                if (li.tagName !== 'LI') continue;
                if (li.hasAttribute('value')) n = parseInt(li.getAttribute('value'), 10) || n;
                li.setAttribute('data-hf-n', n);
                n++;
            }
        }
    };

    // 预拆分（先拆后测）：把每个 li 按块级子元素边界拆成若干 li（续段无符号、
    // 不改变缩进与间距），于是 li 内部任意块边界都变成 Chromium 可靠执行的
    // 断点；必须在测量前完成，保证"测什么就印什么"。
    // 【暂缓启用（迁出前的原状保留）】：屏幕模拟与打印成品尚有 ~6mm 系统偏差，
    // 做成布局中性后再启用——见 CHANGELOG 0.18.0「已知边界」
    var PRE_SPLIT_ENABLED = false;
    var preSplit = function (root) {
        var lis = root.querySelectorAll('li');
        for (var i = 0; i < lis.length; i++) {
            var li = lis[i];
            if (li.closest('ul[data-type="taskList"]')) continue;
            var par = li.parentNode, cur = li;
            var kids = Array.prototype.slice.call(li.children);
            for (var j = 1; j < kids.length; j++) {
                var k = kids[j];
                if (!BLK[k.tagName]) continue;
                var cont = document.createElement('li');
                cont.style.listStyle = 'none';
                var moved = [];
                for (var sn = k; sn; sn = sn.nextSibling) moved.push(sn);
                for (var mi = 0; mi < moved.length; mi++) cont.appendChild(moved[mi]);
                par.insertBefore(cont, cur.nextSibling);
                cur = cont;
            }
        }
        // 新增 li 会顶掉后续项编号：按预钉序号回填 value
        var ols = root.querySelectorAll('ol');
        for (var oi = 0; oi < ols.length; oi++) {
            var ols2 = ols[oi];
            for (var oj = 0; oj < ols2.children.length; oj++) {
                var l2 = ols2.children[oj];
                if (l2.tagName !== 'LI') continue;
                var n2 = l2.getAttribute('data-hf-n');
                if (n2 && !l2.hasAttribute('value')) l2.value = n2;
            }
        }
    };

    // 块级标签白名单：判定容器是否继续下钻（preSplit 与单元收集共用）。列表项
    // 里可能装着整棵嵌套子树——只按 li 一层收集会让「6 级嵌套列表」的第一项
    // 成为不可分巨块，整块被推到下一页、前一页留大片空白
    var BLK = { P: 1, UL: 1, OL: 1, LI: 1, PRE: 1, TABLE: 1, BLOCKQUOTE: 1,
                DIV: 1, HR: 1, H1: 1, H2: 1, H3: 1, H4: 1, H5: 1, H6: 1, FIGURE: 1 };

    // =========================================================================
    // 纯规划器：给定单元几何与脚注高度表，算出页划分与逐页脚注规划。
    // 无 DOM 依赖（vitest 直调）。
    //
    // units: [{node, top, h, thH, canBreak, refs:[id…]}]（top/h 为屏幕流坐标）
    // refs:  [{id, top}] 按 top 升序（引用点流坐标；id 可重复——同 def 多引用）
    // defH:  {id: 高px}（含 def 自身 margin-top；探针语境测得）
    // sepH:  分隔线+内垫高（.hf-fn 的 border-top+padding-top，一次一页）
    // Hpx:   版心预算高；natCap: 自然页容量（Hpx+下边距）
    // =========================================================================
    function planPages(units, refs, defH, sepH, Hpx, natCap) {
        var pages = [], pageTops = [];
        var seen = {};                 // 已随当前页预留的 def id（回滚可撤）
        var tall = 0, fnTall = 0, fnStraddle = 0, breaks = 0, natSteps = 0;
        var fills = [], lastBottom = 0;
        var breakUnits = [];           // 开新页的单元（贪心断点，需 breakAt）

        function newPage(top, th0, opener) {
            var p = { top: top, th0: th0 || 0, fnIds: [], fnH: 0, hasFn: false, over: false };
            pages.push(p);
            pageTops.push(top);
            if (opener) { breakUnits.push(opener); breaks++; }
            return p;
        }
        var cur = units.length ? newPage(units[0].top, 0, null) : newPage(0, 0, null);

        // 并入 u 首次出现的 def（脚注与引用同页硬约束：先并再判容量）。
        // 分隔线只在该页第一条 def 落位时计一次（sepH 一次性）
        function addDefs(u) {
            var ids = [], h = 0, opened = false;
            for (var r = 0; r < u.refs.length; r++) {
                var id = u.refs[r];
                if (seen[id] || !(defH[id] > 0)) continue;   // 悬空引用（无 def）忽略
                seen[id] = 1;
                ids.push(id);
                h += defH[id];
            }
            if (ids.length) {
                if (!cur.hasFn) { cur.hasFn = true; opened = true; h += sepH; }
                cur.fnIds = cur.fnIds.concat(ids);
                cur.fnH += h;
            }
            return { ids: ids, h: h, opened: opened };
        }
        // 撤回（单元放不下推下页时，def 随单元走——分隔线若因此撤空一并回退）
        function undoDefs(a) {
            if (!a.ids.length) return;
            cur.fnH -= a.h;
            cur.fnIds.length -= a.ids.length;
            for (var i = 0; i < a.ids.length; i++) delete seen[a.ids[i]];
            if (!cur.fnIds.length) cur.hasFn = false;
        }

        for (var i = 0; i < units.length; i++) {
            var u = units[i], top = u.top, h = u.h;
            if (h > Hpx) tall++;
            if (!u.canBreak) {
                // 不可断单元：自然分页模拟——其末尾跨过自然页界时，把页游标
                // 同步到该自然页起点（否则后续断点全部漂移）。跨页区间内每一
                // 个内容线（页顶+Hpx）被穿越的页都记 over（墨迹越线）
                var np = cur.top;
                while (top + h > np + natCap) {
                    np += natCap;
                    fills.push(Math.round(lastBottom - cur.top));
                    cur = newPage(np, 0, null);
                    natSteps++;
                }
                addDefs(u);   // 保守并入末端页；跨页含引用时由 straddle 防线兜底
                var p0 = pageIndexOf(pageTops, top);
                var p1 = pageIndexOf(pageTops, top + Math.max(h - 1, 0));
                for (var pi = p0; pi <= p1 && pi < pages.length; pi++)
                    if (top + h > pages[pi].top + Hpx) pages[pi].over = true;
            } else {
                var a = addDefs(u);   // 先并 def 再判容量
                var budget = Hpx - cur.th0 - cur.fnH;
                if (top - cur.top > 0 && (top - cur.top) + h > budget) {
                    // 放不下（含自身新 def）→ 推下页：撤回刚并入的 def 随单元走
                    undoDefs(a);
                    fills.push(Math.round(lastBottom - cur.top));
                    cur = newPage(top, u.thH, u.node);
                    a = addDefs(u);   // 新页重挂（分隔线计入新页）
                }
                // 已在页首的块不再推（重复 break-before 会插白页）；页首单元
                // 连同自身脚注仍超整页 → 无解，交 C++ 回退
                if (top - cur.top <= 0 && h + cur.fnH + cur.th0 > Hpx) fnTall++;
            }
            lastBottom = top + h;
        }
        fills.push(Math.round(lastBottom - cur.top));

        // 引用归属页：ref.top 对页顶边界表二分（首现优先，同 def 多引用只随
        // 首引页——Word/DOCX 同款一脚注一锚点）。覆盖被推单元与 natCap 跨页
        // 单元内部两种情形
        var fnPlan = {}, fnCount = 0, assigned = {};
        for (var ri = 0; ri < refs.length; ri++) {
            var rid = refs[ri].id;
            if (rid === null || assigned[rid] || !(defH[rid] > 0)) continue;
            assigned[rid] = 1;
            var pg = pageIndexOf(pageTops, refs[ri].top);
            if (!fnPlan[pg + 1]) fnPlan[pg + 1] = [];
            fnPlan[pg + 1].push(rid);
            fnCount++;
        }
        // 冲突页：墨迹越线（over）× 脚注区同页 → 重叠与归属都不可靠，整类回退
        for (var pj = 0; pj < pages.length; pj++)
            if (pages[pj].over && fnPlan[pj + 1] && fnPlan[pj + 1].length) fnStraddle++;

        return { pages: pages, pageTops: pageTops, fnPlan: fnPlan, fnCount: fnCount,
                 fnTall: fnTall, fnStraddle: fnStraddle, fills: fills, breaks: breaks,
                 breakUnits: breakUnits, tall: tall, natSteps: natSteps };
    }

    // 最后一个 ≤ y 的页顶下标（pageTops 升序）
    function pageIndexOf(pageTops, y) {
        var lo = 0, hi = pageTops.length - 1, ans = 0;
        while (lo <= hi) {
            var mid = (lo + hi) >> 1;
            if (pageTops[mid] <= y) { ans = mid; lo = mid + 1; } else { hi = mid - 1; }
        }
        return ans;
    }

    globalThis.__planPages = planPages;   // vitest 直调

    // =========================================================================
    // 脚注规划前置（__fixTableColumns 之后、__paginateNativeHf 之前调用）：
    // 探针语境测 def 高 → 快照文本袋 → 摘除被引用 def → 引用点批量测 rect
    // =========================================================================
    globalThis.__planFootnotes = function () {
        var cell = document.getElementById('hfcell');
        if (!cell) { window.__hfFnReady = false; return 'no-cell'; }
        var defEls = document.querySelectorAll('div[data-type="footnoteDef"]');
        window.__hfFnDefs = null;
        window.__hfFnHeights = null;
        window.__hfFnSepH = 0;
        window.__hfRefs = null;
        window.__hfFnSig = '';
        if (!defEls.length) { window.__hfFnReady = true; return 'no-fns'; }

        // ① 探针宿主测量：hfcell 流尾追加 .hf-fn-probe（与盖章 .hf-fn 同款
        // 排版——单一来源 CSS；visibility:hidden 不上屏但参与布局），克隆
        // 全体 def 入内，一次回流后批量读高
        var probe = document.createElement('div');
        probe.className = 'hf-fn-probe';
        cell.appendChild(probe);
        var ids = [];
        for (var i = 0; i < defEls.length; i++) {
            var id = defEls[i].getAttribute('data-id');
            if (id === null) id = String(i);   // 无 data-id 的 def 按序号作键
            probe.appendChild(defEls[i].cloneNode(true));
            ids.push(id);
        }
        var defH = {};
        var pcs = getComputedStyle(probe);
        var sepH = (parseFloat(pcs.borderTopWidth) || 0) + (parseFloat(pcs.paddingTop) || 0);
        var clones = probe.children;
        for (var j = 0; j < clones.length; j++) {
            // def 间距用仅 margin-top 模型（CSS 下发）：不塌陷、逐页求和可加
            var mt = parseFloat(getComputedStyle(clones[j]).marginTop) || 0;
            defH[ids[j]] = Math.ceil(clones[j].getBoundingClientRect().height + mt);
        }
        probe.remove();

        // ② 全体 def 文本字符多重集快照（盖章后自检基线：无引用 def 留流 +
        // 被引用 def 克隆进片 ⇒ 总袋守恒）
        var bagText = '';
        for (var k = 0; k < defEls.length; k++) bagText += defEls[k].textContent;
        window.__hfFnSig = bagText.replace(/\s+/g, '').split('').sort().join('');

        // ③ 摘除被引用的 def（detach 保活供盖章克隆）。只摘被引用的——无引用
        // def 留在文末容器（保留容器形态，同 paged 侧 __floatFootnotes 契约），
        // 容器清空且无残余文本才整体移除
        var stash = {};
        var sups = document.querySelectorAll('sup[data-type="footnoteRef"]');
        for (var s = 0; s < sups.length; s++) {
            var fid = sups[s].getAttribute('data-id');
            if (fid === null || stash[fid]) continue;
            for (var d = 0; d < defEls.length; d++) {
                if (defEls[d].getAttribute('data-id') === fid) { stash[fid] = defEls[d]; break; }
            }
        }
        var cited = 0;
        for (var key in stash) { stash[key].remove(); cited++; }
        var cont = document.querySelector('div[data-type="footnoteContainer"]');
        if (cont && !cont.querySelector('div[data-type="footnoteDef"]')
            && !cont.textContent.trim()) cont.remove();
        window.__hfFnDefs = stash;

        // ④ 引用点批量测 rect（摘除后的稳定流——容器在文档中部时摘除使下游
        // 上移，摘除前测的坐标全作废）
        var refs = [];
        for (var s2 = 0; s2 < sups.length; s2++) {
            var r3 = sups[s2].getBoundingClientRect();
            refs.push({ id: sups[s2].getAttribute('data-id'), top: r3.top + window.scrollY });
        }
        refs.sort(function (a, b) { return a.top - b.top; });
        window.__hfRefs = refs;
        window.__hfFnHeights = defH;
        window.__hfFnSepH = sepH;
        window.__hfFnReady = true;
        return 'ok defs=' + defEls.length + ' cited=' + cited;
    };

    // =========================================================================
    // 轻量分页（签名与产出兼容迁出前实现；C++ 调用点零改动）：
    // 批量测单元 rect → planPages 纯算术规划 → 给页首块打 break-before:page。
    // 测量全部在未变异 DOM 上一次批量读；切页判定见 planPages 注释
    // =========================================================================
    globalThis.__paginateNativeHf = function (Hpx, MbPx) {
        var cell = document.getElementById('hfcell');
        if (!cell || !(Hpx > 0)) return 'no-cell';
        pinOl(cell);
        if (PRE_SPLIT_ENABLED) preSplit(cell);
        // 脚注规划数据（__planFootnotes 未跑或失败 → 按无脚注规划，C++ 侧由
        // fnReady/fnCount 门控回退）
        var fnReady = !!window.__hfFnReady;
        var defH = (fnReady && window.__hfFnHeights) ? window.__hfFnHeights : {};
        var sepH = fnReady ? (window.__hfFnSepH || 0) : 0;
        var refs = (fnReady && window.__hfRefs) ? window.__hfRefs : [];

        var units = [];
        // 单元 = {node 断点节点, top, h, thH 重复表头高, canBreak, refs 引用 id 集}。
        // 探针实测 break-before:page 只在块边界被可靠执行：顶层块、表格行、
        // **li（任意层级）**；li 内部的非 li 元素会被忽略。故 li 内部的断点
        // 一律"就地拆 li"（见 breakAt）。refs 挂在**被测元素**（refsEl）上——
        // keepNode 分支的断点节点是容器，其引用集会捞到不属于本单元的引用
        var add = function (node, top, h, thH, canBreak, refsEl) {
            var ids = [];
            if (refsEl && refsEl.querySelectorAll) {
                var ss = refsEl.querySelectorAll('sup[data-type="footnoteRef"]');
                for (var q = 0; q < ss.length; q++) {
                    var id = ss[q].getAttribute('data-id');
                    if (id !== null) ids.push(id);
                }
            }
            units.push({ node: node, top: top, h: h, thH: thH || 0,
                         canBreak: canBreak ? 1 : 0, refs: ids });
        };
        var hasBlk = function (el) {
            for (var i = 0; i < el.children.length; i++)
                if (BLK[el.children[i].tagName]) return true;
            return false;
        };
        // 叶块收集：表格按行（续页扣重复表头）、代码块按 .pl 行块、可断块
        // 直接成单元；容器（列表/引文/含块级子元素的 div）继续下钻
        var leaf = function (el, safe) {
            var t = el.tagName, r = el.getBoundingClientRect();
            var top = r.top + window.scrollY, h = r.height;
            if (t === 'TABLE') {
                var th = el.querySelector(':scope > thead');
                var thH = th ? th.getBoundingClientRect().height : 0;
                var body = el.querySelector(':scope > tbody') || el;
                var rows = body.children, seen = 0;
                for (var j = 0; j < rows.length; j++) {
                    if (rows[j].tagName !== 'TR') continue;
                    var rr = rows[j].getBoundingClientRect();
                    add(rows[j], rr.top + window.scrollY, rr.height,
                        seen === 0 ? 0 : thH, safe, rows[j]);
                    seen++;
                }
                if (seen === 0) add(el, top, h, 0, safe, el);
                return;
            }
            if (t === 'PRE') {
                var ls = el.querySelectorAll('.pl');
                if (ls.length) {
                    for (var j2 = 0; j2 < ls.length; j2++) {
                        var rr2 = ls[j2].getBoundingClientRect();
                        add(ls[j2], rr2.top + window.scrollY, rr2.height, 0, safe, ls[j2]);
                    }
                    return;
                }
                add(el, top, h, 0, safe, el);
                return;
            }
            if (t === 'UL' || t === 'OL') { desc(el, el, safe); return; }
            // li 本身：任意层级都可断（探针实测），以其为首叶块的断点节点
            if (t === 'LI') { desc(el, el, true); return; }
            if (hasBlk(el) && el.children.length) { desc(el, el, false); return; }
            add(el, top, h, 0, safe, el);
        };
        // 下钻：keepNode 非空时，本容器（或其后代）的首个叶块以 keepNode 作
        // 断点——列表序号/引用条等前置标记随块走；且容器要一路穿透到叶块
        var desc = function (el, keepNode, safe) {
            var first = !!keepNode;
            for (var i = 0; i < el.children.length; i++) {
                var k = el.children[i], t = k.tagName;
                if (k.id === 'numhost' || t === 'SCRIPT' || t === 'STYLE') continue;
                if (first) {
                    first = false;
                    if (!hasBlk(k)) {
                        var rr = k.getBoundingClientRect();
                        add(keepNode, rr.top + window.scrollY, rr.height, 0, safe, k);
                        continue;
                    }
                    desc(k, keepNode, safe);
                    continue;
                }
                leaf(k, safe);
            }
        };
        // 入口：正文格的直接子块均为直接安全断点（keepNode=null）
        desc(cell, null, true);

        if (!units.length) return 'no-units';
        var natCap = Hpx + MbPx;
        var r = planPages(units, refs, defH, sepH, Hpx, natCap);
        // 规划完成后才动 DOM（breakAt 拆 li / 打 break-before）——测量坐标
        // 全部取自变异前的流，与迁出前实现一致
        for (var b = 0; b < r.breakUnits.length; b++) breakAt(r.breakUnits[b]);

        window.__hfPagTall = r.tall;
        window.__hfPages = r.pages.length;
        window.__hfFnPlan = r.fnPlan;
        window.__hfFnTall = r.fnTall;
        window.__hfFnStraddle = r.fnStraddle;
        window.__hfFnCount = r.fnCount;
        window.__hfNatSteps = r.natSteps;
        window.__hfPagInfo = 'breaks=' + r.breaks + ' units=' + units.length
            + ' tall=' + r.tall + ' H=' + Math.round(Hpx)
            + ' fills=[' + r.fills.join(',') + ']'
            + ' fnPages=' + r.pages.length + ' fnCount=' + r.fnCount
            + ' natSteps=' + r.natSteps;
        return window.__hfPagInfo;
    };
})();
