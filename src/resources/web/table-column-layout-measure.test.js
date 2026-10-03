// @vitest-environment jsdom
// __measureTableColumns 的 cfg 守卫路径单测（γ 修复，2026-10-03）。
// jsdom 无真实布局（min/max-content 布局缺失——w2-gamma 取证因这层弃用
// jsdom 复算），但守卫赋值/保存/还原链路可完整驱动：expand() 对首行每个
// 子格调 getBoundingClientRect 量宽，在本钩子内采样全表内联样式即得
// 「测量期生效值」（守卫赋值之后、还原之前）。
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const src = readFileSync(join(here, 'table-column-layout.js'), 'utf8');
// 模块挂全局（IIFE 尾参在 jsdom 环境解析为 window）
eval(src);
const measure = window.__measureTableColumns;

// 固件：border="1" 呈现属性（Qt toHtml 形态）+ 既有内联样式（验证还原）+
// 既有 colgroup（验证测量前剥除）+ th/td 混合首行
const FIXTURE = '<table border="1" style="border-collapse:collapse;">'
    + '<colgroup><col style="width:63px"></colgroup>'
    + '<tbody><tr><td style="color:red">编号</td><th>状态</th></tr>'
    + '<tr><td>001</td><td>已完成</td></tr></tbody></table>';

function setup() {
    document.body.innerHTML = FIXTURE;
    return {
        table: document.querySelector('table'),
        styledCell: document.querySelector('td[style]'),
        plainCell: document.querySelectorAll('td')[1],
        th: document.querySelector('th'),
    };
}

// 测量中点采样：patch getBoundingClientRect，在每次量宽时快照
// td/th/table 三处内联 border-width（测量期生效值）
function sampleDuringMeasure(fn) {
    const snaps = [];
    const proto = window.Element.prototype;
    const orig = proto.getBoundingClientRect;
    proto.getBoundingClientRect = function () {
        const t = document.querySelector('table');
        snaps.push({
            td: document.querySelector('td').style.borderWidth,
            th: document.querySelector('th').style.borderWidth,
            table: t ? t.style.borderWidth : undefined,
        });
        return orig.call(this);
    };
    try {
        fn();
    } finally {
        proto.getBoundingClientRect = orig;
    }
    return snaps;
}

describe('__measureTableColumns cfg.border 守卫', () => {
    // CSSOM 读回长度值会规范化单位（'0' → '0px'），按零语义断言
    const zeroBorder = (v) => v === '0' || v === '0px';

    it('cfg.border 生效路径：测量期 td/th 与 table 三处同清', () => {
        const els = setup();
        const snaps = sampleDuringMeasure(() => {
            const m = measure(els.table, { fontSize: '11pt', padding: '0 8px', border: '0' });
            expect(m).toBeTruthy();
        });
        // 两趟测量（min/max）× 首行 2 子格 = 4 次量宽，全部采样点三处都须清零
        expect(snaps.length).toBeGreaterThanOrEqual(4);
        for (const s of snaps) {
            expect(zeroBorder(s.td)).toBe(true);
            expect(zeroBorder(s.th)).toBe(true);
            expect(zeroBorder(s.table)).toBe(true);
        }
    });

    it('不传 cfg.border：测量期不写边框（守卫式，既有调用者构造性无变化）', () => {
        const els = setup();
        const snaps = sampleDuringMeasure(() => {
            measure(els.table, { fontSize: '11pt', padding: '0 8px' });
        });
        expect(snaps.length).toBeGreaterThanOrEqual(1);
        for (const s of snaps) {
            expect(s.td).toBe('');
            expect(s.table).toBe('');
        }
    });

    it('测量后样式还原：既有 style 属性逐字节回放、无属性者移除', () => {
        const els = setup();
        const tableStyleBefore = els.table.getAttribute('style');
        const cellStyleBefore = els.styledCell.getAttribute('style');
        measure(els.table, { border: '0', fontSize: '11pt' });
        expect(els.table.getAttribute('style')).toBe(tableStyleBefore);
        expect(els.styledCell.getAttribute('style')).toBe(cellStyleBefore);
        expect(els.plainCell.getAttribute('style')).toBe(null);
    });

    it('测量前剥除既有 colgroup（克隆表带旧 col 不污染 auto 测量）', () => {
        const els = setup();
        measure(els.table, { border: '0' });
        expect(els.table.querySelector('colgroup')).toBe(null);
    });
});
