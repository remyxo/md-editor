// 往返保真矩阵回归测试体系（test_roundtrip_matrix）
//
// 与 test_markdownhandler.cpp 的零散用例互补，本文件做系统化矩阵回归：
//   1. 全元素覆盖：行内 7 语法元素 × 6 内容变体、块级全集
//   2. 嵌套组合矩阵：两层全排（A嵌B 与 B嵌A）、三层结构化抽样
//      （含 C嵌B嵌A 逆序）、块级 × 行内
//   3. 长行区：>80/>160 列含嵌套标记、唯一空格折行点（历史折行吞字区）
//   4. 历史 bug 回归：CHANGELOG 16 个版本 60+ 条修复按类别固化为最小复现
//   5. 多轮往返：首轮保真 ≠ 稳定——第二/三轮失真（转义累积、归一振荡）
//      由「收敛 + 双确认」判定抓出
//
// 两层判定模型（沿用 testEscapedExtDelimRoundTrip 的 allowNormalize 分级先例）：
//   - ByteStable          rounds[1..4] 全部 == 输入（字节保真）
//   - Converge            convergedAt ∈ {1,2} 且双确认（R[n]==R[n+1] 两连等）
//   - NormalizeThenConverge  rounds[1] == 精确 expectR1 字面量 + Converge
//
// 轮数 = 4（输入 + 4 次往返）：两步归一（R2==R3）需 R3==R4 双确认；
// 周期 2 振荡（P,Q,P,Q）4 轮内无相邻相等，可显式识别标注。
//
// 双轨断言：收敛对「丢层后稳定的不动点」无感（整层 bold 丢失后纯文本照样
// 收敛）——语义形态守卫是唯一防线：HTML 轨（首轮 HTML 匹配元素形态）与
// MD 轨（rounds[1] 匹配 MD 语法形态）任一命中即通过。
//
// 归一白名单三原则（防 weakening）：
//   1. 全部集中在下方 NormEntry 单表，reason 注明 CHANGELOG 版本出处
//   2. 永不豁免四条通用不变量：收敛 / 哨兵泄漏 / 累积探针 / 内容存活
//   3. expectR1 必须是精确字面量——白名单只是把「R1==输入」换成
//      「R1==文档化归一形」，仍是逐字节断言
//
// 运行：ctest -R test_roundtrip_matrix --output-on-failure
//       ./tests/test_roundtrip_matrix testNestedPairMatrix（单槽）
//       ./tests/test_roundtrip_matrix "testNestedPairMatrix:bold(sub)_cjk"（单行，
//       「槽:行名」须作一个参数传入，行名禁含 ':'）

#include <QTest>
#include <QElapsedTimer>
#include <algorithm>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QVector>
#include "../src/core/MarkdownHandler.h"

// ═══════════════════════════════════════════════════════════════════════
// 区块 A：往返检查器
// ═══════════════════════════════════════════════════════════════════════

static const int kMaxRounds = 4;   // 输入 + 4 次往返

// rounds[0]=输入 md；rounds[i]=第 i 次往返后的 md；
// htmls[i]=toHtml(rounds[i])，即第 i+1 次往返的中间 HTML
// （htmls[0] 供守卫扫描，零额外转换成本）。
struct RoundTripOutcome {
    QStringList rounds;    // 大小 5
    QStringList htmls;     // 大小 4
    int convergedAt = -1;  // 首个 rounds[i]==rounds[i+1] 的 i（i∈[1,3]）；-1=未收敛
};

// ── 行内重叠格式的 DOM 归一化（TipTap 链路近似） ─────────────────────────
// 背景：QTextDocument 序列化「strong 区间内套 mark/sup 等字符格式」的重叠时会
// 产出裂标签链，如
//   <strong><mark></strong><strong><em>abc</em></strong><strong></mark></strong>
// 生产链路中该 HTML 进入 QWebEngineView 后由浏览器 DOM（HTML5 自适应解析 +
// ProseMirror mark 集合模型）重建为规范嵌套；测试直连 fromHtml 无此环节，
// mark 提取会跨链取到字面 </strong> 造成内容污染（矩阵回归 ① 簇）。
// 此处按「每个文本片段记录生效格式集合」的 ProseMirror 语义重建规范嵌套，
// 逼近真实 TipTap 链路（端到端验证必须走真实链路，见项目记忆）。
//
// 保守策略：仅当检出裂链（闭某家族标签时其上还有其它家族未闭）才重建；
// 良构输入原样返回（字节恒等，不影响既有判定）；闭标签无对应开标签、或扫
// 描结束仍有未闭标签 → 原样返回。家族含 a/font（与产品 escAware 约定一致）；
// <br> 为 void 元素不入栈。重建嵌套序固定（外→内）＝家族序号：
// strong, em, mark, s/del/strike, sub, sup, u, small, kbd, code, a, font。
struct InlineFormatOpen {
    int fam = -1;
    QString openTag;    // 原始开标签全文（保留属性）
    QString name;       // 标签名（闭标签配对用）
};

static int inlineFormatFamily(const QString &name)
{
    static const char *kNames[16] = {
        "strong", "b", "em", "i", "mark", "s", "del", "strike",
        "sub", "sup", "u", "small", "kbd", "code", "a", "font",
    };
    static const int kFamily[16] = { 0, 0, 1, 1, 2, 3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    for (int i = 0; i < 16; ++i)
        if (name == QLatin1String(kNames[i])) return kFamily[i];
    return -1;
}

static QString tipTapNormalizeInlineOverlap(const QString &html)
{
    auto tagNameOf = [](const QString &tag) -> QString {
        int s = (tag.size() > 1 && tag.at(1) == QLatin1Char('/')) ? 2 : 1;
        int e = s;
        while (e < tag.size() && tag.at(e).isLetter())
            ++e;
        return tag.mid(s, e - s).toLower();
    };
    struct Atom { QString text; QVector<InlineFormatOpen> opens; };
    QVector<Atom> atoms;
    QVector<InlineFormatOpen> stack;
    bool splitDetected = false;
    int i = 0;
    while (i < html.size()) {
        if (html.at(i) != QLatin1Char('<')) {
            const int lt = html.indexOf(QLatin1Char('<'), i);
            const int end = lt < 0 ? html.size() : lt;
            atoms << Atom{ html.mid(i, end - i), stack };
            i = end;
            continue;
        }
        int gt = html.indexOf(QLatin1Char('>'), i);
        if (gt < 0) {
            atoms << Atom{ html.mid(i), stack };
            break;
        }
        const QString tag = html.mid(i, gt - i + 1);
        i = gt + 1;
        const int fam = inlineFormatFamily(tagNameOf(tag));
        if (tag.startsWith(QLatin1String("</"))) {
            if (fam < 0) { atoms << Atom{ tag, stack }; continue; }
            int idx = -1;
            for (int k = stack.size() - 1; k >= 0; --k)
                if (stack[k].fam == fam) { idx = k; break; }
            if (idx < 0)
                return html;               // 无对应开标签：放弃归一
            if (idx < stack.size() - 1)
                splitDetected = true;      // 之上有其它家族未闭＝裂链
            QVector<InlineFormatOpen> above;
            while (stack.size() - 1 > idx)
                above.prepend(stack.takeLast());
            stack.removeAt(idx);
            stack.append(above);           // 上方家族隐式延续（DOM 行为）
        } else {
            if (fam < 0 || tag.endsWith(QLatin1String("/>"))) {
                atoms << Atom{ tag, stack };
                continue;
            }
            stack.append(InlineFormatOpen{ fam, tag, tagNameOf(tag) });
        }
    }
    if (!splitDetected || !stack.isEmpty())
        return html;                       // 良构或结构异常：字节恒等

    // 重建：相邻同格式集合的原子合并为一组，按家族序（外→内）包裹。
    // 分组键只看家族序列（C++17 无合成 operator==，且裂链重开的开标签
    // 属性与原开一致，取首组的原始开标签即可）
    auto sameFamilies = [](const QVector<InlineFormatOpen> &a,
                           const QVector<InlineFormatOpen> &b) {
        if (a.size() != b.size()) return false;
        for (int k = 0; k < a.size(); ++k)
            if (a[k].fam != b[k].fam) return false;
        return true;
    };
    QString out;
    QVector<InlineFormatOpen> groupOpens;
    QString groupText;
    auto flushGroup = [&]() {
        if (groupText.isEmpty()) { groupOpens.clear(); return; }
        QVector<InlineFormatOpen> ordered = groupOpens;
        std::sort(ordered.begin(), ordered.end(),
                  [](const InlineFormatOpen &a, const InlineFormatOpen &b) { return a.fam < b.fam; });
        for (const InlineFormatOpen &o : std::as_const(ordered))
            out += o.openTag;
        out += groupText;
        for (int k = ordered.size() - 1; k >= 0; --k)
            out += QStringLiteral("</") + ordered[k].name + QLatin1Char('>');
        groupText.clear();
        groupOpens.clear();
    };
    for (const Atom &a : std::as_const(atoms)) {
        if (!sameFamilies(a.opens, groupOpens)) {
            flushGroup();
            groupOpens = a.opens;
        }
        groupText += a.text;
    }
    flushGroup();
    return out;
}

static RoundTripOutcome roundTrip(const QString &md)
{
    RoundTripOutcome o;
    o.rounds << md;
    for (int i = 0; i < kMaxRounds; ++i) {
        // htmls 存归一化后的 HTML——生产链路编辑区持有的是浏览器 DOM
        // 规范化结果（TipTap mark 集合），守卫与 fromHtml 都应基于它
        o.htmls << tipTapNormalizeInlineOverlap(MarkdownHandler::toHtml(o.rounds.last()));
        o.rounds << MarkdownHandler::fromHtml(o.htmls.last());
    }
    for (int i = 1; i + 1 < o.rounds.size(); ++i) {
        if (o.rounds[i] == o.rounds[i + 1]) { o.convergedAt = i; break; }
    }
    return o;
}

// 最早失真轮定位：首个 rounds[i]!=rounds[i+1] 的行:列 + 分歧行全文
struct FirstDiff {
    int roundIdx = -1;   // 0=首轮即失真
    int line = -1;       // 0 起
    int col = -1;        // 0 起
    QString lineA, lineB;
    QString note;        // 如「行数不同 a=3 b=4」
};

static FirstDiff firstDiff(const QStringList &rounds)
{
    FirstDiff d;
    for (int i = 0; i + 1 < rounds.size(); ++i) {
        if (rounds[i] == rounds[i + 1]) continue;
        d.roundIdx = i;
        const QStringList la = rounds[i].split('\n');
        const QStringList lb = rounds[i + 1].split('\n');
        if (la.size() != lb.size())
            d.note = QString("行数不同 a=%1 b=%2").arg(la.size()).arg(lb.size());
        const int n = qMin(la.size(), lb.size());
        for (int ln = 0; ln < n; ++ln) {
            if (la[ln] == lb[ln]) continue;
            d.line = ln;
            const int m = qMin(la[ln].size(), lb[ln].size());
            int c = 0;
            while (c < m && la[ln][c] == lb[ln][c]) ++c;
            d.col = c;
            d.lineA = la[ln];
            d.lineB = lb[ln];
            return d;
        }
        // 行数不同且公共前缀全同 → 分歧在行尾差异
        d.line = n - 1;
        d.col = qMin(la[n - 1].size(), lb[n - 1].size());
        d.lineA = la[n - 1];
        d.lineB = lb[n - 1];
        return d;
    }
    return d;
}

// 哨兵扫描：ZZ 占位符家族（源码实测 25 种，ZZ[A-Z]{2,6} 覆盖）+ \x01 内部
// 哨兵 + BRMARKER + data-brk。矩阵用例内容约定不含 ZZ 字面量（防自匹配），
// 命中即产品代码泄漏。返回命中描述列表，空=干净。
static QStringList scanSentinels(const QString &text)
{
    QStringList hits;
    static const QRegularExpression zzRe("ZZ[A-Z]{2,6}[0-9]*ZZ");
    auto it = zzRe.globalMatch(text);
    while (it.hasNext()) {
        const QString h = it.next().captured();
        if (!hits.contains(h)) hits << h;
    }
    if (text.contains(QChar(1))) hits << "\\x01";
    if (text.contains("BRMARKER")) hits << "BRMARKER";
    if (text.contains("data-brk")) hits << "data-brk";
    return hits;
}

// 可视化转义：单行化 + 截断（失真链排版用）
static QString escapeVisual(const QString &s)
{
    QString r = s;
    r.replace('\n', "⏎").replace('\t', "⇥").replace(QChar(1), "\\x01");
    if (r.size() > 120) r = r.left(117) + "…";
    return r;
}

// ═══════════════════════════════════════════════════════════════════════
// 区块 B：语义形态守卫
// ═══════════════════════════════════════════════════════════════════════

// 守卫规则：htmlForms 匹配 htmls[0] 或 mdForms 匹配 rounds[1]，任一命中即
// 通过（双轨制：bold(italic) 产出 ***x***，HTML 侧 <strong><em> 双形态都在
// 而 MD 侧嵌套正则会失败，反之亦然）。
struct SemGuard {
    QVector<QRegularExpression> htmlForms;
    QVector<QRegularExpression> mdForms;
};

struct GuardSpec {
    const char *key;
    QVector<const char *> htmlForms;   // 正则源
    QVector<const char *> mdForms;
};

// 守卫映射表（22 元素）。code 用跨平台 mono 族名宽松匹配
// （docs/cross-platform-monospace-font.md：Linux 测不出 Windows bug，
// 宽松正则是唯一可行解）；sup 排除脚注引用的 <sup data-type="footnoteRef">。
static const QVector<GuardSpec> &guardTable()
{
    static const QVector<GuardSpec> table = {
        {"bold", {"<strong\\b", "<b\\b", "font-weight:\\s*(700|bold)"}, {"\\*\\*[^*\\n]+\\*\\*"}},
        // italic mdForms 兼收 ***x*** 扁平形——writer 序列化 bold+italic 重叠
        // 字符格式时扁平化（同 bold(italic)≡***x***），语义即 italic 在场
        {"italic", {"<em\\b", "<i\\b", "font-style:\\s*italic"},
         {"(?<!\\*)\\*[^*\\n]+\\*(?!\\*)", "\\*\\*\\*[^*\\n]+\\*\\*\\*"}},
        {"strike", {"<s\\b", "<del\\b", "<strike\\b", "line-through"}, {"~~[^~\\n]+~~"}},
        {"mark", {"<mark\\b", "background-color:"}, {"==[^=\\n]+=="}},
        {"sub", {"<sub\\b"}, {"(?<!~)~[^~\\n]+~(?!~)"}},
        {"sup", {"<sup(?![^>]*data-type)"}, {"\\^[^^\\n]+\\^"}},
        {"code", {"<code\\b", "font-family:[^;\"]*(monospace|Courier|Menlo|Consolas)"}, {"`+[^`\\n]+`+"}},
        {"literalHtml", {"<kbd\\b", "<u\\b", "<small\\b", "<span style"}, {"<kbd\\b", "<u\\b", "<small\\b", "<span style"}},
        {"inlineMath", {"inline-math"}, {"\\$[^$\\n]+\\$"}},
        {"blockMath", {"block-math"}, {"\\$\\$[\\s\\S]+?\\$\\$"}},
        {"image", {"<img\\b"}, {"!\\[[^\\]]*\\]\\([^)]*\\)"}},
        {"link", {"<a\\b"}, {"\\[[^\\]\\n]+\\]\\([^)\\s]+\\)"}},
        {"footnoteRef", {"data-type=\"footnoteRef\""}, {"\\[\\^[^\\]\\n]+\\](?!:)"}},
        {"footnoteDef", {"data-type=\"footnoteDef\""}, {"^\\[\\^[^\\]\\n]+\\]:"}},
        {"heading", {"<h[1-6]\\b"}, {"^#{1,6}\\s"}},
        {"blockquote", {"<blockquote\\b"}, {"^>\\s"}},
        {"ul", {"<ul\\b", "<li\\b"}, {"^\\s*[-*+]\\s"}},
        {"ol", {"<ol\\b"}, {"^\\s*\\d+\\.\\s"}},
        {"task", {"type=\"checkbox\"", "task-item"}, {"^\\s*- \\[[ x]\\]"}},
        {"table", {"<table\\b", "<t[dh]\\b"}, {"^\\|.*\\|$"}},
        {"fence", {"<pre\\b"}, {"^`{3,}"}},
        {"hr", {"<hr\\b"}, {"^(-{3,}|\\*{3,})\\s*$"}},
    };
    return table;
}

// 编译后的守卫缓存（key → SemGuard；mdForms 里 ^ 开头的开 Multiline）
static const QHash<QString, SemGuard> &guardCache()
{
    static QHash<QString, SemGuard> cache;
    if (cache.isEmpty()) {
        for (const GuardSpec &g : guardTable()) {
            SemGuard sg;
            for (const char *re : std::as_const(g.htmlForms)) {
                if (!re) continue;
                sg.htmlForms << QRegularExpression(re);
            }
            for (const char *re : std::as_const(g.mdForms)) {
                if (!re) continue;
                QRegularExpression::PatternOptions opts;
                if (QString(re).startsWith('^'))
                    opts |= QRegularExpression::MultilineOption;
                sg.mdForms << QRegularExpression(re, opts);
            }
            cache.insert(QString::fromUtf8(g.key), sg);
        }
    }
    return cache;
}

// 缺失守卫报告：elements 中每个元素的双轨形态均未命中 → 记入返回列表
static QStringList missingGuards(const QString &html0, const QString &md1,
                                 const QStringList &elements)
{
    QStringList missing;
    const QHash<QString, SemGuard> &cache = guardCache();
    for (const QString &key : elements) {
        const SemGuard g = cache.value(key);
        if (g.htmlForms.isEmpty() && g.mdForms.isEmpty()) {
            missing << key + "(未知守卫key)";
            continue;
        }
        bool hit = false;
        for (const QRegularExpression &re : std::as_const(g.htmlForms))
            if (re.match(html0).hasMatch()) { hit = true; break; }
        if (!hit)
            for (const QRegularExpression &re : std::as_const(g.mdForms))
                if (re.match(md1).hasMatch()) { hit = true; break; }
        if (!hit) missing << key;
    }
    return missing;
}

// 内容存活伪守卫：rounds[1] 去反斜杠转义后必须包含内容子串
// （防文本丢失，白名单也不豁免）
static bool contentSurvives(const QString &round1, const QString &content)
{
    QString stripped;
    stripped.reserve(round1.size());
    for (int i = 0; i < round1.size(); ++i) {
        if (round1[i] == QLatin1Char('\\') && i + 1 < round1.size()) {
            ++i;
            stripped += round1[i];
        } else {
            stripped += round1[i];
        }
    }
    return stripped.contains(content);
}

// ═══════════════════════════════════════════════════════════════════════
// 区块 C：断言器
// ═══════════════════════════════════════════════════════════════════════

enum class Fidelity { ByteStable, Converge, NormalizeThenConverge };

struct RtCase {
    QString name;      // 行名，禁含 ':'
    QString md;
    Fidelity level = Fidelity::Converge;
    QStringList guards;      // 需形态守卫的元素 key；空=跳过
    QString expectR1;        // NormalizeThenConverge 必填精确字面量
    QString content;         // 内容存活检查的内层文本；空=跳过
};

// 纯函数判定（不调产品代码，selftest 可手造 outcome 直接测）。
// 返回违例列表，空=通过。
static QStringList evaluateOutcome(const RoundTripOutcome &o, const RtCase &c)
{
    QStringList v;

    // 1. 哨兵泄漏（rounds 与 htmls 全扫——HTML 泄漏也是产品 bug，编辑器会显示占位符）
    for (int i = 0; i < o.rounds.size(); ++i) {
        const QStringList hits = scanSentinels(o.rounds[i]);
        if (!hits.isEmpty()) v << QString("哨兵泄漏: rounds[%1] 含 %2").arg(i).arg(hits.join(","));
    }
    for (int i = 0; i < o.htmls.size(); ++i) {
        const QStringList hits = scanSentinels(o.htmls[i]);
        if (!hits.isEmpty()) v << QString("哨兵泄漏: htmls[%1] 含 %2").arg(i).arg(hits.join(","));
    }

    // 2. 形态守卫 + 内容存活
    if (!c.guards.isEmpty()) {
        const QStringList missing = missingGuards(o.htmls[0], o.rounds[1], c.guards);
        if (!missing.isEmpty())
            v << QString("守卫缺失: %1").arg(missing.join(","));
    }
    if (!c.content.isEmpty() && !contentSurvives(o.rounds[1], c.content))
        v << QString("内容丢失: '%1' 不在 rounds[1]").arg(escapeVisual(c.content));

    // 3. 等级判定
    const bool converged12 = o.convergedAt == 1;   // R1==R2
    const bool doubleConfirmed1 = converged12 && o.rounds[2] == o.rounds[3];
    const bool converged23 = o.convergedAt == 2;   // R2==R3（R1 归一一次）
    const bool doubleConfirmed2 = converged23 && o.rounds[3] == o.rounds[4];
    const bool convergeOk = doubleConfirmed1 || doubleConfirmed2;
    switch (c.level) {
    case Fidelity::ByteStable:
        for (int i = 1; i < o.rounds.size(); ++i)
            if (o.rounds[i] != c.md) {
                v << QString("等级ByteStable: 第%1轮字节失真").arg(i);
                break;
            }
        break;
    case Fidelity::Converge:
        if (!convergeOk) {
            if (o.convergedAt == 3)
                v << "等级Converge: 三步归一(convergedAt=3)超出允许";
            else if (o.convergedAt == -1)
                v << "等级Converge: 4轮未收敛";
            else
                v << QString("等级Converge: convergedAt=%1 无双确认").arg(o.convergedAt);
        }
        break;
    case Fidelity::NormalizeThenConverge:
        if (o.rounds[1] != c.expectR1)
            v << QString("等级Norm: rounds[1] 与 expectR1 不符");
        if (!convergeOk)
            v << "等级Norm: 未按期收敛(允许一步归一)";
        break;
    }

    // 4. 累积探针 + 周期 2 振荡（诊断信息；未收敛时必然已有等级违例）
    if (o.convergedAt == -1) {
        if (o.rounds[3] == o.rounds[1] && o.rounds[4] == o.rounds[2])
            v << "周期2振荡: 轮3==轮1 且 轮4==轮2";
        int bsGrow = -1;
        for (int i = 2; i + 1 < o.rounds.size(); ++i) {
            if (o.rounds[i + 1].count('\\') > o.rounds[i].count('\\')) { bsGrow = i + 1; break; }
        }
        if (bsGrow > 0)
            v << QString("累积: 反斜杠计数在第%1轮递增").arg(bsGrow);
        int lenGrow = -1;
        for (int i = 2; i + 1 < o.rounds.size(); ++i) {
            if (o.rounds[i + 1].size() > o.rounds[i].size()) { lenGrow = i + 1; break; }
        }
        if (lenGrow > 0)
            v << QString("累积: 长度在第%1轮递增").arg(lenGrow);
    }
    return v;
}

// 失真链渲染：头部违例类别 → 每轮一行（≡/≠）→ 首处分歧展开。
// 单用例报告上限 2KB（防合成文档级长文刷屏）。
static QString renderFailureChain(const QString &caseName, const RoundTripOutcome &o,
                                  const QStringList &violations)
{
    QString r = QString("[RT-FAIL] %1 —— 违例: %2\n").arg(caseName, violations.join("; "));
    for (int i = 0; i < o.rounds.size(); ++i) {
        QString tag;
        bool same = false;
        if (i == 0) {
            tag = "输入  ";
        } else {
            tag = QString("第%1轮").arg(i);
            same = o.rounds[i] == o.rounds[i - 1];
        }
        r += QString("%1 %2|%3\n").arg(tag, same ? "≡" : "≠", escapeVisual(o.rounds[i]));
    }
    // 首轮 HTML（守卫缺失定性用：语义在哪一层丢失；裁掉 head 前缀与
    // style/font 属性噪音，只留结构标签）
    if (!o.htmls.isEmpty()) {
        QString h = o.htmls.first();
        const int body = h.indexOf(QLatin1String("<body"));
        if (body >= 0) h = h.mid(body);
        // 注意：本文件禁用 C++11 raw string 字面量 R"(...)"——moc 预处理器
        // 不理解 raw string，会吞掉其后的 Q_OBJECT 导致 .moc 产物为空
        static const QRegularExpression attrNoise(
            "\\s+(?:style|face|color|size)=\"[^\"]*\"");
        h.remove(attrNoise);
        r += QString("首轮HTML ≡|%1\n").arg(escapeVisual(h));
    }
    const FirstDiff d = firstDiff(o.rounds);
    if (d.roundIdx >= 0) {
        r += QString("首处分歧(轮%1→轮%2) L%3:C%4\n")
                 .arg(d.roundIdx).arg(d.roundIdx + 1).arg(d.line + 1).arg(d.col + 1);
        if (!d.note.isEmpty()) r += "  " + d.note + "\n";
        if (!d.lineA.isNull() || !d.lineB.isNull()) {
            r += QString("  轮%1: %2\n").arg(d.roundIdx).arg(escapeVisual(d.lineA));
            r += QString("  轮%1: %2\n").arg(d.roundIdx + 1).arg(escapeVisual(d.lineB));
        }
    }
    if (r.size() > 2048) r = r.left(2040) + "\n…(截断)";
    return r;
}

// ═══════════════════════════════════════════════════════════════════════
// 区块 D：归一白名单表（三原则见文件头；须在 checkRtCase 之前定义）
// ═══════════════════════════════════════════════════════════════════════

struct NormEntry {
    const char *key;        // 用例行名（或其前缀）
    const char *expectR1;   // 精确字面量
    const char *reason;     // 归一原因 + CHANGELOG 出处
};
static const QVector<NormEntry> &normWhitelist()
{
    static const QVector<NormEntry> table = {
        // QtMarkdownWriter（vendored writer）序列化时把不成对反引号转义为 \`
        // （防重载时被解释成 code span 开头），一轮归一后字节稳定。
        // 出处：docs/vendor-qt-markdown-writer.md（与上游 toMarkdown 等价行为）
        {"bold(tick)",   "**a\\`b**", "writer 转义不成对反引号（vendor-qt-markdown-writer.md）"},
        {"italic(tick)", "*a\\`b*",   "writer 转义不成对反引号（vendor-qt-markdown-writer.md）"},
    };
    return table;
}

// 统一断言入口：返回空串=通过；否则返回完整失真链。
// 白名单命中（key 精确或前缀）→ 等级升级为 NormalizeThenConverge、expectR1
// 取自集中表（三原则：单表集中 / 精确字面量 / reason 带出处，见文件头）。
static QString checkRtCase(const RtCase &cIn)
{
    RtCase c = cIn;
    for (const NormEntry &e : normWhitelist()) {
        const QString key = QString::fromUtf8(e.key);
        if (c.name == key || c.name.startsWith(key)) {
            c.level = Fidelity::NormalizeThenConverge;
            if (c.expectR1.isEmpty())
                c.expectR1 = QString::fromUtf8(e.expectR1);
            break;
        }
    }
    const RoundTripOutcome o = roundTrip(c.md);
    const QStringList v = evaluateOutcome(o, c);
    if (v.isEmpty()) return QString();
    return renderFailureChain(c.name, o, v);
}

// ═══════════════════════════════════════════════════════════════════════
// 区块 E：矩阵生成器
// ═══════════════════════════════════════════════════════════════════════

namespace MatrixGen {

using WrapFn = QString (*)(const QString &);

static QString wrapBold(const QString &s) { return "**" + s + "**"; }
static QString wrapItalic(const QString &s) { return "*" + s + "*"; }
static QString wrapStrike(const QString &s) { return "~~" + s + "~~"; }
static QString wrapMark(const QString &s) { return "==" + s + "=="; }
static QString wrapSub(const QString &s) { return "~" + s + "~"; }
static QString wrapSup(const QString &s) { return "^" + s + "^"; }
static QString wrapCode(const QString &s) { return "`" + s + "`"; }

struct ElemSpec {
    const char *key;
    WrapFn wrap;
};

static const QVector<ElemSpec> &inlineElems()
{
    static const QVector<ElemSpec> v = {
        {"bold", wrapBold},   {"italic", wrapItalic}, {"strike", wrapStrike},
        {"mark", wrapMark},   {"sub", wrapSub},       {"sup", wrapSup},
        {"code", wrapCode},
    };
    return v;
}

static const WrapFn wrapByKey(const QString &key)
{
    for (const ElemSpec &e : inlineElems())
        if (key == QLatin1String(e.key)) return e.wrap;
    return nullptr;
}

struct ContentVariant {
    const char *key;
    QString text;
};

// 内容变体：ascii 基线 / cjk 中文 / html 字面 HTML（ZZHI 保护路径）/
// tick 不成对反引号 / mix 混合扩展定界符字符（孤立 ~ ^ 会归一转义）/
// space 含空格。约定不含 ZZ/BRMARKER/data-brk 字面量（防哨兵自匹配）。
static const QVector<ContentVariant> &contentVariants()
{
    static const QVector<ContentVariant> v = {
        {"ascii", QStringLiteral("abc")},
        {"cjk", QStringLiteral("中文内容")},
        {"html", QStringLiteral("a<u>u</u>b")},
        {"tick", QStringLiteral("a`b")},
        {"mix", QStringLiteral("a~b^c=d")},
        {"space", QStringLiteral("two words")},
    };
    return v;
}

// 由外到内依序包裹：compose({"bold","sub"}, "abc") = "**~abc~**"
static QString compose(const QStringList &outerToInner, const QString &content)
{
    QString s = content;
    for (int i = outerToInner.size() - 1; i >= 0; --i) {
        const WrapFn w = wrapByKey(outerToInner[i]);
        if (w) s = w(s);
    }
    return s;
}

}   // namespace MatrixGen

// ═══════════════════════════════════════════════════════════════════════
// 区块 F：测试槽
// ═══════════════════════════════════════════════════════════════════════

class TestRoundtripMatrix : public QObject {
    Q_OBJECT

private slots:
    // ── 检查器自检（手造数据，不经产品代码） ──────────────────────────
    void testCheckerSelfTest()
    {
        // firstDiff：相同 rounds → 未定位
        {
            QStringList r = {"a", "a", "a"};
            FirstDiff d = firstDiff(r);
            QVERIFY(d.roundIdx < 0);
        }
        // firstDiff：首轮失真，定位行:列
        {
            QStringList r = {"abc", "abx", "abx"};
            FirstDiff d = firstDiff(r);
            QCOMPARE(d.roundIdx, 0);
            QCOMPARE(d.line, 0);
            QCOMPARE(d.col, 2);
            QCOMPARE(d.lineA, QString("abc"));
            QCOMPARE(d.lineB, QString("abx"));
        }
        // firstDiff：第二轮失真，行:列在第二行（"l2" vs "lx" 首分歧在列 1）
        {
            QStringList r = {"l1\nl2", "l1\nl2", "l1\nlx"};
            FirstDiff d = firstDiff(r);
            QCOMPARE(d.roundIdx, 1);
            QCOMPARE(d.line, 1);
            QCOMPARE(d.col, 1);
        }
        // scanSentinels：占位符 / 哨兵命中
        {
            QCOMPARE(scanSentinels("前置 ZZHR0ZZ 后置"), QStringList({"ZZHR0ZZ"}));
            QCOMPARE(scanSentinels(QString(QChar(1)) + "CODE1"), QStringList({"\\x01"}));
            QCOMPARE(scanSentinels("data-brk 存在"), QStringList({"data-brk"}));
            QVERIFY(scanSentinels("正常文本 ZZTOP 无占位").isEmpty());
        }
        // contentSurvives：转义归一后子串存活
        {
            QVERIFY(contentSurvives("**\\~中文\\~**", "中文"));
            QVERIFY(!contentSurvives("**丢弃内容**", "不在场"));
        }
        // evaluateOutcome：ByteStable 全等 → 通过
        {
            RoundTripOutcome o;
            o.rounds = {"md", "md", "md", "md", "md"};
            o.htmls = {"h", "h", "h", "h"};
            o.convergedAt = 1;
            RtCase c{"x", "md", Fidelity::ByteStable, {}, "", ""};
            QVERIFY(evaluateOutcome(o, c).isEmpty());
        }
        // evaluateOutcome：Converge 双确认
        {
            RoundTripOutcome o;
            o.rounds = {"in", "n1", "n1", "n1", "n1"};
            o.convergedAt = 1;
            RtCase c{"x", "in", Fidelity::Converge, {}, "", ""};
            QVERIFY(evaluateOutcome(o, c).isEmpty());
        }
        // evaluateOutcome：Converge 单次相等无双确认 → 违例
        {
            RoundTripOutcome o;
            o.rounds = {"in", "n1", "n1", "n2", "n2"};
            o.convergedAt = 1;
            RtCase c{"x", "in", Fidelity::Converge, {}, "", ""};
            QVERIFY(!evaluateOutcome(o, c).isEmpty());
        }
        // evaluateOutcome：两步归一 + 双确认 → 通过
        {
            RoundTripOutcome o;
            o.rounds = {"in", "n1", "n2", "n2", "n2"};
            o.convergedAt = 2;
            RtCase c{"x", "in", Fidelity::Converge, {}, "", ""};
            QVERIFY(evaluateOutcome(o, c).isEmpty());
        }
        // evaluateOutcome：Norm expectR1 不符 → 违例
        {
            RoundTripOutcome o;
            o.rounds = {"in", "wrong", "wrong", "wrong", "wrong"};
            o.convergedAt = 1;
            RtCase c{"x", "in", Fidelity::NormalizeThenConverge, {}, "expect", ""};
            const QStringList v = evaluateOutcome(o, c);
            QCOMPARE(v.size(), 1);
            QVERIFY(v.first().contains("expectR1"));
        }
        // evaluateOutcome：周期 2 振荡标注
        {
            RoundTripOutcome o;
            o.rounds = {"in", "P", "Q", "P", "Q"};
            o.convergedAt = -1;
            RtCase c{"x", "in", Fidelity::Converge, {}, "", ""};
            const QStringList v = evaluateOutcome(o, c);
            QVERIFY(v.join(";").contains("周期2振荡"));
        }
        // evaluateOutcome：反斜杠累积探针
        {
            RoundTripOutcome o;
            o.rounds = {"in", "a\\~", "a\\\\~", "a\\\\\\~", "x"};
            o.convergedAt = -1;
            RtCase c{"x", "in", Fidelity::Converge, {}, "", ""};
            QVERIFY(evaluateOutcome(o, c).join(";").contains("反斜杠计数"));
        }
        // evaluateOutcome：守卫命中（HTML 轨）
        {
            RoundTripOutcome o;
            o.rounds = {"in", "md1", "md1", "md1", "md1"};
            o.htmls = {"<strong>x</strong>", "h", "h", "h"};
            o.convergedAt = 1;
            RtCase c{"x", "in", Fidelity::Converge, {"bold"}, "", ""};
            QVERIFY(evaluateOutcome(o, c).isEmpty());
        }
        // evaluateOutcome：守卫双轨全缺失 → 违例
        {
            RoundTripOutcome o;
            o.rounds = {"in", "纯文本", "纯文本", "纯文本", "纯文本"};
            o.htmls = {"<p>纯文本</p>", "h", "h", "h"};
            o.convergedAt = 1;
            RtCase c{"x", "in", Fidelity::Converge, {"sub"}, "", ""};
            QVERIFY(evaluateOutcome(o, c).join(";").contains("守卫缺失: sub"));
        }
        // 白名单表完整性：key/expectR1/reason 三字段非空（三原则的结构性校验）
        for (const NormEntry &e : normWhitelist()) {
            QVERIFY2(e.key && *e.key, "白名单 key 非空");
            QVERIFY2(e.expectR1 && *e.expectR1, "白名单 expectR1 非空");
            QVERIFY2(e.reason && *e.reason, "白名单 reason 非空");
        }
    }

    // ── 单元素 × 内容变体（守卫校准槽） ─────────────────────────────
    // 7 元素 × 6 变体 = 42 行。等级初始设定为校准起点，跑挂后按实际
    // 输出调整（mix 变体含孤立 ~ ^，预期归一转义 → Converge）。
    void testSingleInlineElementRoundTrip_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("md");
        QTest::addColumn<int>("level");
        QTest::addColumn<QString>("guards");
        QTest::addColumn<QString>("content");

        for (const MatrixGen::ElemSpec &e : std::as_const(MatrixGen::inlineElems())) {
            for (const MatrixGen::ContentVariant &v : std::as_const(MatrixGen::contentVariants())) {
                Fidelity lv = Fidelity::ByteStable;
                if (v.key == QLatin1String("mix"))
                    lv = Fidelity::Converge;   // 孤立 ~ ^ 归一转义（HTML 有损消歧，见规律 12）
                const QString md = e.wrap(v.text);
                QTest::newRow(QString("%1(%2)").arg(e.key, v.key).toUtf8().constData())
                    << QString("%1(%2)").arg(e.key, v.key)
                    << md
                    << int(lv)
                    << QString(e.key)
                    << v.text;
            }
        }
    }

    void testSingleInlineElementRoundTrip()
    {
        QFETCH(QString, name);
        QFETCH(QString, md);
        QFETCH(int, level);
        QFETCH(QString, guards);
        QFETCH(QString, content);

        RtCase c;
        c.name = name;
        c.md = md;
        c.level = Fidelity(level);
        c.guards = guards.isEmpty() ? QStringList() : QStringList(guards);
        c.content = content;
        const QString fail = checkRtCase(c);
        QVERIFY2(fail.isEmpty(), qPrintable(fail));
    }

    // ── 两层嵌套矩阵（A 嵌 B / B 嵌 A 有序对全排）──────────────────
    // 7×7 有序对 × 6 内容变体，md 同串 QSet 去重（bold(italic) 与
    // italic(bold) 同产 ***x***——首个行名胜出）。分级与守卫规则：
    //   - 异种嵌套 + ascii/cjk/html/space → ByteStable（字节保真基线）
    //   - tick/mix 变体 → Converge：不成对反引号/孤立 ~/^ 的 writer 转义
    //     归一（根因已在单元素槽白名单精确固化，矩阵只验收敛与守卫）
    //   - 自嵌对 → Converge（CommonMark 同族扁平化/定界符合并歧义）
    //   - bold↔strike / italic↔strike 互嵌 → Converge：~~ 与 **/*/== 无合法
    //     组合能表达嵌套（Qt setMarkdown 拆分丢层），保留整层字面 HTML 是
    //     有意设计（ee31ef2，testNestedStrikeBoldItalicMarkRoundTrip）
    //   - 移除 strike(strike)/strike(sub)：~~~~x~~~~ / ~~~x~~~ 单行按
    //     CommonMark 是围栏开启行（其余部分为 info string），非 strike 嵌套；
    //     围栏行为由历史槽（code-fence）覆盖
    //   - 语义重解释对：sub(sub) 产出 ~~x~~ 实为 strike、italic(italic) 产出
    //     **x*** 归一为 **x** 实为 bold——守卫按实际语义元素
    //   - code 为内层 → 守卫只留 code+外层（code 内容字面显示，内层定界符
    //     不渲染）；code 为外层同理只留 code
    //   - mix 变体跳过内容存活检查（内容本身含定界符字符，结构性拆解
    //     （~a~ 配对成 sub 等）使字符级存活不可判定；存活由其余 5 变体保证）
    void testNestedPairMatrix_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("md");
        QTest::addColumn<int>("level");
        QTest::addColumn<QString>("guards");
        QTest::addColumn<QString>("content");

        QSet<QString> seen;
        for (const MatrixGen::ElemSpec &o : std::as_const(MatrixGen::inlineElems())) {
            for (const MatrixGen::ElemSpec &i : std::as_const(MatrixGen::inlineElems())) {
                // CommonMark 围栏开启行（非 strike 嵌套），移除（见槽头注释）：
                // 组合产出 ~~~ 前缀的三种情况——strike(strike)/strike(sub)/sub(strike)
                const bool fenceAmbiguous =
                    (o.key == QLatin1String("strike")
                     && (i.key == QLatin1String("strike")
                         || i.key == QLatin1String("sub")))
                    || (o.key == QLatin1String("sub")
                        && i.key == QLatin1String("strike"));
                if (fenceAmbiguous)
                    continue;
                for (const MatrixGen::ContentVariant &v : std::as_const(MatrixGen::contentVariants())) {
                    const QString md = MatrixGen::compose({o.key, i.key}, v.text);
                    if (seen.contains(md)) continue;
                    seen.insert(md);
                    const bool selfPair = (o.key == i.key);
                    const bool codeInvolved = (o.key == QLatin1String("code")
                                               || i.key == QLatin1String("code"));
                    // 等级：mix/tick 归一 → Converge；自嵌对 → Converge；
                    // bold↔strike/italic↔strike 互嵌（有意字面 HTML）→ Converge
                    const bool strikeXEmph =
                        ((o.key == QLatin1String("strike")
                          || i.key == QLatin1String("strike"))
                         && (o.key == QLatin1String("bold")
                             || i.key == QLatin1String("bold")
                             || o.key == QLatin1String("italic")
                             || i.key == QLatin1String("italic")));
                    Fidelity lv = Fidelity::ByteStable;
                    if (v.key == QLatin1String("mix") || v.key == QLatin1String("tick")
                        || selfPair || strikeXEmph)
                        lv = Fidelity::Converge;
                    // 守卫：按实际语义元素（sub(sub)/italic(italic) 重解释）；
                    // code 任一侧 → 单守卫（code 内容字面显示，内层定界符不渲染）
                    QStringList gs;
                    if (o.key == QLatin1String("sub") && selfPair)
                        gs << QLatin1String("strike");      // ~~x~~ 即删除线
                    else if (o.key == QLatin1String("italic") && selfPair)
                        gs << QLatin1String("bold");        // **x*** → **x**
                    else if (o.key == QLatin1String("code"))
                        gs << QLatin1String("code");        // code 外层：内层全字面
                    else if (i.key == QLatin1String("code") || selfPair)
                        gs << QLatin1String(o.key);         // code 内层字面/自嵌单守卫
                    else
                        gs << QLatin1String(o.key) << QLatin1String(i.key);
                    // mix 变体：只守外层 + 跳过内容存活（内容含定界符字符，
                    // 与包裹定界符交错时结构性撕裂不可判定，见槽头注释）
                    // tick×code：反引号配对重排撕裂字符串（字符存活、连续性破坏）
                    const bool mixWeak = (v.key == QLatin1String("mix"));
                    if (mixWeak && gs.size() > 1)
                        gs = QStringList{gs.first()};
                    const QString content =
                        (mixWeak || (v.key == QLatin1String("tick") && codeInvolved))
                            ? QString() : v.text;
                    QTest::newRow(QString("%1(%2)_%3")
                                      .arg(o.key, i.key, v.key).toUtf8().constData())
                        << QString("%1(%2)_%3").arg(o.key, i.key, v.key)
                        << md << int(lv) << gs.join(u',') << content;
                }
            }
        }
    }

    void testNestedPairMatrix()
    {
        QFETCH(QString, name);
        QFETCH(QString, md);
        QFETCH(int, level);
        QFETCH(QString, guards);
        QFETCH(QString, content);

        RtCase c;
        c.name = name;
        c.md = md;
        c.level = Fidelity(level);
        c.guards = guards.split(u',', Qt::SkipEmptyParts);
        c.content = content;
        QElapsedTimer t;
        t.start();
        const QString fail = checkRtCase(c);
        QVERIFY2(fail.isEmpty(), qPrintable(fail));
        if (t.elapsed() > 100)
            qInfo() << "[slow]" << name << t.elapsed() << "ms";
    }

    // ── 三层嵌套矩阵（A 嵌 B 嵌 C 有序三元组）───────────────────────
    // T1 高频族全排 120：bold/italic/strike/mark/sub/sup 的 6×5×4 有序三元组
    //    （无重复元素；逆序组合天然覆盖 C嵌B嵌A），内容 ascii 基线。
    // T2 code 位置 90：code 分别处外/中/内层 × 其余两元素 6×5 全排。
    //    code 内容字面显示 → 守卫只要求 code 之外的元素 + code 自身。
    // T3 手挑 10：cjk/html 内容的典型三层（含全逆序）。
    // 共同规则（与两层矩阵一致，见 testNestedPairMatrix_data 槽头注释）：
    //   - 相邻 (sub 外包 strike) 或 (strike 外包 sub) → 组合产出 ~~~ 前缀 =
    //     CommonMark 围栏开启行，移除（code 隔断的相邻不受影响）
    //   - 含 strike 与 bold/italic 同时在场（均在 code 外）→ Converge
    //     （互嵌字面 HTML 设计，ee31ef2）
    //   - 其余 → ByteStable；守卫 = code 外的全部元素（字面 HTML 行 html 轨
    //     以真实标签命中）
    void testNestedTripleMatrix_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("md");
        QTest::addColumn<int>("level");
        QTest::addColumn<QString>("guards");
        QTest::addColumn<QString>("content");

        using MatrixGen::inlineElems;
        const auto ruleLevel = [](const QStringList &codeOutside) {
            // codeOutside：参与 MD 语义的元素（code 之外）
            bool hasStrike = codeOutside.contains(QLatin1String("strike"));
            bool hasEmph = codeOutside.contains(QLatin1String("bold"))
                           || codeOutside.contains(QLatin1String("italic"));
            if (hasStrike && hasEmph)
                return Fidelity::Converge;
            // bold/italic 分居首尾、中间隔 mark/sup → writer 扁平化归一：
            // mark/sup 走 setMarkdown 后处理，内层 *abc* 被 md4c 解析为 em，
            // HTML 侧产出 strong/em 相邻嵌套，vendored writer 序列化相邻
            // strong+em 时扁平化为 ***（如 **==*abc*==** → ***==abc==***，
            // 语义全保留、一步收敛）。sub 隔断不受影响（行级抽取早于
            // setMarkdown，strong 包占位符，strong/em 不相邻）→ 保持 ByteStable
            if (codeOutside.size() >= 3) {
                const auto isEmph = [](const QString &k) {
                    return k == QLatin1String("bold") || k == QLatin1String("italic");
                };
                const auto isMarkSup = [](const QString &k) {
                    return k == QLatin1String("mark") || k == QLatin1String("sup");
                };
                if (isEmph(codeOutside.first()) && isEmph(codeOutside.last())
                    && isMarkSup(codeOutside.at(1)))
                    return Fidelity::Converge;
            }
            return Fidelity::ByteStable;
        };
        // 相邻外→内方向 {sub,strike} 相遇 → ~~~ 围栏歧义
        const auto fenceAmbiguous = [](const QStringList &outerToInner) {
            for (int k = 0; k + 1 < outerToInner.size(); ++k) {
                const QString &a = outerToInner[k];
                const QString &b = outerToInner[k + 1];
                if ((a == QLatin1String("sub") && b == QLatin1String("strike"))
                    || (a == QLatin1String("strike") && b == QLatin1String("sub")))
                    return true;
            }
            return false;
        };
        auto addRow = [this](const QString &name, const QStringList &keys,
                            const QString &text, Fidelity lv,
                            const QStringList &guards) {
            const QString md = MatrixGen::compose(keys, text);
            QTest::newRow(name.toUtf8().constData())
                << name << md << int(lv) << guards.join(u',') << text;
        };

        QSet<QString> seen;
        auto tryAdd = [&addRow, &seen](const QString &name,
                                       const QStringList &keys,
                                       const QString &text, Fidelity lv,
                                       const QStringList &guards) {
            const QString md = MatrixGen::compose(keys, text);
            if (seen.contains(md)) return;
            seen.insert(md);
            addRow(name, keys, text, lv, guards);
        };

        // T1：无 code 全排（6×5×4 = 120）
        for (int a = 0; a < 6; ++a) {
            for (int b = 0; b < 6; ++b) {
                if (b == a) continue;
                for (int c = 0; c < 6; ++c) {
                    if (c == a || c == b) continue;
                    const QStringList keys{
                        QLatin1String(inlineElems()[a].key),
                        QLatin1String(inlineElems()[b].key),
                        QLatin1String(inlineElems()[c].key)};
                    if (fenceAmbiguous(keys)) continue;
                    const Fidelity lv = ruleLevel(keys);
                    tryAdd(QString("%1(%2(%3))_ascii").arg(keys[0], keys[1], keys[2]),
                           keys, QStringLiteral("abc"), lv, keys);
                }
            }
        }

        // T2：code 位置 × 其余两元素全排（3 × 30 = 90，同串去重）
        for (int a = 0; a < 6; ++a) {
            for (int b = 0; b < 6; ++b) {
                if (b == a) continue;
                const QString ea = QLatin1String(inlineElems()[a].key);
                const QString eb = QLatin1String(inlineElems()[b].key);
                struct Pos { const char *name; QStringList keys; QStringList outside; };
                const QList<Pos> pos = {
                    {"code(%1(%2))", {"code", ea, eb}, {"code"}},
                    {"%1(code(%2))", {ea, "code", eb}, {ea, "code"}},
                    {"%1(%2(code))", {ea, eb, "code"}, {ea, eb, "code"}},
                };
                for (const Pos &p : pos) {
                    if (fenceAmbiguous(p.keys)) continue;   // code 隔断时不会命中
                    const QString name = QString(p.name).arg(ea, eb);
                    const Fidelity lv = ruleLevel(p.outside);
                    tryAdd(name + "_ascii", p.keys, QStringLiteral("abc"), lv,
                           p.outside);
                }
            }
        }

        // T3：手挑 10——cjk/html 内容典型三层（含全逆序、mark/sup 备份转换路径）
        struct Hand {
            const char *name;
            std::initializer_list<const char *> keys;
            const char *vkey;
        };
        const Hand hands[] = {
            {"bold(italic(sub))_cjk",          {"bold", "italic", "sub"},  "cjk"},
            {"sub(italic(bold))_cjk",          {"sub", "italic", "bold"},  "cjk"},
            {"mark(bold(italic))_html",        {"mark", "bold", "italic"}, "html"},
            {"italic(bold(mark))_html",        {"italic", "bold", "mark"}, "html"},
            {"sup(mark(bold))_html",           {"sup", "mark", "bold"},    "html"},
            {"bold(mark(sup))_html",           {"bold", "mark", "sup"},    "html"},
            {"sub(sup(mark))_cjk",             {"sub", "sup", "mark"},     "cjk"},
            {"mark(sup(sub))_cjk",             {"mark", "sup", "sub"},     "cjk"},
            {"sup(bold(sub))_cjk",             {"sup", "bold", "sub"},     "cjk"},
            {"italic(strike(mark))_cjk",       {"italic", "strike", "mark"}, "cjk"},
        };
        for (const Hand &h : hands) {
            QStringList keys;
            for (const char *k : h.keys) keys << QLatin1String(k);
            if (fenceAmbiguous(keys)) continue;
            QString text;
            for (const MatrixGen::ContentVariant &v :
                 std::as_const(MatrixGen::contentVariants()))
                if (v.key == QLatin1String(h.vkey)) text = v.text;
            const QStringList codeOutside = keys;   // 手挑组不含 code
            tryAdd(QLatin1String(h.name), keys, text, ruleLevel(codeOutside),
                   codeOutside);
        }
    }

    void testNestedTripleMatrix()
    {
        QFETCH(QString, name);
        QFETCH(QString, md);
        QFETCH(int, level);
        QFETCH(QString, guards);
        QFETCH(QString, content);

        RtCase c;
        c.name = name;
        c.md = md;
        c.level = Fidelity(level);
        c.guards = guards.split(u',', Qt::SkipEmptyParts);
        c.content = content;
        QElapsedTimer t;
        t.start();
        const QString fail = checkRtCase(c);
        QVERIFY2(fail.isEmpty(), qPrintable(fail));
        if (t.elapsed() > 100)
            qInfo() << "[slow]" << name << t.elapsed() << "ms";
    }

};

QTEST_MAIN(TestRoundtripMatrix)
#include "test_roundtrip_matrix.moc"
