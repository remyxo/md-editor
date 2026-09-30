#!/usr/bin/env node
// =============================================================================
// mode-switch-probe.js — 模式切换三指标测量（双模式单文件）
//
// 模式判定（同一文件两种运行环境，互不干扰）：
//   1. 页面探针：本文件全文经 CDP Runtime.evaluate 注入编辑器页面后，
//      在 window 上安装 __msProbe（对 editorApi 四个入口打只测量补丁，
//      不改任何返回值/行为——探针是宿主侧注入，不入 qrc、零产品改动）。
//   2. Node 驱动：以 node 直接运行本文件（--drive 单轮测量 / --aggregate 汇总），
//      负责 Xvfb+编辑器进程生命周期、CDP 连接、时序编排与 JSON 落盘。
//
// 三指标协议（字段名被 Wave 2 Task 3/4/5 作为 A/B 判据引用，不得改名）：
//   switch_ms       切入源码(getContent 被调)→源码首帧 + 退出编排被调→WYSIWYG
//                   首帧，两段之和（不含源码停留时长）。
//                   边界说明：切入段含 C++ fromHtml 转换与 IPC；退出段是未编辑
//                   快速路径（exitSourceModeOrGetMd 单调用内完成），C++ 侧
//                   runJavaScript 派发开销（~15ms 量级）不计入，应用自身
//                   【计时】qWarning 日志作为交叉核对一并采集。
//   viewport_hl_ms  源码首帧 → 「视口矩形内全部代码块（div.code-block pre code）
//                   都含 .hljs-* 后代」的时刻；无 token 可着色的块以
//                   asyncLowlight 插件 pending 归零兜底（防悬挂）。
//                   实现背景：①源码视图是纯 textarea，hljs 节点只存在于
//                   keep-warm 隐藏保留的 WYSIWYG DOM 中（visibility:hidden
//                   保留布局，getBoundingClientRect 仍有效）；②本机实测 4k
//                   bench 空闲高亮 ~1s 跑完、且切入编排的 C++ 转换间隙里批次
//                   照样推进——故探针装「空闲闸门」把高亮批次挂起到源码首帧
//                   才释放（受控起点），否则首帧时刻高亮已完成、指标恒 ~0。
//   wrap_cache_hit  第 3 次切入源码后、enterSourceMode 同步返回时刻
//                   _srcWrapCache 是否命中当前文本（0/1）。
//                   ★ 演进点：当前实现 _srcStop 退出即清缓存，本指标恒为 0
//                   （占位基线）；Task 4 落地 window.__wrapCacheState() 后，
//                   readWrapCache() 改为优先消费该只读探针——本文件内唯一
//                   需要随 Task 4 调整的位置（搜索「演进点」）。
//
// 测量锚定：切入源码前把光标置文档末尾（源码视图随光标所在标题滚动到文末，
// 可视区代码块 = 文档尾部块，文档顺序的空闲高亮批次最后才轮到——最大化
// 「视口优先高亮」优化的 A/B 信号；详见 scripts/measure-mode-switch.sh）。
// =============================================================================

/* ===================== 第一部分：页面探针（注入编辑器页执行） ===================== */
(function () {
    if (typeof window === 'undefined' || typeof window.editorApi === 'undefined') return;

    // 幂等：重复注入不二次打补丁（补丁只包一层）
    if (window.__msProbe) { window.__msProbeInstallResult = 'already'; return; }

    var api = window.editorApi;

    var probe = {
        armed: null,          // 'enter' | 'exit' —— 待消耗的武装相位（宿主脚本设置）
        cycles: [],           // 每次「切入→切出」一轮的记录（第 1 轮带高亮测量）
        currentCycle: null,
        hl: null,             // 可视区高亮测量状态（仅 measureHl 的轮启用）
        _hlMo: null, _hlTimer: null, _hlGuard: null,

        // ---- 空闲闸门（测量起点确定性） ----
        // 背景：本机实测 4k bench 的空闲高亮 ~1s 内全部跑完，而切入源码编排
        //（fromHtml + 填充 ~0.8s）的 C++ 转换间隙里渲染线程空闲、批次照样推进
        //——不闸住则源码首帧时刻高亮已完成，viewport_hl_ms 恒为 ~0，
        // Task 3「视口优先」无 A/B 信号。
        // 做法：安装时把 requestIdleCallback 换成排队暂缓版（vendored 插件的空闲
        // 批次全部挂起）；measureHl 轮在**源码首帧**才释放（受控起点：首帧时刻
        // 批次全在途，指标测纯调度完成时间），非测量轮在 armEnter 即释放。
        _idleGate: { queued: [], released: false, seq: 0, origRic: null,
                     releasedAtMs: null, queuedAtRelease: 0 },

        installIdleGate: function () {
            var gate = this._idleGate;
            var orig = (typeof window.requestIdleCallback === 'function')
                ? window.requestIdleCallback.bind(window) : null;
            gate.origRic = orig;
            var runFn = function (fn) {
                if (gate.origRic) gate.origRic(fn);
                else setTimeout(function () {
                    fn({ didTimeout: false, timeRemaining: function () { return 50; } });
                }, 16);
            };
            window.requestIdleCallback = function (fn, opts) {
                if (gate.released) { return (gate.origRic
                    ? gate.origRic(fn, opts)
                    : setTimeout(fn, 16)); }
                gate.queued.push(fn);
                return ++gate.seq;   // 句柄仅作返回值；本页无 cancelIdleCallback 调用方
            };
            gate.runQueued = runFn;
        },
        releaseIdleGate: function () {
            var gate = this._idleGate;
            if (gate.released) return;
            gate.released = true;
            gate.releasedAtMs = performance.now();
            gate.queuedAtRelease = gate.queued.length;
            gate.pendingAtRelease = this.lowlightPending();
            var q = gate.queued.slice();
            gate.queued.length = 0;
            for (var i = 0; i < q.length; i++) gate.runQueued(q[i]);
        },

        // 折行缓存只读探测。★演进点：Task 4 落地 window.__wrapCacheState() 后，
        // 此处改为「if (typeof window.__wrapCacheState === 'function')
        // return window.__wrapCacheState();」，当前先用 _srcWrapCache 直读 +
        // DOM/textarea 对齐判定兜底（_srcWrapCache 是 editorApi 对象属性，可直接读）。
        readWrapCache: function () {
            var sv = document.getElementById('source-view');
            var c = api._srcWrapCache;
            return {
                present: !!c,
                // 命中判据：缓存存在且缓存文本与当前 textarea 全等
                hit: !!(c && sv && c.text === sv.value),
                pending: !!api._srcWrapPending,
                wrapPoints: (c && c.points) ? c.points.length : 0
            };
        },

        // vendored lowlight 插件的 pending 长度（全部待高亮块归零 = 无可再补）。
        // 找不到插件（保底降级回上游同步版）时返回 -1，不作为完成条件。
        lowlightPending: function () {
            try {
                var ed = api.editor;
                var plugins = ed.state.plugins;
                for (var i = 0; i < plugins.length; i++) {
                    var key = plugins[i].key || '';
                    if (key.indexOf('asyncLowlight') === 0) {
                        var st = plugins[i].getState(ed.state);
                        return (st && st.pending) ? st.pending.length : 0;
                    }
                }
            } catch (e) { /* 保底：读不到不算完成 */ }
            return -1;
        },

        newCycle: function () {
            var c = {
                enterStartMs: null,      // 切入编排第一步（getContent 被调）
                firstFrameMs: null,      // 源码首帧（enterSourceMode 返回后下一帧）
                firstFrameBy: null,      // 'raf' | 'timer-fallback'
                exitStartMs: null,       // 退出编排第一步（exitSourceModeOrGetMd 被调）
                exitFirstFrameMs: null,  // WYSIWYG 恢复首帧
                exitFirstFrameBy: null,
                measureHl: false,
                wrapCacheAtEnter: null   // enterSourceMode 同步返回时刻的缓存状态
            };
            this.cycles.push(c);
            return c;
        },

        armEnter: function (opts) {
            // 空闲闸门释放时机（见 installIdleGate）：measureHl 轮推迟到源码首帧
            // 才释放（受控起点——首帧时刻批次全在途，指标测的是纯调度完成时间，
            // 对 Task 3「视口优先」最敏感且方差最小）；非测量轮在武装时即释放
            if (!(opts && opts.measureHl)) this.releaseIdleGate();
            this.currentCycle = this.newCycle();
            this.currentCycle.measureHl = !!(opts && opts.measureHl);
            this.armed = 'enter';
            return true;
        },
        armExit: function () { this.armed = 'exit'; return true; },

        // 源码首帧回调（rAF 主通道；500ms 定时兜底防无帧环境悬挂）。
        // 注意两点：① 绑定调用时刻的 cycle 对象（闭包捕获），迟到的兜底定时器
        // 不会写进后续 cycle（曾致跨 cycle 误写、exit_ms 出现大负值）；
        // ② 用 performance.now() 而非 rAF 入参时间戳（帧起始时间戳可能早于
        // 排程时刻，曾致 exit_ms 出现 -15ms 伪值）。
        onEnterFirstFrameFor: function (c, by) {
            if (!c || c.firstFrameMs !== null) return;
            c.firstFrameMs = performance.now();
            c.firstFrameBy = by || 'raf';
            try { performance.mark('msw_first_frame'); } catch (e) { /* mark 失败不碍测量 */ }
            if (c.measureHl) {
                // 源码首帧才释放闸门：首帧时刻高亮批次全在途（受控起点），
                // 此后按真实空闲调度推进直到可视区块全部着色
                this.releaseIdleGate();
                this.startHlWatch(c);
            }
        },
        onExitFirstFrameFor: function (c, by) {
            if (!c || c.exitFirstFrameMs !== null) return;
            c.exitFirstFrameMs = performance.now();
            c.exitFirstFrameBy = by || 'raf';
            try { performance.mark('msw_exit_first_frame'); } catch (e) { }
        },

        // 可视区高亮监视：冻结首帧时刻视口内的代码块清单，MutationObserver +
        // 周期兜底轮询判定完成；45s 总闸强制结束（防无可着色块悬挂）。
        startHlWatch: function (cyc) {
            var self = this;
            var blocks = [];
            var nodes = document.querySelectorAll('#editor div.code-block pre code');
            var vh = window.innerHeight;
            for (var i = 0; i < nodes.length; i++) {
                var r = nodes[i].getBoundingClientRect();
                if (r.bottom > 0 && r.top < vh) blocks.push(nodes[i]);
            }
            this.hl = {
                blocks: blocks,
                firstFrameMs: cyc.firstFrameMs,
                doneMs: null, doneInfo: null
            };
            var countHl = function () {
                var n = 0;
                for (var i = 0; i < self.hl.blocks.length; i++) {
                    if (self.hl.blocks[i].querySelector('[class*="hljs"]')) n++;
                }
                return n;
            };
            var check = function () {
                if (!self.hl || self.hl.doneMs !== null) return;
                var nHl = countHl();
                var pending = self.lowlightPending();
                // 完成：视口内全部块已着色；或全部待算块已排空（无可着色块的兜底）
                var allHl = self.hl.blocks.length > 0 && nHl === self.hl.blocks.length;
                if (allHl || pending === 0) {
                    self.hl.doneMs = performance.now();
                    try { performance.mark('msw_hl_done'); } catch (e) { }
                    self.hl.doneInfo = {
                        blocksInView: self.hl.blocks.length,
                        blocksWithHljs: nHl,
                        lowlightPending: pending,
                        byPendingDrain: !allHl
                    };
                    self.stopHlWatch();
                }
            };
            check();
            if (this.hl.doneMs !== null) return;
            this._hlMo = new MutationObserver(function () { setTimeout(check, 0); });
            this._hlMo.observe(document.getElementById('editor'),
                { childList: true, subtree: true });
            this._hlTimer = setInterval(check, 150);
            this._hlGuard = setTimeout(function () {
                if (self.hl && self.hl.doneMs === null) {
                    self.hl.doneMs = performance.now();
                    self.hl.doneInfo = {
                        blocksInView: self.hl.blocks.length,
                        blocksWithHljs: countHl(),
                        lowlightPending: self.lowlightPending(),
                        timeout: true     // 总闸触发：样本仍记录但打标记
                    };
                    self.stopHlWatch();
                }
            }, 45000);
        },
        stopHlWatch: function () {
            if (this._hlMo) { try { this._hlMo.disconnect(); } catch (e) { } this._hlMo = null; }
            if (this._hlTimer) { clearInterval(this._hlTimer); this._hlTimer = null; }
            if (this._hlGuard) { clearTimeout(this._hlGuard); this._hlGuard = null; }
        },

        // 宿主脚本轮询读的快照（全部数值化，无 DOM 引用泄漏）
        state: function () {
            var self = this;
            var cycles = this.cycles.map(function (c) {
                return {
                    enterMs: (c.firstFrameMs !== null && c.enterStartMs !== null)
                        ? c.firstFrameMs - c.enterStartMs : null,
                    exitMs: (c.exitFirstFrameMs !== null && c.exitStartMs !== null)
                        ? c.exitFirstFrameMs - c.exitStartMs : null,
                    firstFrameBy: c.firstFrameBy,
                    exitFirstFrameBy: c.exitFirstFrameBy,
                    measureHl: c.measureHl,
                    wrapCacheAtEnter: c.wrapCacheAtEnter
                };
            });
            return {
                armed: this.armed,
                isSourceMode: !!api.isSourceMode,
                cycles: cycles,
                hl: this.hl ? {
                    doneMs: this.hl.doneMs,
                    elapsedMs: (this.hl.doneMs !== null)
                        ? this.hl.doneMs - this.hl.firstFrameMs : null,
                    doneInfo: this.hl.doneInfo
                } : null,
                wrapCacheNow: this.readWrapCache(),
                idleGate: {
                    released: this._idleGate.released,
                    queuedAtRelease: this._idleGate.queuedAtRelease,
                    pendingAtRelease: this._idleGate.pendingAtRelease
                },
                docSize: api.editor ? api.editor.state.doc.content.size : 0
            };
        }
    };

    // 空闲闸门先于补丁安装（vendored 插件的批次在注入前就可能排上 idle 队列）
    probe.installIdleGate();

    // ---- 只测量补丁（返回值原样透传，不改行为） ----
    // 1) getContent：切入源码编排的第一步 JS（C++ onToggleSourceMode 先调它取 HTML）
    var origGetContent = api.getContent;
    api.getContent = function () {
        if (probe.armed === 'enter') {
            probe.armed = null;
            probe.currentCycle.enterStartMs = performance.now();
            try { performance.mark('msw_enter_start'); } catch (e) { }
        }
        return origGetContent.apply(this, arguments);
    };

    // 2) enterSourceMode：填充 textarea；同步返回时刻读折行缓存（此刻
    //    _srcStartSearch 已跑完、异步折行测量必未回填——缓存若命中只可能来自
    //    跨会话保留，即 wrap_cache_hit 判据）；rAF 后记源码首帧。
    var origEnter = api.enterSourceMode;
    api.enterSourceMode = function () {
        var r = origEnter.apply(this, arguments);
        probe.currentCycle = probe.currentCycle || probe.newCycle();
        var cyc = probe.currentCycle;
        cyc.wrapCacheAtEnter = probe.readWrapCache();
        // hl 监视在 onEnterFirstFrameFor 内按 cyc.measureHl 决定是否启动
        requestAnimationFrame(function () { probe.onEnterFirstFrameFor(cyc, 'raf'); });
        setTimeout(function () { probe.onEnterFirstFrameFor(cyc, 'timer-fallback'); }, 500);
        return r;
    };

    // 3) exitSourceModeOrGetMd：退出编排的第一步 JS（未编辑走快速路径）
    var origExitOrGet = api.exitSourceModeOrGetMd;
    api.exitSourceModeOrGetMd = function () {
        if (probe.armed === 'exit') {
            probe.armed = null;
            probe.currentCycle.exitStartMs = performance.now();
            try { performance.mark('msw_exit_start'); } catch (e) { }
        }
        return origExitOrGet.apply(this, arguments);
    };

    // 4) exitSourceMode：恢复 WYSIWYG；rAF 后记恢复首帧（快速路径在 3) 内同步调到这里）
    var origExit = api.exitSourceMode;
    api.exitSourceMode = function () {
        var r = origExit.apply(this, arguments);
        var cyc = probe.currentCycle;
        requestAnimationFrame(function () { probe.onExitFirstFrameFor(cyc, 'raf'); });
        setTimeout(function () { probe.onExitFirstFrameFor(cyc, 'timer-fallback'); }, 500);
        return r;
    };

    window.__msProbe = probe;
    window.__msProbeInstallResult = 'ok';
})();

/* ===================== 第二部分：Node 驱动（node 直接运行本文件） ===================== */
if (typeof process !== 'undefined' && typeof require !== 'undefined'
    && require.main === module) {
    'use strict';
    const fs = require('fs');
    const path = require('path');
    const net = require('net');
    const { spawn, execFile } = require('child_process');

    // ---------- 常量 ----------
    const SCREEN_SPEC = '-screen 0 1600x1000x24';
    const READY_TIMEOUT_MS = 90000;      // 冷 HOME 首启（QWebEngine 建档）较慢
    const CDP_UP_TIMEOUT_MS = 45000;     // /json/list 出现 page target
    const ENTER_TIMEOUT_MS = 30000;      // 切入→源码首帧
    const HL_TIMEOUT_MS = 60000;         // 源码首帧→可视区高亮完成
    // 折行全文异步测量在大文档上可达分钟级（窗口界定二分仍是对前缀布局的
    // 反复测量，实测 bench-500 约 21s、markdown-guide 约 23s、bench-4000 远超
    // 10min）——超时只影响 wrap_measure_ms 诊断字段（记 -1）与 wrap_cache_hit
    // 在大文档上的可翻面性，不阻塞本轮；慢机可用环境变量放宽
    const WRAP_TIMEOUT_MS = parseInt(process.env.MSW_WRAP_TIMEOUT_MS || '45000', 10);
    const EXIT_TIMEOUT_MS = 30000;       // 退出→WYSIWYG 首帧
    const POLL_MS = 120;

    // X11 按键助手：python3-xlib 设焦点 + XTEST 合成 Ctrl+\（真实键盘事件路径，
    // 走 Qt QAction 快捷键 → MainWindow::onToggleSourceMode 真实编排）。
    // 无窗口管理器（裸 Xvfb）下：按 WM_CLASS 找可视的最大窗口手动 XSetInputFocus。
    const XKEY_PY = [
        'import sys',
        'try:',
        '    from Xlib import X, display as xdisplay, XK',
        '    from Xlib.ext import xtest',
        'except Exception as e:',
        '    print("python3-xlib 不可用: %r" % (e,)); sys.exit(2)',
        'd = xdisplay.Display(sys.argv[1])',
        'root = d.screen().root',
        'best = None; best_area = -1',
        'for w in root.query_tree().children:',
        '    try:',
        '        cls = w.get_wm_class()',
        '        attrs = w.get_attributes()',
        '        geom = w.get_geometry()',
        '    except Exception:',
        '        continue',
        '    if not (cls and "md-editor" in cls): continue',
        '    if attrs.map_state != X.IsViewable: continue',
        '    area = geom.width * geom.height',
        '    if area > best_area: best = w; best_area = area',
        'if best is None:',
        '    print("DISPLAY %s 上未找到可视的 md-editor 窗口" % sys.argv[1]); sys.exit(3)',
        'd.set_input_focus(best, X.RevertToPointerRoot, X.CurrentTime)',
        'd.sync()',
        'kc_ctrl = d.keysym_to_keycode(XK.XK_Control_L)',
        'kc_bs = d.keysym_to_keycode(XK.XK_backslash)',
        'if kc_bs == 0: print("Xvfb 键位表无 backslash 键码"); sys.exit(4)',
        'xtest.fake_input(d, X.KeyPress, kc_ctrl)',
        'xtest.fake_input(d, X.KeyPress, kc_bs)',
        'xtest.fake_input(d, X.KeyRelease, kc_bs)',
        'xtest.fake_input(d, X.KeyRelease, kc_ctrl)',
        'd.sync(); d.close()'
    ].join('\n');

    // ---------- 小工具 ----------
    const sleep = (ms) => new Promise(r => setTimeout(r, ms));
    function fail(msg) { throw new Error(msg); }
    function fmtErr(e) { return (e && e.message) ? e.message : String(e); }

    // 选一个空闲 TCP 端口（listen(0) 由内核分配，随即释放复用为 CDP 端口）
    function freePort() {
        return new Promise((resolve, reject) => {
            const srv = net.createServer();
            srv.listen(0, '127.0.0.1', () => {
                const p = srv.address().port;
                srv.close(() => resolve(p));
            });
            srv.on('error', reject);
        });
    }

    // ---------- CDP ----------
    class Cdp {
        constructor(ws) { this.ws = ws; this.seq = 0; this.pending = new Map(); this.closed = false;
            ws.onmessage = (ev) => {
                let msg; try { msg = JSON.parse(ev.data); } catch (e) { return; }
                if (msg.id !== undefined) {
                    const p = this.pending.get(msg.id);
                    if (p) { this.pending.delete(msg.id);
                        msg.error ? p.reject(new Error('CDP 错误: ' + JSON.stringify(msg.error)))
                                  : p.resolve(msg.result); }
                }
            };
            ws.onclose = () => { this.closed = true;
                for (const p of this.pending.values()) p.reject(new Error('CDP WebSocket 已关闭'));
                this.pending.clear(); };
            ws.onerror = () => { /* onclose 随后到，统一在那处理 */ };
        }
        send(method, params) {
            if (this.closed) return Promise.reject(new Error('CDP WebSocket 已关闭'));
            return new Promise((resolve, reject) => {
                const id = ++this.seq;
                this.pending.set(id, { resolve, reject });
                this.ws.send(JSON.stringify({ id, method, params: params || {} }));
            });
        }
        async evaluate(expression, awaitPromise) {
            const r = await this.send('Runtime.evaluate',
                { expression, returnByValue: true, awaitPromise: !!awaitPromise });
            if (r.exceptionDetails) {
                const d = r.exceptionDetails.exception && r.exceptionDetails.exception.description;
                fail('页面脚本异常: ' + (d || r.exceptionDetails.text) + ' @ ' + expression.slice(0, 120));
            }
            return r.result ? r.result.value : undefined;
        }
    }

    async function findPageTarget(port, timeoutMs) {
        const t0 = Date.now();
        for (;;) {
            try {
                const res = await fetch('http://127.0.0.1:' + port + '/json/list');
                if (res.ok) {
                    const list = await res.json();
                    const pages = list.filter(t => t.type === 'page');
                    if (pages.length) {
                        // 优先编辑器页（qrc://…/editor.html）；辨认不出就取第一个 page
                        return pages.find(t => /editor/i.test(t.url || '')) || pages[0];
                    }
                }
            } catch (e) { /* 端口未就绪，继续轮询 */ }
            if (Date.now() - t0 > timeoutMs) fail('CDP /json/list 超时：编辑器页面目标未出现');
            await sleep(250);
        }
    }

    // ---------- 子进程管理 ----------
    function killGroup(child) {
        if (!child || child.killed) return;
        try { process.kill(-child.pid, 'SIGTERM'); } catch (e) { /* 组可能已退出 */ }
        setTimeout(() => { try { process.kill(-child.pid, 'SIGKILL'); } catch (e) { } }, 2500);
    }

    // ---------- 单轮测量 ----------
    async function driveRound(opts) {
        const binary = opts.binary;
        const roundDir = path.join(opts.workdir, 'round-' + opts.round);
        fs.mkdirSync(roundDir, { recursive: true });
        const homeDir = path.join(roundDir, 'home');
        fs.mkdirSync(homeDir, { recursive: true });
        const errLogPath = path.join(roundDir, 'editor-stderr.log');
        const errFd = fs.openSync(errLogPath, 'w');

        const port = opts.port || await freePort();
        const displayNum = 20 + (port % 400);   // 端口哈希出稳定且大概率空闲的显示号
        // xvfb-run 默认把 X 授权 cookie 写到 cwd 的 ./.Xauthority（会污染仓库根目录），
        // 显式指到轮次目录，按键助手经 XAUTHORITY 连同一虚拟显示
        const authFile = path.join(roundDir, 'Xauthority');

        // 页面环境：剔除 WAYLAND_DISPLAY、强制 xcb；隔离 HOME（应用有会话恢复，
        // 每轮全新 HOME 保证轮间可比——冷档/无残留设置状态一致）
        const env = Object.assign({}, process.env, {
            QT_QPA_PLATFORM: 'xcb',
            QTWEBENGINE_REMOTE_DEBUGGING: String(port),
            HOME: homeDir
        });
        delete env.WAYLAND_DISPLAY;

        const child = spawn('xvfb-run',
            ['-n', String(displayNum), '-s', SCREEN_SPEC, '-f', authFile, binary,
             '--remote-debugging-port=' + port, opts.file],
            { env, detached: true, stdio: ['ignore', 'ignore', 'pipe'] });
        let stderrBuf = '';
        let childDied = '';
        child.stderr.setEncoding('utf8');
        child.stderr.on('data', (s) => {
            stderrBuf += s;
            try { fs.writeSync(errFd, s); } catch (e) { }
            if (stderrBuf.length > 200000) stderrBuf = stderrBuf.slice(-100000);
        });
        child.on('exit', (code, sig) => { childDied = '编辑器进程提前退出 code=' + code + ' sig=' + sig; });

        // 单循环轮询：done() 返回真值即完成；超时/进程早退即失败（无旁路看护循环）
        const pollGuard = async (done, timeoutMs, what) => {
            const t0 = Date.now();
            for (;;) {
                if (childDied) fail(childDied + '（stderr: ' + errLogPath + '）');
                const v = await done();
                if (v) return v;
                if (Date.now() - t0 > timeoutMs)
                    fail('等待超时（' + timeoutMs + 'ms）: ' + what + '（stderr: ' + errLogPath + '）');
                await sleep(POLL_MS);
            }
        };
        const probeState = () => cdp.evaluate('window.__msProbe.state()');

        let cdp = null, round = null;
        try {
            // 1. CDP 页面目标出现并连接
            const target = await findPageTarget(port, CDP_UP_TIMEOUT_MS);
            const ws = await new Promise((resolve, reject) => {
                const w = new WebSocket(target.webSocketDebuggerUrl);
                const to = setTimeout(() => reject(new Error('CDP WebSocket 连接超时')), 10000);
                w.onopen = () => { clearTimeout(to); resolve(w); };
                w.onerror = () => { clearTimeout(to); reject(new Error('CDP WebSocket 连接失败')); };
            });
            cdp = new Cdp(ws);
            await cdp.send('Runtime.enable');

            // 2. 探针尽早注入：editorApi 一存在就装上（探针 IIFE 只依赖 editorApi
            //    对象存在，不要求编辑器实例/内容）——空闲闸门必须赶在 setContent
            //    后的空闲高亮批次排上队之前，否则 settle/warmup/定位期间批次已
            //    推进，切入时刻高亮可能已全部完成（viewport_hl_ms 恒 ~0）
            await pollGuard(() => cdp.evaluate('typeof window.editorApi !== "undefined"'),
                READY_TIMEOUT_MS, 'editorApi 就绪');
            const probeSrc = fs.readFileSync(__filename, 'utf8');
            await cdp.evaluate(probeSrc);
            const installed = await cdp.evaluate('window.__msProbeInstallResult');
            if (installed !== 'ok' && installed !== 'already') fail('探针注入失败: ' + installed);

            // 3. 等内容就绪（setContent 完成；此间高亮批次被闸门挂起）
            const readyExpr = '!!(window.editorApi && window.editorApi.editor ' +
                '&& window.editorApi.editor.state ' +
                '&& window.editorApi.editor.state.doc.content.size > 2)';
            await pollGuard(() => cdp.evaluate(readyExpr), READY_TIMEOUT_MS, '文档内容就绪');
            await sleep(300);   // 布局/字体短暂稳定（固定值，轮间一致）

            // 4. 预热消费：应用对大文档会在加载 1.5s 后调 warmup()（丢弃式 getHTML，
            //    冷 JIT 数百 ms）——主动提前消费掉，避免它落进测量窗口抬高方差；
            //    副作用是 enter_ms 反映预热后稳态（与产品「预热消除首次切换停顿」
            //    的设计意图一致，报告注明）
            await cdp.evaluate(
                '(function(){ if (document.activeElement && document.activeElement.blur) ' +
                'document.activeElement.blur(); window.editorApi.warmup(); return true; })()');

            // 5. 光标置文档末尾 + 滚到底（测量锚定，见文件头注释）
            await cdp.evaluate(
                '(function(){ var ed = window.editorApi.editor; ' +
                'var size = ed.state.doc.content.size; ' +
                'ed.commands.setTextSelection(Math.max(0, size - 1)); ' +
                'window.scrollTo(0, document.documentElement.scrollHeight); return true; })()');

            // === 周期 1（ headline 指标）：切入→可视区高亮完成→切出（无搜索干扰） ===
            await cdp.evaluate('window.__msProbe.armEnter({measureHl:true})');
            await sendKey(displayNum, authFile);
            await pollGuard(async () => {
                const st = await probeState();
                return st.cycles.length >= 1 && st.cycles[0].enterMs !== null ? st : null;
            }, ENTER_TIMEOUT_MS, '切入源码首帧');

            const hl = await pollGuard(async () => {
                const st = await probeState();
                return (st.hl && st.hl.doneMs !== null) ? st.hl : null;
            }, HL_TIMEOUT_MS, '可视区高亮完成');

            await cdp.evaluate('window.__msProbe.armExit()');
            await sendKey(displayNum, authFile);
            await pollGuard(async () => {
                const st = await probeState();
                return st.cycles.length >= 1 && st.cycles[0].exitMs !== null ? st : null;
            }, EXIT_TIMEOUT_MS, '切出源码首帧（周期 1）');

            // === 周期 2：再入源码激活搜索（真实入口 startSearch → _srcStartSearch →
            //     触发全文异步折行测量），等折行缓存回填后带搜索切出 ===
            await cdp.evaluate('window.__msProbe.armEnter({measureHl:false})');
            await sendKey(displayNum, authFile);
            await pollGuard(async () => {
                const st = await probeState();
                return st.cycles.length >= 2 && st.cycles[1].enterMs !== null ? st : null;
            }, ENTER_TIMEOUT_MS, '切入源码首帧（周期 2）');

            const searchStart = Date.now();
            await cdp.evaluate('window.editorApi.startSearch(' + JSON.stringify(opts.query) +
                ', false, false, false)');
            let wrapMeasureMs = -1;
            try {
                await pollGuard(async () => {
                    const st = await probeState();
                    return st.wrapCacheNow.present ? st.wrapCacheNow : null;
                }, WRAP_TIMEOUT_MS, '折行缓存回填');
                wrapMeasureMs = Date.now() - searchStart;   // 宿主时钟，含轮询粒度（诊断字段）
            } catch (e) {
                process.stderr.write('[警告] 折行缓存未回填（搜索词可能无匹配）: ' + fmtErr(e) + '\n');
            }

            await cdp.evaluate('window.__msProbe.armExit()');
            await sendKey(displayNum, authFile);
            await pollGuard(async () => {
                const st = await probeState();
                return st.cycles.length >= 2 && st.cycles[1].exitMs !== null ? st : null;
            }, EXIT_TIMEOUT_MS, '切出源码首帧（周期 2）');

            // === 周期 3：带搜索状态第 3 次切入——enterSourceMode 返回时刻读折行缓存
            //     命中（wrap_cache_hit；当前实现退出即清缓存恒 0，Task 4 后应为 1） ===
            await cdp.evaluate('window.__msProbe.armEnter({measureHl:false})');
            await sendKey(displayNum, authFile);
            await pollGuard(async () => {
                const st = await probeState();
                return st.cycles.length >= 3 && st.cycles[2].enterMs !== null ? st : null;
            }, ENTER_TIMEOUT_MS, '切入源码首帧（周期 3）');
            const finalState = await probeState();

            // 汇总本轮：switch_ms 取周期 1（无搜索的干净往返），wrap_cache_hit 取周期 3
            const c1 = finalState.cycles[0], c3 = finalState.cycles[2];
            const enterMs = c1.enterMs, exitMs = c1.exitMs;
            round = {
                round: opts.round,
                file: opts.file,
                query: opts.query,
                port: port, display: ':' + displayNum,
                switch_ms: (enterMs !== null && exitMs !== null) ? enterMs + exitMs : null,
                viewport_hl_ms: (hl && hl.elapsedMs !== undefined) ? hl.elapsedMs : null,
                wrap_cache_hit: (c3.wrapCacheAtEnter && c3.wrapCacheAtEnter.hit) ? 1 : 0,
                breakdown: {
                    enter_ms: enterMs,
                    exit_ms: exitMs,
                    wrap_measure_ms: wrapMeasureMs,
                    first_frame_by: c1.firstFrameBy,
                    exit_first_frame_by: c1.exitFirstFrameBy,
                    cycles: finalState.cycles
                },
                detail: {
                    hl: hl ? hl.doneInfo : null,
                    idle_gate: finalState.idleGate,
                    wrap_cache_at_first_enter: c1.wrapCacheAtEnter,
                    wrap_cache_at_third_enter: c3.wrapCacheAtEnter,
                    wrap_cache_now_after_third_enter: finalState.wrapCacheNow,
                    cpp_timing: extractCppTiming(stderrBuf)
                }
            };
        } finally {
            if (cdp) { try { cdp.ws.close(); } catch (e) { } }
            killGroup(child);
            try { fs.closeSync(errFd); } catch (e) { }
        }
        // 等子进程组退净再返回（下一轮端口/显示号不撞）
        await new Promise((r) => { child.on('exit', r); setTimeout(r, 6000); });
        return round;
    }

    function extractCppTiming(stderrBuf) {
        // 应用自身【计时】qWarning（C++ QElapsedTimer 口径）——交叉核对用
        const lines = stderrBuf.split('\n').filter(l => l.indexOf('【计时】') >= 0);
        return lines.slice(-8);
    }

    async function sendKey(displayNum, authFile) {
        // 经 Xvfb 显示器直接注入（xvfb-run -n 指定显示号 + -f 指定授权文件，驱动端已知）
        await new Promise((resolve, reject) => {
            execFile('python3', ['-c', XKEY_PY, ':' + displayNum],
                { env: Object.assign({}, process.env,
                    { DISPLAY: ':' + displayNum, XAUTHORITY: authFile }) },
                (err, stdout, stderr) => {
                    if (err) reject(new Error('按键注入失败(' + err.code + '): '
                        + (stdout || '') + (stderr || '')));
                    else resolve();
                });
        });
        await sleep(60);   // 按键到 Qt 事件循环处理的一点余量
    }

    // ---------- 汇总（中位数） ----------
    function median(arr) {
        const v = arr.filter(x => typeof x === 'number' && !isNaN(x)).sort((a, b) => a - b);
        if (!v.length) return null;
        const mid = v.length >> 1;
        return (v.length % 2) ? v[mid] : (v[mid - 1] + v[mid]) / 2;
    }

    async function aggregate(opts) {
        const rounds = [];
        for (let i = 1; i <= opts.rounds; i++) {
            const p = path.join(opts.workdir, 'round-' + i + '.json');
            if (!fs.existsSync(p)) fail('缺少轮次产物: ' + p);
            rounds.push(JSON.parse(fs.readFileSync(p, 'utf8')));
        }
        const pick = (k) => rounds.map(r => r[k]);
        const metrics = {
            switch_ms: median(pick('switch_ms')),
            viewport_hl_ms: median(pick('viewport_hl_ms')),
            wrap_cache_hit: median(pick('wrap_cache_hit'))
        };
        const out = {
            // 三指标中位数置于顶层（Task 3/4/5 消费固定字段名）
            switch_ms: metrics.switch_ms,
            viewport_hl_ms: metrics.viewport_hl_ms,
            wrap_cache_hit: metrics.wrap_cache_hit,
            unit: { switch_ms: 'ms', viewport_hl_ms: 'ms', wrap_cache_hit: '0/1' },
            file: opts.file,
            rounds: opts.rounds,
            query: opts.query,
            generated_at: new Date().toISOString(),
            samples: {
                switch_ms: pick('switch_ms'),
                viewport_hl_ms: pick('viewport_hl_ms'),
                wrap_cache_hit: pick('wrap_cache_hit'),
                enter_ms: rounds.map(r => r.breakdown.enter_ms),
                exit_ms: rounds.map(r => r.breakdown.exit_ms),
                wrap_measure_ms: rounds.map(r => r.breakdown.wrap_measure_ms)
            },
            protocol: {
                switch_ms: '切入(getContent 被调)→源码首帧 + 退出(exitSourceModeOrGetMd 被调)→WYSIWYG 首帧；不含源码停留；不含 C++ runJavaScript 派发开销(~15ms，见 detail.cpp_timing 交叉核对)',
                viewport_hl_ms: '源码首帧→视口内全部 div.code-block pre code 含 .hljs-* 后代（或 asyncLowlight pending 归零兜底）；首帧=enterSourceMode 返回后首个 rAF；空闲闸门：探针把 requestIdleCallback 排队挂起至源码首帧才释放（受控起点——首帧时刻批次全在途；本机 4k 空闲高亮 ~1s 跑完且切入编排的转换间隙里批次照样推进，不闸则首帧时刻高亮已完成、指标恒 ~0）',
                wrap_cache_hit: '第 3 次切入后 enterSourceMode 同步返回时刻 _srcWrapCache 命中 0/1；当前实现 _srcStop 退出清缓存恒 0（占位）——Task 4 落地 __wrapCacheState() 后 probe.readWrapCache 切换消费',
                wrap_measure_note: '折行全文测量耗时远超计划预期（实测 bench-500 ~21s、guide ~23s、bench-4000 45s 上限内不完成即 >10min 量级）——4k 的 wrap_measure_ms 记 -1、wrap_cache_hit 无法在有界停留内翻面，Task 4 的 0→1 翻面以 guide 级文档为准',
                anchor: '切入前光标置文档末尾+页面滚到底（可视区锚定文档尾部代码块）',
                trigger: 'XTEST 合成 Ctrl+\\ → Qt QAction 快捷键 → MainWindow::onToggleSourceMode 真实编排',
                env: 'Xvfb ' + SCREEN_SPEC + ', QT_QPA_PLATFORM=xcb, 每轮隔离 HOME（应用有会话恢复）, 加载后主动消费 warmup() 防其 1.5s 后落入测量窗口',
                search: '源码内经 window.editorApi.startSearch 激活搜索以触发折行测量（无匹配则折行指标记 -1）'
            },
            rounds_detail: rounds
        };
        fs.mkdirSync(path.dirname(opts.out), { recursive: true });
        fs.writeFileSync(opts.out, JSON.stringify(out, null, 2) + '\n', 'utf8');
        process.stdout.write('已写入: ' + opts.out + '\n');
        process.stdout.write([
            '中位数(共 ' + opts.rounds + ' 轮):',
            '  switch_ms       = ' + metrics.switch_ms,
            '  viewport_hl_ms  = ' + metrics.viewport_hl_ms,
            '  wrap_cache_hit  = ' + metrics.wrap_cache_hit,
            '逐轮 switch_ms      : ' + JSON.stringify(out.samples.switch_ms),
            '逐轮 viewport_hl_ms : ' + JSON.stringify(out.samples.viewport_hl_ms),
            '逐轮 wrap_cache_hit : ' + JSON.stringify(out.samples.wrap_cache_hit)
        ].join('\n') + '\n');
    }

    // ---------- CLI ----------
    function parseArgs(argv) {
        const opts = { mode: null };
        for (let i = 0; i < argv.length; i++) {
            const a = argv[i];
            if (a === '--drive') opts.mode = 'drive';
            else if (a === '--aggregate') opts.mode = 'aggregate';
            else if (a === '--file') opts.file = argv[++i];
            else if (a === '--out') opts.out = argv[++i];
            else if (a === '--workdir') opts.workdir = argv[++i];
            else if (a === '--round') opts.round = parseInt(argv[++i], 10);
            else if (a === '--rounds') opts.rounds = parseInt(argv[++i], 10);
            else if (a === '--query') opts.query = argv[++i];
            else if (a === '--binary') opts.binary = argv[++i];
            else if (a === '--port') opts.port = parseInt(argv[++i], 10);
            else fail('未知参数: ' + a);
        }
        if (!opts.mode) fail('用法: node mode-switch-probe.js --drive|--aggregate …（见 measure-mode-switch.sh）');
        return opts;
    }

    (async () => {
        const opts = parseArgs(process.argv.slice(2));
        if (typeof WebSocket !== 'function')
            fail('node 内建 WebSocket 不可用（需 Node >= 22）');
        if (opts.mode === 'aggregate') {
            if (!opts.workdir || !opts.rounds || !opts.file || !opts.out)
                fail('--aggregate 需要 --workdir/--rounds/--file/--out');
            await aggregate(opts);
            return;
        }
        // --drive
        if (!opts.file || !opts.out || !opts.round)
            fail('--drive 需要 --file/--out/--round');
        opts.repoRoot = path.resolve(__dirname, '..');
        opts.workdir = opts.workdir || path.dirname(path.resolve(opts.out));
        opts.binary = opts.binary || path.join(opts.repoRoot, 'build', 'src', 'md-editor');
        if (!fs.existsSync(opts.binary))
            fail('编辑器二进制不存在: ' + opts.binary + '（先 cmake --build build --target md-editor）');
        if (!fs.existsSync(opts.file)) fail('待测文档不存在: ' + opts.file);
        opts.query = opts.query || '章节';
        const round = await driveRound(opts);
        if (!round || round.switch_ms === null || round.viewport_hl_ms === null)
            fail('第 ' + opts.round + ' 轮测量不完整: ' + JSON.stringify(round));
        fs.writeFileSync(opts.out, JSON.stringify(round, null, 2) + '\n', 'utf8');
        process.stdout.write(JSON.stringify({
            round: opts.round,
            switch_ms: round.switch_ms,
            viewport_hl_ms: round.viewport_hl_ms,
            wrap_cache_hit: round.wrap_cache_hit
        }) + '\n');
    })().catch((e) => {
        process.stderr.write('[measure-mode-switch] 失败: ' + fmtErr(e) + '\n');
        process.exit(1);
    });
}
