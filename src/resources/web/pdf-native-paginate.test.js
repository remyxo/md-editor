// @vitest-environment jsdom
// __planPages 纯规划器测试（脚注快路径，2026-10-13）：单遍前向贪心的预算/
// 推页/归属/防线语义。DOM 侧（__planFootnotes/__paginateNativeHf 的测量）
// 依赖真实布局，jsdom 无布局——由 tests/test_pdfexporter.cpp 的形状断言与
// E2E（xvfb selftest）覆盖；这里只测纯算术。
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));

function loadPlanner() {
    window.eval(readFileSync(path.join(W, 'pdf-native-paginate.js'), 'utf-8'));
    return window.__planPages;
}

// 单元工厂：u(top, h, {refs, canBreak, thH})，默认 canBreak=true；node 给
// 占位对象（规划器按 node 非空记录断点单元，真实调用传入 DOM 节点）
function u(top, h, opts) {
    opts = opts || {};
    return { node: { top: top }, top: top, h: h, thH: opts.thH || 0,
             canBreak: opts.canBreak === false ? 0 : 1,
             refs: opts.refs || [] };
}

const HPX = 100;      // 版心预算
const NATCAP = 120;   // 自然页容量（Hpx + 下边距）

describe('__planPages（脚注快路径规划器）', () => {
    it('def 计入预算：含引用单元连同 def 推下页，分隔线计入新页', () => {
        const plan = loadPlanner();
        const units = [
            u(0, 60),
            u(60, 30, { refs: ['a'] }),
            u(90, 15),
        ];
        const r = plan(units, [{ id: 'a', top: 65 }], { a: 25 }, 5, HPX, NATCAP);
        // u2 连 def（25+分隔线5=30）超页 1 预算 → 推页 2，def 随行
        expect(r.pages.length).toBe(2);
        expect(r.pages[0].fnH).toBe(0);            // 无引用页零预留
        expect(r.pages[0].hasFn).toBe(false);
        expect(r.pages[1].fnH).toBe(30);           // def 25 + 分隔线 5（只计一次）
        expect(r.fnPlan['2']).toEqual(['a']);
        expect(r.fnPlan['1']).toBeUndefined();
        expect(r.fnCount).toBe(1);
        expect(r.fnTall).toBe(0);
        expect(r.fnStraddle).toBe(0);
    });

    it('同 def 多引用只随首引页（一脚注一锚点，Word 同款）', () => {
        const plan = loadPlanner();
        const units = [
            u(0, 50, { refs: ['a'] }),
            u(50, 40, { refs: ['a'] }),   // 第二处引用同一 def
        ];
        const r = plan(units, [{ id: 'a', top: 5 }, { id: 'a', top: 55 }],
                       { a: 10 }, 5, HPX, NATCAP);
        expect(r.pages[0].fnH).toBe(15);           // 只在页 1 预留一次
        expect(r.fnPlan['1']).toEqual(['a']);
        expect(r.fnPlan['2']).toBeUndefined();
        expect(r.fnCount).toBe(1);
    });

    it('防线 1：单 def+分隔线超整页 → fnTall（回退 paged 的判据）', () => {
        const plan = loadPlanner();
        const units = [u(0, 80, { refs: ['a'] })];
        const r = plan(units, [{ id: 'a', top: 5 }], { a: 98 }, 5, HPX, NATCAP);
        expect(r.fnTall).toBe(1);
    });

    it('防线 1：页首单元连同自身 def 超整页 → fnTall', () => {
        const plan = loadPlanner();
        // 单元本身 60，def 30+分隔线 5 → 95 < 100 不触发；换 def 40 → 105 触发
        const r1 = plan([u(0, 60, { refs: ['a'] })], [{ id: 'a', top: 5 }],
                        { a: 30 }, 5, HPX, NATCAP);
        expect(r1.fnTall).toBe(0);
        const r2 = plan([u(0, 60, { refs: ['a'] })], [{ id: 'a', top: 5 }],
                        { a: 40 }, 5, HPX, NATCAP);
        expect(r2.fnTall).toBe(1);
    });

    it('防线 2：natCap 跨页不可断块越线页 × 脚注同页 → straddle；' + '引用在边界两侧归属不同页', () => {
        const plan = loadPlanner();
        const units = [u(0, 125, { canBreak: false, refs: ['a'] })];
        // 引用在页 1 区段（top=110 < 120）：页 1 被 125 高的墨迹穿越内容线 →
        // over × 脚注同页 → straddle
        const r1 = plan(units, [{ id: 'a', top: 110 }], { a: 10 }, 5, HPX, NATCAP);
        expect(r1.natSteps).toBe(1);
        expect(r1.pages.length).toBe(2);           // natCap 推进页计入页数
        expect(r1.pages[0].over).toBe(true);
        expect(r1.fnPlan['1']).toEqual(['a']);
        expect(r1.fnStraddle).toBe(1);
        // 引用在页 2 区段（top=130 ≥ 120）：页 2 未越线 → 无 straddle
        const r2 = plan(units, [{ id: 'a', top: 130 }], { a: 10 }, 5, HPX, NATCAP);
        expect(r2.fnPlan['2']).toEqual(['a']);
        expect(r2.fnStraddle).toBe(0);
    });

    it('悬空引用（无对应 def）忽略，不空转不预留', () => {
        const plan = loadPlanner();
        const units = [u(0, 50, { refs: ['b'] })];
        const r = plan(units, [{ id: 'b', top: 5 }], {}, 5, HPX, NATCAP);
        expect(r.fnCount).toBe(0);
        expect(r.pages[0].fnH).toBe(0);
        expect(r.fnTall).toBe(0);
    });

    it('表续页重复表头（thH）与脚注区（fnH）同页同扣', () => {
        const plan = loadPlanner();
        const units = [
            u(0, 60),
            u(60, 40, { thH: 20, refs: ['a'] }),
        ];
        const r = plan(units, [{ id: 'a', top: 65 }], { a: 10 }, 5, HPX, NATCAP);
        // 页 1 预算 100−15=85 放不下 u2 → 推页 2；页 2 预算 100−20−15=65，
        // u2 占 40 装得下
        expect(r.pages.length).toBe(2);
        expect(r.pages[1].th0).toBe(20);
        expect(r.pages[1].fnH).toBe(15);
        expect(r.fnPlan['2']).toEqual(['a']);
    });

    it('贪心断页与迁出前行为一致（无脚注时零影响）', () => {
        const plan = loadPlanner();
        const units = [
            u(0, 60), u(60, 50),   // u2: 60+50=110 > 100 → 断页
        ];
        const r = plan(units, [], {}, 0, HPX, NATCAP);
        expect(r.pages.length).toBe(2);
        expect(r.breaks).toBe(1);
        expect(r.breakUnits.length).toBe(1);
        expect(r.breakUnits[0]).toBe(units[1].node);
        expect(r.tall).toBe(0);
        expect(r.fills).toEqual([60, 50]);
    });

    it('单块高于版心 → tall 计数（既有回退判据不受脚注路径影响）', () => {
        const plan = loadPlanner();
        const r = plan([u(0, 150)], [], {}, 0, HPX, NATCAP);
        expect(r.tall).toBe(1);
    });
});
