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
