#include "PdfPageGeometry.h"
#include "miniz/miniz.h"
#include <QTransform>
#include <QHash>
#include <QtNumeric>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────
// 结构假设（2026-09-20 实证）：Chromium printToPdf 产物无对象流（ObjStm），
// 对象头「N G obj … endobj」裸露可切分；无 xref-stream。任何环节失配 →
// 对应页 valid=false 或整体返回空，调用方回退纯文本几何——只增不减。
// ─────────────────────────────────────────────────────────────────────────
namespace {

// 字节级空白判断（PDF 分隔符：NUL TAB LF FF CR SP）
inline bool isWs(char c)
{
    return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}
inline bool isDelim(char c)
{
    return isWs(c) || c == '(' || c == ')' || c == '<' || c == '>' || c == '['
        || c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

// 切分出下一个非分隔 token（起点回填 i；数组括号 [ ] 视作可跨的边界）
QByteArray nextToken(const QByteArray &s, qsizetype &i)
{
    while (i < s.size() && (isWs(s[i]) || s[i] == '[' || s[i] == ']'))
        ++i;
    qsizetype e = i;
    while (e < s.size() && !isDelim(s[e]))
        ++e;
    QByteArray t = s.mid(i, e - i);
    i = e;
    return t;
}

// dict 里取 /Key 后的值原文（跳空白；数组取平衡 ]、字典取平衡 >>、字符串取
// 平衡 )、其余取到分隔符）。找不到返回空。/KeyXxx 误命中防御：Key 后紧跟
// 字母的跳过（如搜 Page 不吃 /PageSize）
QByteArray dictValue(const QByteArray &dict, const char *key)
{
    const QByteArray pat = QByteArray("/") + key;
    qsizetype i = dict.indexOf(pat);
    while (i >= 0) {
        const qsizetype after = i + pat.size();
        if (after >= dict.size()
            || !std::isalpha(static_cast<unsigned char>(dict[after])))
            break;
        i = dict.indexOf(pat, i + 1);
    }
    if (i < 0)
        return QByteArray();
    qsizetype q = i + pat.size();
    while (q < dict.size() && isWs(dict[q]))
        ++q;
    if (q >= dict.size())
        return QByteArray();
    qsizetype endQ = q;
    const char c = dict[q];
    if (c == '[') {
        int depth = 0;
        while (endQ < dict.size()) {
            if (dict[endQ] == '[')
                ++depth;
            else if (dict[endQ] == ']') {
                if (--depth == 0) {
                    ++endQ;
                    break;
                }
            }
            ++endQ;
        }
    } else if (c == '<' && q + 1 < dict.size() && dict[q + 1] == '<') {
        int depth = 0;
        while (endQ < dict.size()) {
            if (endQ + 1 < dict.size() && dict[endQ] == '<' && dict[endQ + 1] == '<') {
                ++depth;
                ++endQ;
            } else if (endQ + 1 < dict.size() && dict[endQ] == '>'
                       && dict[endQ + 1] == '>') {
                --depth;
                ++endQ;
                if (depth == 0) {
                    ++endQ;
                    break;
                }
            }
            ++endQ;
        }
    } else if (c == '(') {
        int depth = 0;
        while (endQ < dict.size()) {
            if (dict[endQ] == '\\')
                ++endQ;
            else if (dict[endQ] == '(')
                ++depth;
            else if (dict[endQ] == ')') {
                if (--depth == 0) {
                    ++endQ;
                    break;
                }
            }
            ++endQ;
        }
    } else {
        // 名字值（/Catalog 等）以 '/' 起头，而 '/' 本身是分隔符——名字整体
        // 含首 '/' 一并取出（普通分支碰首字符即停会只剩 "/"）
        if (c == '/')
            ++endQ;
        while (endQ < dict.size() && !isDelim(dict[endQ]))
            ++endQ;
        // 引用值是三 token（N G R）：数字/R 序列贪婪续读到名字或关键字停
        // （只读首 token 会得到孤立 "4"，refList 永远匹配不上）
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+') {
            while (true) {
                qsizetype w = endQ;
                while (w < dict.size() && isWs(dict[w]))
                    ++w;
                qsizetype t = w;
                if (t < dict.size()
                    && (std::isdigit(static_cast<unsigned char>(dict[t]))
                        || dict[t] == '-' || dict[t] == '+')) {
                    while (t < dict.size() && !isDelim(dict[t]))
                        ++t;
                } else if (t < dict.size() && dict[t] == 'R'
                           && (t + 1 >= dict.size() || isDelim(dict[t + 1]))) {
                    ++t;
                } else {
                    break;
                }
                endQ = t;
            }
        }
    }
    return dict.mid(q, endQ - q);
}

struct ObjEntry {
    // 对象字典前缀（obj 头后「<<」到 stream 关键字/endobj 之间）
    QByteArray dict;
    qsizetype dataPos = -1;   // stream 数据起点（无 stream 为 -1）
    qsizetype dataLen = -1;   // 数据长度（按 /Length 或 endstream 定位）
};

// 「N G obj」切分。误报防御：流内二进制偶含「 obj」字节——要求头后跳空白
// 恰为「<<」才收；同号既有条目不覆盖（真实对象恒能重建自己的条目语义）
QHash<int, ObjEntry> splitObjects(const QByteArray &pdf)
{
    QHash<int, ObjEntry> objs;
    qsizetype pos = 0;
    while (true) {
        const qsizetype o = pdf.indexOf(" obj", pos);
        if (o < 0)
            break;
        pos = o + 4;
        // 向前取「N G」数字头（上限 24 字节，防跨对象吞数字）
        qsizetype s = o;
        const qsizetype sMin = o > 24 ? o - 24 : 0;
        while (s > sMin && (std::isdigit(static_cast<unsigned char>(pdf[s - 1]))
                            || pdf[s - 1] == ' '))
            --s;
        const QByteArray head = QByteArray(pdf.constData() + s, int(o - s)).trimmed();
        const int sp = head.indexOf(' ');
        if (sp <= 0)
            continue;
        bool ok1 = false, ok2 = false;
        const int num = head.left(sp).toInt(&ok1);
        head.mid(sp + 1).trimmed().toInt(&ok2);
        if (!ok1 || !ok2 || num <= 0)
            continue;
        // 头后跳空白须为「<<」（二进制流内的伪头几乎不可能满足）
        qsizetype d = o + 4;
        while (d < pdf.size() && isWs(pdf[d]))
            ++d;
        if (d + 1 >= pdf.size() || pdf[d] != '<' || pdf[d + 1] != '<')
            continue;
        const qsizetype end = pdf.indexOf("endobj", d);
        if (end < 0)
            continue;
        ObjEntry e;
        // stream 关键字定位：dict 区内不含该词（首个命中即真关键字；
        // endstream 在数据之后不干扰）
        const qsizetype st = pdf.indexOf("stream", d);
        if (st >= 0 && st < end && st > d + 1 && pdf[st - 1] != '/') {
            e.dict = pdf.mid(d, st - d);
            // 关键字后恰一个换行（\n 或 \r\n 或 \r）
            qsizetype ds = st + 6;
            if (ds < pdf.size() && pdf[ds] == '\r')
                ++ds;
            if (ds < pdf.size() && pdf[ds] == '\n')
                ++ds;
            e.dataPos = ds;
            // /Length 直取（Chromium 恒直写整数；间接引用 → endstream 兜底）
            const QByteArray lv = dictValue(e.dict, "Length");
            bool okL = false;
            const int len = lv.toInt(&okL);
            if (okL && len >= 0 && e.dataPos + len <= end)
                e.dataLen = len;
            if (e.dataLen < 0) {
                const qsizetype es = pdf.indexOf("endstream", e.dataPos);
                e.dataLen = es >= 0 ? es - e.dataPos : -1;
            }
            if (e.dataLen < 0)
                continue;   // 长度不可定 → 弃（该对象流不可用）
        } else {
            e.dict = pdf.mid(d, end - d);
        }
        // 同号先到先得：流内二进制伪头若与真实对象同号，不得覆盖真条目
        if (!objs.contains(num))
            objs.insert(num, e);
    }
    return objs;
}

// 值串里的全部「N G R」引用（token 全收后扫三连窗口；数组括号由
// nextToken 跨过——/Kids [N 0 R …] 解析依赖此点）
QList<int> refList(const QByteArray &v)
{
    QList<int> out;
    QList<QByteArray> toks;
    qsizetype i = 0;
    while (true) {
        const QByteArray t = nextToken(v, i);
        if (t.isEmpty())
            break;
        toks.append(t);
    }
    for (int k = 0; k + 2 < toks.size(); ++k) {
        if (toks[k + 2] == "R") {
            bool ok1 = false, ok2 = false;
            const int n = toks[k].toInt(&ok1);
            toks[k + 1].toInt(&ok2);
            if (ok1 && ok2 && n > 0)
                out.append(n);
        }
    }
    return out;
}

// Flate 解压（miniz，zlib 包装格式）。容量未知时倍增重试（上限 64MB）
QByteArray inflate(const QByteArray &src)
{
    if (src.isEmpty())
        return QByteArray();
    mz_ulong cap = src.size() * 4 + 64;
    while (cap < 64ull * 1024 * 1024) {
        QByteArray out(int(cap), '\0');
        mz_ulong dlen = cap;
        const int r = mz_uncompress(reinterpret_cast<mz_uint8 *>(out.data()), &dlen,
                                    reinterpret_cast<const mz_uint8 *>(src.constData()),
                                    mz_ulong(src.size()));
        if (r == MZ_OK)
            return out.left(int(dlen));
        if (r != MZ_BUF_ERROR)
            return QByteArray();   // 数据损坏，非容量不足
        cap *= 2;
    }
    return QByteArray();
}

// 对象流数据：/FlateDecode 解压，否则原样
QByteArray streamData(const QByteArray &pdf, const ObjEntry &e)
{
    if (e.dataPos < 0 || e.dataLen < 0 || e.dataPos + e.dataLen > pdf.size())
        return QByteArray();
    const QByteArray raw = pdf.mid(e.dataPos, e.dataLen);
    if (e.dict.contains("/FlateDecode"))
        return inflate(raw);
    return raw;
}

// XObject 名 → 对象号。/XObject 值为名字字典（<< /Name N G R ... >>）或其
// 间接引用
QHash<QByteArray, int> parseXObjects(const QByteArray &dict,
                                     const QHash<int, ObjEntry> &objs)
{
    QHash<QByteArray, int> out;
    QByteArray v = dictValue(dict, "XObject");
    if (v.isEmpty())
        return out;
    if (!v.trimmed().startsWith("<<")) {
        // 间接引用：目标对象的 dict 即名字字典
        const QList<int> rr = refList(v);
        if (rr.size() != 1 || !objs.contains(rr[0]))
            return out;
        v = dictValue(objs[rr[0]].dict, "XObject");
        if (v.isEmpty() || !v.trimmed().startsWith("<<"))
            return out;
    }
    // 剥 << >> 外壳：扫描器从首个 '/' 名字起步，带着外壳会在首字符即 break
    v = v.trimmed().mid(2, v.size() - 4);
    qsizetype i = 0;
    while (i < v.size()) {
        while (i < v.size() && isWs(v[i]))
            ++i;
        if (i >= v.size() || v[i] != '/')
            break;
        qsizetype e = i + 1;
        while (e < v.size() && !isDelim(v[e]))
            ++e;
        const QByteArray name = v.mid(i + 1, e - i - 1);
        // 名后三 token「N G R」为值
        qsizetype q = e;
        const QByteArray t1 = nextToken(v, q);
        const QByteArray t2 = nextToken(v, q);
        const QByteArray t3 = nextToken(v, q);
        if (!name.isEmpty() && t3 == "R") {
            bool ok1 = false, ok2 = false;
            const int num = t1.toInt(&ok1);
            t2.toInt(&ok2);
            if (ok1 && ok2 && num > 0)
                out.insert(name, num);
            i = q;
        } else {
            i = e;   // 形态不符（防御），跳过此名继续扫
        }
    }
    return out;
}

// 数组值（[a b c …]）取全部数值
QList<qreal> numList(const QByteArray &v)
{
    QList<qreal> out;
    qsizetype i = 0;
    while (i < v.size()) {
        while (i < v.size() && (isWs(v[i]) || v[i] == '[' || v[i] == ']'))
            ++i;
        qsizetype s = i;
        while (s < v.size() && !isDelim(v[s]) && v[s] != ']')
            ++s;
        if (s > i) {
            bool ok = false;
            const qreal r = v.mid(i, s - i).toDouble(&ok);
            if (ok)
                out.append(r);
        }
        i = s;
    }
    return out;
}

// 内容流扫描器：CTM/qQ 栈 + 文本矩阵 + 填充色 + 绘制操作取点。输出：
// minY = 最小设备 y（内容底）；footerMinY = #666 填充色下半页文本的最小
// 设备 y（页码标记基线——文档内唯一下半页的该色文本，表头同色恒在页顶）
class ContentScanner {
public:
    struct GState {
        QTransform ctm;
        qreal fr = 0, fg = 0, fb = 0;   // 填充色（rg/g/k/scn 近似归一）
    };

    ContentScanner(const QByteArray &pdf, const QHash<int, ObjEntry> &objs)
        : m_pdf(pdf), m_objs(objs) {}

    void scan(const QByteArray &content, QTransform ctm,
              const QHash<QByteArray, int> &xobjs, qreal &minY, int &ops,
              int depth, qreal footerZoneY, qreal &footerMinY)
    {
        QVector<GState> stack;
        GState gs;
        gs.ctm = ctm;
        QTransform tlm;              // 文本行矩阵
        QTransform tm;               // 文本矩阵
        qreal tl = 0;                // 行距（T* 用）
        QVector<double> nums;        // 数值操作数（数组内的不入栈）
        QByteArray lastName;         // 最近名字操作数（Do 等）
        int bracket = 0;             // [] 深度（TJ 数组内的数不进操作数栈）
        // 路径挂起纪律：构造操作（m/l/c/v/y/re）只累积不记账，见 dispatch
        QVector<QPointF> pending;
        QPointF cur;                 // 当前点（曲线端点/反射控制点推算用）
        QPointF prevC2;              // 上一段曲线的第二控制点（v 反射推算）
        const qsizetype n = content.size();
        qsizetype i = 0;
        while (i < n) {
            const char c = content[i];
            if (isWs(c)) {
                ++i;
                continue;
            }
            if (c == '%') {   // 注释到行尾
                while (i < n && content[i] != '\n' && content[i] != '\r')
                    ++i;
                continue;
            }
            if (c == '(') {   // 字符串：平衡括号跳过（\ 转义）
                int depth = 0;
                while (i < n) {
                    if (content[i] == '\\') {
                        i += 2;
                        continue;
                    }
                    if (content[i] == '(')
                        ++depth;
                    else if (content[i] == ')') {
                        if (--depth == 0) {
                            ++i;
                            break;
                        }
                    }
                    ++i;
                }
                continue;
            }
            if (c == '<') {
                if (i + 1 < n && content[i + 1] == '<') {
                    // 内联字典（BDC 属性等）：平衡 <<>> 跳过；内部括号字符串
                    // 里的 <> 需感知，免得深度算崩
                    int depth = 0;
                    while (i < n) {
                        if (i + 1 < n && content[i] == '<' && content[i + 1] == '<') {
                            ++depth;
                            i += 2;
                        } else if (i + 1 < n && content[i] == '>'
                                   && content[i + 1] == '>') {
                            --depth;
                            i += 2;
                            if (depth == 0)
                                break;
                        } else if (content[i] == '(') {
                            int d2 = 0;
                            while (i < n) {
                                if (content[i] == '\\') {
                                    i += 2;
                                    continue;
                                }
                                if (content[i] == '(')
                                    ++d2;
                                else if (content[i] == ')') {
                                    if (--d2 == 0) {
                                        ++i;
                                        break;
                                    }
                                }
                                ++i;
                            }
                        } else {
                            ++i;
                        }
                    }
                } else {   // 十六进制串
                    while (i < n && content[i] != '>')
                        ++i;
                    if (i < n)
                        ++i;
                }
                continue;
            }
            if (c == '[') {
                ++bracket;
                ++i;
                continue;
            }
            if (c == ']') {
                if (bracket > 0)
                    --bracket;
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < n && !isDelim(content[i + 1])) {   // 名字
                qsizetype e = i + 1;
                while (e < n && !isDelim(content[e]))
                    ++e;
                lastName = content.mid(i + 1, e - i - 1);
                i = e;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+'
                || c == '.') {
                qsizetype e = i;
                while (e < n && !isDelim(content[e]))
                    ++e;
                if (bracket == 0) {
                    bool ok = false;
                    const double v = content.mid(i, e - i).toDouble(&ok);
                    if (ok)
                        nums.append(v);
                }
                i = e;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '\''
                || c == '"') {
                // 操作符：读完整词
                qsizetype e = i;
                while (e < n && !isDelim(content[e]))
                    ++e;
                const QByteArray op = content.mid(i, e - i);
                i = e;
                dispatch(op, nums, lastName, gs, tlm, tm, tl, stack, xobjs, minY,
                         ops, depth, pending, cur, prevC2, footerZoneY,
                         footerMinY);
                // 规范语义：每个操作符清空操作数栈
                nums.clear();
                lastName.clear();
                continue;
            }
            ++i;   // 其余杂字节
        }
    }

private:
    const QByteArray &m_pdf;
    const QHash<int, ObjEntry> &m_objs;

    static void take(const QVector<double> &nums, int cnt, double *out)
    {
        for (int k = 0; k < cnt; ++k)
            out[k] = nums[nums.size() - cnt + k];
    }

    static void record(const QTransform &ctm, qreal x, qreal y, qreal &minY,
                       int &ops)
    {
        const QPointF d = ctm.map(QPointF(x, y));
        if (d.y() < minY)
            minY = d.y();
        ++ops;
    }

    static QPointF bezier(const QPointF &p0, const QPointF &p1, const QPointF &p2,
                          const QPointF &p3, qreal t)
    {
        const qreal u = 1 - t;
        return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t)
            + p3 * (t * t * t);
    }

    // 挂起路径冲刷：只有绘制操作符（S/s/f/F/f*/B/B*/b/b*/sh）产生墨迹才
    // 记账——W/W* 裁剪与 n 空结束直接丢弃。若不区分，Chromium 每页开头的
    // 裁剪框（q … re W n）会把所有页量成满页，页码标记逐页下漂
    static void paintPending(const QTransform &ctm, const QVector<QPointF> &pending,
                             qreal &minY, int &ops)
    {
        for (const QPointF &p : pending) {
            const QPointF d = ctm.map(p);
            if (d.y() < minY)
                minY = d.y();
            ++ops;
        }
    }

    void dispatch(const QByteArray &op, QVector<double> &nums,
                  const QByteArray &name, GState &gs, QTransform &tlm,
                  QTransform &tm, qreal &tl, QVector<GState> &stack,
                  const QHash<QByteArray, int> &xobjs, qreal &minY, int &ops,
                  int depth, QVector<QPointF> &pending, QPointF &cur,
                  QPointF &prevC2, qreal footerZoneY, qreal &footerMinY)
    {
        double v[6];
        // 路径构造族：累积到挂起缓冲（等绘制操作符冲刷）
        if (op == "m" || op == "l") {
            if (nums.size() >= 2) {
                take(nums, 2, v);
                cur = QPointF(v[0], v[1]);
                pending.append(cur);
            }
        } else if (op == "c") {
            if (nums.size() >= 6) {
                take(nums, 6, v);
                const QPointF p1(v[0], v[1]), p2(v[2], v[3]), p3(v[4], v[5]);
                // 控制点可在实际曲线凸包之外（记账会高估底沿）——端点 +
                // 4 采样点近似曲线本体
                pending.append(cur);
                pending.append(bezier(cur, p1, p2, p3, 0.25));
                pending.append(bezier(cur, p1, p2, p3, 0.5));
                pending.append(bezier(cur, p1, p2, p3, 0.75));
                pending.append(p3);
                prevC2 = p2;
                cur = p3;
            }
        } else if (op == "v" || op == "y") {
            if (nums.size() >= 4) {
                take(nums, 4, v);
                const QPointF a(v[0], v[1]), b(v[2], v[3]);
                // v：p1 = 当前点对上段 p2 的反射，p2=a；y：p1=a，p2=端点
                const QPointF p1 = (op == "v")
                    ? QPointF(2 * cur.x() - prevC2.x(), 2 * cur.y() - prevC2.y())
                    : a;
                const QPointF p2 = (op == "v") ? a : b;
                const QPointF p3 = b;
                pending.append(cur);
                pending.append(bezier(cur, p1, p2, p3, 0.5));
                pending.append(p3);
                prevC2 = p2;
                cur = p3;
            }
        } else if (op == "re") {
            if (nums.size() >= 4) {
                take(nums, 4, v);
                cur = QPointF(v[0], v[1]);
                pending.append(QPointF(v[0], v[1]));
                pending.append(QPointF(v[0] + v[2], v[1]));
                pending.append(QPointF(v[0], v[1] + v[3]));
                pending.append(QPointF(v[0] + v[2], v[1] + v[3]));
            }
        } else if (op == "S" || op == "s" || op == "f" || op == "F" || op == "f*"
                   || op == "B" || op == "B*" || op == "b" || op == "b*"
                   || op == "sh") {
            paintPending(gs.ctm, pending, minY, ops);
            pending.clear();
        } else if (op == "W" || op == "W*" || op == "n" || op == "h") {
            // 裁剪/空结束/闭合：不产生墨迹，路径丢弃（W 后跟 S 罕见，丢
            // 失的是裁剪框本身，非内容）
            pending.clear();
        } else if (op == "q") {
            stack.append(gs);
        } else if (op == "Q") {
            if (!stack.isEmpty())
                gs = stack.takeLast();
        } else if (op == "cm") {
            if (nums.size() >= 6) {
                take(nums, 6, v);
                gs.ctm = QTransform(v[0], v[1], v[2], v[3], v[4], v[5]) * gs.ctm;
            }
        } else if (op == "rg") {   // 填充色 rgb
            if (nums.size() >= 3) {
                take(nums, 3, v);
                gs.fr = v[0];
                gs.fg = v[1];
                gs.fb = v[2];
            }
        } else if (op == "g") {   // 填充色灰
            if (nums.size() >= 1) {
                take(nums, 1, v);
                gs.fr = gs.fg = gs.fb = v[0];
            }
        } else if (op == "k") {   // 填充色 CMYK（近似归一 RGB）
            if (nums.size() >= 4) {
                take(nums, 4, v);
                gs.fr = (1 - v[0]) * (1 - v[3]);
                gs.fg = (1 - v[1]) * (1 - v[3]);
                gs.fb = (1 - v[2]) * (1 - v[3]);
            }
        } else if (op == "sc" || op == "scn") {   // 非stroke色空间（按操作数数近似）
            if (nums.size() >= 4) {   // CMYK
                take(nums, 4, v);
                gs.fr = (1 - v[0]) * (1 - v[3]);
                gs.fg = (1 - v[1]) * (1 - v[3]);
                gs.fb = (1 - v[2]) * (1 - v[3]);
            } else if (nums.size() >= 3) {   // RGB
                take(nums, 3, v);
                gs.fr = v[0];
                gs.fg = v[1];
                gs.fb = v[2];
            } else if (nums.size() >= 1) {   // 灰
                take(nums, 1, v);
                gs.fr = gs.fg = gs.fb = v[0];
            }
        } else if (op == "BT") {
            tlm = QTransform();
            tm = QTransform();
        } else if (op == "Tm") {
            if (nums.size() >= 6) {
                take(nums, 6, v);
                tlm = QTransform(v[0], v[1], v[2], v[3], v[4], v[5]);
                tm = tlm;
            }
        } else if (op == "Td" || op == "TD") {
            if (nums.size() >= 2) {
                take(nums, 2, v);
                if (op == "TD")
                    tl = -v[1];
                tlm = QTransform(1, 0, 0, 1, v[0], v[1]) * tlm;
                tm = tlm;
            }
        } else if (op == "TL") {
            if (nums.size() >= 1) {
                take(nums, 1, v);
                tl = v[0];
            }
        } else if (op == "T*") {
            tlm = QTransform(1, 0, 0, 1, 0, -tl) * tlm;
            tm = tlm;
        } else if (op == "Tj" || op == "TJ" || op == "'" || op == "\"") {
            // 文本绘制：设备点 = CTM(Tm(0,0))——Tm 先把文本空间原点映到
            // 用户空间、再过 CTM（Qt 行向量约定下恰为 tm * ctm；写反会把
            // 基线点算到页外，实测 minY 冲到 -769）。行盒 descender 由调用
            // 方 slack 补（与文本几何路径同一定值）
            if (op == "'" || op == "\"") {
                tlm = QTransform(1, 0, 0, 1, 0, -tl) * tlm;
                tm = tlm;
            }
            const QPointF d = (tm * gs.ctm).map(QPointF(0, 0));
            if (d.y() < minY)
                minY = d.y();
            ++ops;
            // 页码标记识别：rgb(102,102,103) 差值特征（r≈g、b−r∈(0.001,
            // 0.01)）——蓝通道 +1 是页码专用签名（表头 #666 与 mermaid 灰
            // 的 r=g=b 全排除）；用差值而非绝对值，色值整体舍入不敏感
            if (qAbs(gs.fr - gs.fg) < 0.002 && gs.fb > gs.fr + 0.001
                && gs.fb < gs.fr + 0.01 && gs.fr > 0.3 && gs.fr < 0.5
                && d.y() < footerZoneY && d.y() > 0 && d.y() < footerMinY)
                footerMinY = d.y();
        } else if (op == "Do") {
            if (!name.isEmpty() && depth < 8) {
                const int xo = xobjs.value(name, 0);
                if (xo > 0 && m_objs.contains(xo)) {
                    const ObjEntry &e = m_objs[xo];
                    const QByteArray sub = dictValue(e.dict, "Subtype").trimmed();
                    if (sub == "/Form") {
                        // Form：BBox（自有坐标系）经 Matrix × CTM 落位；再递归
                        // 扫内容（自带 /Resources 链，深度 8 防环）
                        QTransform m;
                        const QList<qreal> mt = numList(dictValue(e.dict, "Matrix"));
                        if (mt.size() >= 6)
                            m = QTransform(mt[0], mt[1], mt[2], mt[3], mt[4], mt[5]);
                        const QTransform dev = m * gs.ctm;
                        const QList<qreal> bb = numList(dictValue(e.dict, "BBox"));
                        if (bb.size() >= 4) {
                            record(dev, bb[0], bb[1], minY, ops);
                            record(dev, bb[2], bb[3], minY, ops);
                            record(dev, bb[0], bb[3], minY, ops);
                            record(dev, bb[2], bb[1], minY, ops);
                        }
                        const QByteArray data = streamData(m_pdf, e);
                        if (!data.isEmpty()) {
                            QHash<QByteArray, int> subx
                                = parseXObjects(e.dict, m_objs);
                            if (subx.isEmpty()) {
                                const QList<int> rr
                                    = refList(dictValue(e.dict, "Resources"));
                                if (rr.size() == 1 && m_objs.contains(rr[0]))
                                    subx = parseXObjects(m_objs[rr[0]].dict, m_objs);
                            }
                            scan(data, dev, subx, minY, ops, depth + 1,
                                 footerZoneY, footerMinY);
                        }
                    } else if (sub == "/Image") {
                        // 图片：单位方 [0,1]² 经当前 CTM 落位
                        record(gs.ctm, 0, 0, minY, ops);
                        record(gs.ctm, 1, 1, minY, ops);
                        record(gs.ctm, 0, 1, minY, ops);
                        record(gs.ctm, 1, 0, minY, ops);
                    }
                }
            }
        }
        // 其余操作符（着色参数/标记内容…）不产生几何记录；构造族与绘制族
        // 之外落此处的挂起路径按规范不该存在，遗留到下一操作符会被
        // nums 清栈语义自然阻断（新路径重新从 m 开始）
    }
};

// 页树按 /Kids 递归展开（与 QPdf 页序一致；嵌套 /Pages 深度上限防环）
bool collectKids(int pagesObj, const QHash<int, ObjEntry> &objs, QList<int> &out,
                 int depth)
{
    if (depth > 16 || !objs.contains(pagesObj))
        return false;
    const QList<int> kids = refList(dictValue(objs[pagesObj].dict, "Kids"));
    for (int k : kids) {
        if (!objs.contains(k))
            continue;
        const QByteArray t = dictValue(objs[k].dict, "Type").trimmed();
        if (t == "/Page")
            out.append(k);
        else if (t == "/Pages") {
            if (!collectKids(k, objs, out, depth + 1))
                return false;
        }
    }
    return true;
}

// 页序：Catalog → /Pages → /Kids 展开优先；失败回退按对象号升序
// （Chromium 产物两种序通常一致）
QList<int> pageOrder(const QHash<int, ObjEntry> &objs)
{
    for (auto it = objs.constBegin(); it != objs.constEnd(); ++it) {
        if (dictValue(it.value().dict, "Type").trimmed() != "/Catalog")
            continue;
        const QList<int> pr = refList(dictValue(it.value().dict, "Pages"));
        if (pr.size() != 1)
            continue;
        QList<int> pages;
        if (!collectKids(pr[0], objs, pages, 0) || pages.isEmpty())
            break;
        return pages;
    }
    QList<int> all;
    for (auto it = objs.constBegin(); it != objs.constEnd(); ++it)
        if (dictValue(it.value().dict, "Type").trimmed() == "/Page")
            all.append(it.key());
    std::sort(all.begin(), all.end());
    return all;
}

}   // namespace

QVector<PdfPageGeometry::PageBottom> PdfPageGeometry::measure(const QByteArray &pdf)
{
    QVector<PageBottom> out;
    if (!pdf.startsWith("%PDF"))
        return out;
    const QHash<int, ObjEntry> objs = splitObjects(pdf);
    if (objs.isEmpty())
        return out;
    const QList<int> pages = pageOrder(objs);
    if (qEnvironmentVariableIsSet("HF_DEBUG"))
        qWarning("[geo] objs=%d pages=%d", int(objs.size()), int(pages.size()));
    if (pages.isEmpty())
        return out;

    ContentScanner scanner(pdf, objs);
    for (int pn : pages) {
        const QByteArray pdict = objs[pn].dict;
        // MediaBox / Resources 可继承（父 /Pages 链，上限 32 级防环）。缺啥
        // 补啥：页自带 MediaBox 而缺 /XObject 时仍要上溯找 Resources，但
        // 不得让父级的空值覆盖页级真值（实测 Chromium 页对象自带 MediaBox
        // + 仅 Font 的 Resources，误覆盖使 y1 落回 612×792 默认，页脚 y 反
        // 馈整体差 49pt）
        QByteArray mediaStr, dict = pdict;
        QHash<QByteArray, int> xobjs;
        for (int hop = 0; hop < 32; ++hop) {
            if (mediaStr.isEmpty())
                mediaStr = dictValue(dict, "MediaBox");
            if (xobjs.isEmpty()) {
                const QByteArray resV = dictValue(dict, "Resources");
                if (!resV.isEmpty()) {
                    if (resV.trimmed().startsWith("<<"))
                        xobjs = parseXObjects(dict, objs);
                    else {
                        const QList<int> rr = refList(resV);
                        if (rr.size() == 1 && objs.contains(rr[0]))
                            xobjs = parseXObjects(objs[rr[0]].dict, objs);
                    }
                }
            }
            if (!mediaStr.isEmpty() && !xobjs.isEmpty())
                break;
            const QList<int> parent = refList(dictValue(dict, "Parent"));
            if (parent.size() != 1 || !objs.contains(parent[0]))
                break;
            dict = objs[parent[0]].dict;
        }
        // MediaBox [x0 y0 x1 y1]（缺省 A4 纵向）
        const QList<qreal> mbv = numList(mediaStr);
        const qreal y1 = mbv.size() >= 4 ? mbv[3] : 792.0;
        // /Contents：单引用或数组，多流按序拼接
        QByteArray content;
        const QList<int> crefs = refList(dictValue(pdict, "Contents"));
        for (int r : crefs) {
            if (objs.contains(r))
                content += streamData(pdf, objs[r]);
        }
        PageBottom pb;
        if (!content.isEmpty()) {
            qreal minY = 1e18;
            qreal footerMinY = 1e18;
            int ops = 0;
            // 页码标记识别区：下半页（设备 y-up，y < 30% 页高）
            scanner.scan(content, QTransform(), xobjs, minY, ops, 0,
                         y1 * 0.30, footerMinY);
            if (qEnvironmentVariableIsSet("HF_DEBUG"))
                qWarning("[geo] page%d content=%d ops=%d minY=%.1f footerMinY=%.1f y1=%.1f media=[%s] dict=%dB",
                         pn, int(content.size()), ops, minY, footerMinY, y1,
                         mediaStr.constData(), int(dict.size()));
            if (ops > 0) {
                pb.bottomPt = y1 - minY;   // 顶系底沿（与 QPdf 文本 bounds 同系）
                pb.valid = pb.bottomPt >= 0 && pb.bottomPt <= y1 + 1.0;
            }
            if (footerMinY < 1e17)
                pb.footerBasePt = y1 - footerMinY;   // 顶系基线
        }
        out.append(pb);   // 无效页占位保持页序对齐
    }
    return out;
}
