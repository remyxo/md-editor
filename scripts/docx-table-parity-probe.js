#!/usr/bin/env node
// =============================================================================
// docx-table-parity-probe.js — DOCX 两模式归一器输入采集（Node 驱动，单文件）
//
// 骨架来源：scripts/srcmode-md-probe.js（其骨架又出自 scripts/mode-switch-probe.js
// 的 Wave 2 Task 1 无头测量驱动）——抽取「Xvfb+编辑器进程生命周期 / CDP 连接 /
// XTEST 合成 Ctrl+\ / evaluate 采集」段。与 srcmode 探针的差别：切入源码前
// 先在 WYSIWYG 态取一次 getExportHtml()——单次启动内采齐两模式输入，文档
// 状态两侧严格同源。
//
// 取证协议（γ 层对齐探针，见 scripts/probe-docx-table-parity.sh 与 w2-gamma
// 取证报告）：
//   开文件 → 等加载稳定 → [WYSIWYG] Runtime.evaluate(
//     'window.editorApi.getExportHtml()') 落盘 → 合成 Ctrl+\ 切源码 → 等切入
//   → [源码] Runtime.evaluate('window.editorApi.getSourceContent()') 落盘。
//   两产物即 MainWindow::onExportDocx 两条路径喂给 TableColumnNormalizer 的
//   输入（highlightForExportAsync 只动代码块、表格无关，探针跳过该步）。
//
// 确定性：链路为纯函数（load→TipTap→getExportHtml / getHTML→fromHtml→
// getSourceContent），无编辑无计时；live colgroup 是窗口宽基准，但归一器
// 测量前剥除 colgroup（w2-gamma「起步分配基线」证伪项），窗口尺寸不影响
// 归一产物。
// =============================================================================
'use strict';
const fs = require('fs');
const path = require('path');
const net = require('net');
const { spawn, execFile } = require('child_process');

// ---------- 常量 ----------
const SCREEN_SPEC = '-screen 0 1600x1000x24';
const READY_TIMEOUT_MS = 90000;      // 冷 HOME 首启（QWebEngine 建档）较慢
const CDP_UP_TIMEOUT_MS = 45000;     // /json/list 出现 page target
const ENTER_TIMEOUT_MS = 30000;      // 切入→textarea 填充完成
const POLL_MS = 120;
const SETTLE_MS = 300;               // 切入完成后的固定静置（协议稳定段）

// X11 按键助手：python3-xlib 设焦点 + XTEST 合成 Ctrl+\（真实键盘事件路径，
// 走 Qt QAction 快捷键 → MainWindow::onToggleSourceMode 真实编排）。
// 无窗口管理器（裸 Xvfb）下：按 WM_CLASS 找可视的最大窗口手动 XSetInputFocus。
//（与 srcmode-md-probe.js 同源，逐字节一致）
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
    async evaluate(expression) {
        const r = await this.send('Runtime.evaluate',
            { expression, returnByValue: true });
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

// 原子落盘：先写临时文件再 rename——宿主侧比对/复跑不会读到半截文件
function writeTextAtomic(file, text) {
    const tmp = file + '.tmp';
    fs.writeFileSync(tmp, text, 'utf8');
    fs.renameSync(tmp, file);
}

// ---------- 单次采集 ----------
async function driveOnce(opts) {
    const roundDir = opts.workdir;
    fs.mkdirSync(roundDir, { recursive: true });
    const homeDir = path.join(roundDir, 'home');
    fs.mkdirSync(homeDir, { recursive: true });
    const errLogPath = path.join(roundDir, 'editor-stderr.log');
    const errFd = fs.openSync(errLogPath, 'w');

    const port = await freePort();
    const displayNum = 20 + (port % 400);   // 端口哈希出稳定且大概率空闲的显示号
    // xvfb-run 默认把 X 授权 cookie 写到 cwd 的 ./.Xauthority（会污染仓库根目录），
    // 显式指到运行目录，按键助手经 XAUTHORITY 连同一虚拟显示
    const authFile = path.join(roundDir, 'Xauthority');

    // 页面环境：剔除 WAYLAND_DISPLAY、强制 xcb；隔离 HOME（应用有会话恢复，
    // 全新 HOME 保证采集环境干净、多次运行可比）
    const env = Object.assign({}, process.env, {
        QT_QPA_PLATFORM: 'xcb',
        QTWEBENGINE_REMOTE_DEBUGGING: String(port),
        HOME: homeDir
    });
    delete env.WAYLAND_DISPLAY;

    const child = spawn('xvfb-run',
        ['-n', String(displayNum), '-s', SCREEN_SPEC, '-f', authFile, opts.binary,
         '--remote-debugging-port=' + port, opts.file],
        { env, detached: true, stdio: ['ignore', 'ignore', 'pipe'] });
    let childDied = '';
    child.stderr.setEncoding('utf8');
    child.stderr.on('data', (s) => { try { fs.writeSync(errFd, s); } catch (e) { } });
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

    let cdp = null;
    let exportHtml = null;
    let srcMd = null;
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

        // 2. 等编辑器就绪 + 内容加载完成（setContent 完成、doc 非空）
        await pollGuard(() => cdp.evaluate('typeof window.editorApi !== "undefined"'),
            READY_TIMEOUT_MS, 'editorApi 就绪');
        const readyExpr = '!!(window.editorApi && window.editorApi.editor ' +
            '&& window.editorApi.editor.state ' +
            '&& window.editorApi.editor.state.doc.content.size > 2)';
        await pollGuard(() => cdp.evaluate(readyExpr), READY_TIMEOUT_MS, '文档内容就绪');
        await sleep(SETTLE_MS);   // live 表格布局（ResizeObserver）短暂稳定

        // 3. [WYSIWYG] 导出输入：getExportHtml()（ExportHtmlAsync::Mode::Docx 在
        //    WYSIWYG 态取的即此值；highlightExportHtml 只动代码块，表格无关）
        exportHtml = await cdp.evaluate('window.editorApi.getExportHtml()');
        if (typeof exportHtml !== 'string' || !exportHtml)
            fail('getExportHtml 返回空/非字符串');

        // 4. 合成 Ctrl+\ 切源码（真实快捷键 → onToggleSourceMode 真实编排），
        //    等切入完成：enterSourceMode 同步填充 textarea 后才置 isSourceMode
        await sendKey(displayNum, authFile);
        await pollGuard(
            () => cdp.evaluate('!!(window.editorApi && window.editorApi.isSourceMode)'),
            ENTER_TIMEOUT_MS, '切入源码模式');
        await sleep(SETTLE_MS);

        // 5. [源码模式] 导出输入前置：textarea MD 全文（getSourceContent 原样）。
        //    无编辑一致性核对（_sourceValue 是 C++ 传入的 fromHtml 产物镜像，
        //    textarea 未被编辑时两者全等——不等说明采集窗口内有干扰，产物不可信）
        const check = await cdp.evaluate(
            '(function(){ var sv = document.getElementById("source-view");' +
            ' return { md: window.editorApi.getSourceContent(),' +
            '         mirrorEqual: sv ? sv.value === (window.editorApi._sourceValue || "") : false }; })()');
        if (!check || typeof check.md !== 'string' || !check.md)
            fail('getSourceContent 返回空/非字符串');
        if (!check.mirrorEqual) fail('textarea 与 _sourceValue 镜像不一致（采集窗口内有编辑干扰）');
        srcMd = check.md;
    } finally {
        if (cdp) { try { cdp.ws.close(); } catch (e) { } }
        killGroup(child);
        try { fs.closeSync(errFd); } catch (e) { }
    }
    // 等子进程组退净再返回（端口/显示号不撞）
    await new Promise((r) => { child.on('exit', r); setTimeout(r, 6000); });

    writeTextAtomic(opts.outExportHtml, exportHtml);
    writeTextAtomic(opts.outSourceMd, srcMd);
    process.stdout.write(JSON.stringify({
        export_html: opts.outExportHtml,
        source_md: opts.outSourceMd,
        file: opts.file,
        html_bytes: Buffer.byteLength(exportHtml, 'utf8'),
        md_bytes: Buffer.byteLength(srcMd, 'utf8'),
        md_lines: srcMd.split('\n').length
    }) + '\n');
}

// ---------- CLI ----------
function parseArgs(argv) {
    const opts = {};
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--file') opts.file = argv[++i];
        else if (a === '--out-export-html') opts.outExportHtml = argv[++i];
        else if (a === '--out-source-md') opts.outSourceMd = argv[++i];
        else if (a === '--workdir') opts.workdir = argv[++i];
        else if (a === '--binary') opts.binary = argv[++i];
        else fail('未知参数: ' + a);
    }
    if (!opts.file || !opts.outExportHtml || !opts.outSourceMd)
        fail('用法: node docx-table-parity-probe.js --file <md> --out-export-html <wysiwyg.html>' +
             ' --out-source-md <source.md> [--binary <路径>] [--workdir <目录>]（见 probe-docx-table-parity.sh）');
    return opts;
}

(async () => {
    if (typeof WebSocket !== 'function')
        fail('node 内建 WebSocket 不可用（需 Node >= 22）');
    const opts = parseArgs(process.argv.slice(2));
    opts.repoRoot = path.resolve(__dirname, '..');
    opts.workdir = opts.workdir || path.dirname(path.resolve(opts.outExportHtml));
    opts.binary = opts.binary || path.join(opts.repoRoot, 'build', 'src', 'md-editor');
    if (!fs.existsSync(opts.binary))
        fail('编辑器二进制不存在: ' + opts.binary + '（先 cmake --build build --target md-editor）');
    if (!fs.existsSync(opts.file)) fail('待测文档不存在: ' + opts.file);
    await driveOnce(opts);
})().catch((e) => {
    process.stderr.write('[docx-table-parity-probe] 失败: ' + fmtErr(e) + '\n');
    process.exit(1);
});
