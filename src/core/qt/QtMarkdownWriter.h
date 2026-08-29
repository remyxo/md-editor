// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
//
// 本文件 vendored 自 Qt 6.8.2 源码 src/gui/text/qtextmarkdownwriter_p.h/.cpp
// （qt6-base-6.8.2+dfsg，Debian 打包源码树），用于 md-editor 项目。
//
// 【vendor 原因】QTextDocument::toMarkdown() 硬编码 80 列折行（writeBlock 内
// 局部常量 ColumnLimit，构造函数无 wrap 参数，无公开 API 关闭），且折行算法
// 在「稀疏空格 + 二次折行窗口」场景强制列位切断并吞掉断点字符（中文文档
// 高发，数据不可恢复）。vendor 后可从源头去除折行，并使序列化行为在
// Qt 6.8.2 → 6.10.2 及未来版本间完全固定（本项目兼容性原则）。
//
// 【相对原版的改动】（逐项列出以便对照上游 diff）：
//   1. 类名 QTextMarkdownWriter → QtMarkdownWriter（避免与 Qt 内部符号冲突）
//   2. 删除私有头依赖（qtextdocumentlayout_p.h / qtextdocument_p.h /
//      qtextimagehandler_p.h / qtextmarkdownimporter_p.h —— 实现已逐一核对，
//      前三者全文零使用；第四者仅用 FeatureFrontMatter 枚举值 0x100000，
//      以 kFrontMatterFeature 常量替换）
//   3. 日志类别改为 mdeditor.markdownwriter（原 qt.text.markdown.writer 为
//      Qt 内部类别名，vendored 副本不复用），且默认级别 QtWarningMsg——
//      Qt 内置规则默认关闭 qt.* 的 debug，改名后须显式降级保持同等静默
//   4. 删除 Q_GUI_EXPORT / QT_BEGIN_NAMESPACE 等 Qt 构建体系宏
//   5. LGPL-3.0/GPL-2.0/GPL-3.0 多许可与版权声明原样保留（合规要求）
//   6. 【功能改动】新增 setWrapEnabled(bool)：false 时关闭 80 列折行，
//      每块输出单行（vendor 的根本目的）。默认 true，与上游行为完全等价
//      （等价性由 tests/test_qtmarkdownwriter.cpp 回归验证）
//
// 【许可证合规】本文件为 Qt Company 的 LGPL 作品副本，修改处以上方注释
// 标注。LGPL-3.0 第 4 条允许修改与再分发，须保留本版权与许可声明并标注
// 修改。项目整体许可证须与 LGPL-3.0 组合兼容（GPL-3.0 兼容或满足 LGPL
// 的可替换性义务）。

#ifndef QTMARKDOWNWRITER_H
#define QTMARKDOWNWRITER_H

#include <QtCore/QTextStream>
#include <QtGui/QTextDocument>
#include <QtGui/QTextList>
#include <QtCore/QMap>

class QAbstractItemModel;

class QtMarkdownWriter
{
public:
    QtMarkdownWriter(QTextStream &stream, QTextDocument::MarkdownFeatures features);
    // 关闭/开启 80 列折行（默认开启，与 QTextDocument::toMarkdown() 等价；
    // 关闭后每块单行输出，见文件头改动清单第 6 条）
    void setWrapEnabled(bool wrap) { m_wrapEnabled = wrap; }
    bool writeAll(const QTextDocument *document);
#if QT_CONFIG(itemmodel)
    void writeTable(const QAbstractItemModel *table);
#endif

    int writeBlock(const QTextBlock &block, bool table, bool ignoreFormat, bool ignoreEmpty);
    void writeFrame(const QTextFrame *frame);
    void writeFrontMatter(const QString &fm);

private:
    struct ListInfo {
        bool loose;
    };

    ListInfo listInfo(QTextList *list);
    void setLinePrefixForBlockQuote(int level);

private:
    QTextStream &m_stream;
    QTextDocument::MarkdownFeatures m_features;
    QMap<QTextList *, ListInfo> m_listInfo;
    QString m_linePrefix;
    QString m_codeBlockFence;
    int m_wrappedLineIndent = 0;
    int m_lastListIndent = 1;
    bool m_wrapEnabled = true;  // 改动 6：默认 true 保持上游行为
    bool m_doubleNewlineWritten = false;
    bool m_linePrefixWritten = false;
    bool m_indentedCodeBlock = false;
    bool m_fencedCodeBlock = false;
};

#endif // QTMARKDOWNWRITER_H
