// export-highlight.js 纯函数层测试：主题 CSS 规则解析 + 级联合成 + HAST 展平。
// 色值断言用真实 hljs-light.css（GitHub light 主题）原文，不造夹具主题
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const W = path.dirname(fileURLToPath(import.meta.url));
const cssText = readFileSync(path.join(W, 'hljs-light.css'), 'utf-8');

// 被 eval 的脚本定义 globalThis.__exportHighlightParseRules（node 环境可直接 eval）
function loadModule() {
    (0, eval)(readFileSync(path.join(W, 'export-highlight.js'), 'utf-8'));
    return globalThis.__exportHighlightParseRules;
}

describe('__exportHighlightParseRules', () => {
    it('解析真实主题：关键字/字符串/注释颜色', () => {
        const rules = loadModule()(cssText);
        const byClasses = (cs) => rules.find((r) => r.classes.join('|') === cs.join('|'));
        expect(byClasses(['hljs-keyword']).color).toBe('#d73a49');
        expect(byClasses(['hljs-string']).color).toBe('#032f62');
        expect(byClasses(['hljs-comment']).color).toBe('#6a737d');
        expect(rules.length).toBeGreaterThan(20);
    });

    it('复合选择器入表（.hljs-title.function_ 两类一条）', () => {
        const rules = loadModule()(cssText);
        const compound = rules.find((r) => r.classes.join('.') === 'hljs-title.function_');
        expect(compound).toBeTruthy();
        expect(compound.color).toBe('#6f42c1');
    });

    it('后代选择器（.hljs-meta .hljs-keyword）按类集合存储', () => {
        const rules = loadModule()(cssText);
        const desc = rules.find((r) => r.classes.join(' ') === 'hljs-meta hljs-keyword');
        expect(desc).toBeTruthy();
    });

    it('粗体/斜体/背景属性入表', () => {
        const rules = loadModule()(cssText);
        const byClasses = (cs) => rules.find((r) => r.classes.join('|') === cs.join('|'));
        expect(byClasses(['hljs-section']).weight).toBe('700');
        expect(byClasses(['hljs-emphasis']).fstyle).toBe('italic');
        expect(byClasses(['hljs-addition']).bg).toBe('#f0fff4');
    });

    it('容器规则（pre code.hljs / code.hljs）被跳过：所有类名均以 hljs 开头', () => {
        const rules = loadModule()(cssText);
        for (const r of rules)
            for (const c of r.classes)
                expect(c === 'hljs' || c.startsWith('hljs-')).toBe(true);
    });
});
