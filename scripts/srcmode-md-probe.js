#!/usr/bin/env node
// =============================================================================
// srcmode-md-probe.js — 源码模式 textarea MD 再生取证（Node 驱动，单文件）
//
// 骨架来源：scripts/mode-switch-probe.js（Wave 2 Task 1 无头测量驱动）——抽取
// 「Xvfb+编辑器进程生命周期 / CDP 连接 / XTEST 合成 Ctrl+\ / evaluate 采集」段，
// 裁掉测量轮次与页面探针注入：本工具不做计时，无需 monkey-patch editorApi，
// 只经 CDP 轮询状态与一次性取值（探针是宿主侧驱动，不入 qrc、零产品改动）。
//
// 取证协议（α 层诊断，见 scripts/probe-srcmode-md.sh）：
//   开文件 → 等加载稳定 → 合成 Ctrl+\ 切源码（真实快捷键路径：
//   Qt QAction → MainWindow::onToggleSourceMode → getContent →
//   MarkdownHandler::fromHtml → enterSourceMode）→ 等切入完成 →
//   Runtime.evaluate('window.editorApi.getSourceContent()') 取 textarea MD
//   全文 → 原子落盘。宿主侧再与文件原 MD 做 diff。
//
// 确定性：链路为纯函数（load→toHtml→TipTap→getHTML→fromHtml），无编辑无计时，
// 同一文件多次运行输出应逐字节一致。
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
// （与 mode-switch-probe.js 同源，逐字节一致）
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

// 原子落盘：先写临时文件再 rename——宿主侧 diff/复跑对比不会读到半截文件
function writeTextAtomic(file, text) {
    const tmp = file + '.tmp';
    fs.writeFileSync(tmp, text, 'utf8');
    fs.renameSync(tmp, file);
}

// ---------- 单次取证 ----------
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
    // 全新 HOME 保证取证环境干净、多次运行可比）
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
    let md = null;
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
        await sleep(SETTLE_MS);   // 布局/异步装饰短暂稳定（固定值，运行间一致）

        // 3. 合成 Ctrl+\ 切源码（真实快捷键 → onToggleSourceMode 真实编排），
        //    等切入完成：enterSourceMode 同步填充 textarea 后才置 isSourceMode，
        //    即该标志为真时 MD 已完整在 textarea 中
        await sendKey(displayNum, authFile);
        await pollGuard(
            () => cdp.evaluate('!!(window.editorApi && window.editorApi.isSourceMode)'),
            ENTER_TIMEOUT_MS, '切入源码模式');
        await sleep(SETTLE_MS);

        // 4. 取证：textarea MD 全文（getSourceContent 原样）+ 无编辑一致性核对
        //   （_sourceValue 是 C++ 传入的 fromHtml 产物镜像，textarea 未被编辑时
        //    两者全等——不等说明取证窗口内有干扰，产物不可信，报错）
        const check = await cdp.evaluate(
            '(function(){ var sv = document.getElementById("source-view");' +
            ' return { md: window.editorApi.getSourceContent(),' +
            '         mirror: window.editorApi._sourceValue || "",' +
            '         mirrorEqual: sv ? sv.value === (window.editorApi._sourceValue || "") : false }; })()');
        if (!check || typeof check.md !== 'string') fail('getSourceContent 返回非字符串');
        if (!check.mirrorEqual) fail('textarea 与 _sourceValue 镜像不一致（取证窗口内有编辑干扰）');
        md = check.md;
    } finally {
        if (cdp) { try { cdp.ws.close(); } catch (e) { } }
        killGroup(child);
        try { fs.closeSync(errFd); } catch (e) { }
    }
    // 等子进程组退净再返回（端口/显示号不撞）
    await new Promise((r) => { child.on('exit', r); setTimeout(r, 6000); });

    // 落盘：MD 再生末尾若有换行差异属链路产物，原样保留（取证不修饰）
    writeTextAtomic(opts.out, md);
    process.stdout.write(JSON.stringify({
        out: opts.out,
        file: opts.file,
        md_bytes: Buffer.byteLength(md, 'utf8'),
        md_lines: md.split('\n').length
    }) + '\n');
}

// ---------- CLI ----------
function parseArgs(argv) {
    const opts = {};
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--file') opts.file = argv[++i];
        else if (a === '--out') opts.out = argv[++i];
        else if (a === '--workdir') opts.workdir = argv[++i];
        else if (a === '--binary') opts.binary = argv[++i];
        else fail('未知参数: ' + a);
    }
    if (!opts.file || !opts.out) fail('用法: node srcmode-md-probe.js --file <md> --out <source-md.txt> [--binary <路径>] [--workdir <目录>]（见 probe-srcmode-md.sh）');
    return opts;
}

(async () => {
    if (typeof WebSocket !== 'function')
        fail('node 内建 WebSocket 不可用（需 Node >= 22）');
    const opts = parseArgs(process.argv.slice(2));
    opts.repoRoot = path.resolve(__dirname, '..');
    opts.workdir = opts.workdir || path.dirname(path.resolve(opts.out));
    opts.binary = opts.binary || path.join(opts.repoRoot, 'build', 'src', 'md-editor');
    if (!fs.existsSync(opts.binary))
        fail('编辑器二进制不存在: ' + opts.binary + '（先 cmake --build build --target md-editor）');
    if (!fs.existsSync(opts.file)) fail('待测文档不存在: ' + opts.file);
    await driveOnce(opts);
})().catch((e) => {
    process.stderr.write('[srcmode-md-probe] 失败: ' + fmtErr(e) + '\n');
    process.exit(1);
});
