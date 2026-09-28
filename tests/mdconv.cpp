// mdconv：MarkdownHandler 转换 CLI 桥。
// 用途：vitest 真实链路矩阵经子进程调用真实 C++ 转换；亦作往返排查工具。
// stdin 读全文（UTF-8）→ stdout 输出转换结果；退出码 2=用法错误。
#include <QGuiApplication>
#include <QFile>
#include <cstdio>
#include <cstring>
#include "core/MarkdownHandler.h"

int main(int argc, char *argv[])
{
    // QTextDocument 的字体度量需要 GUI 应用实例；CLI 默认 offscreen，
    // 允许环境变量覆盖（调试平台差异时用）
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (argc != 2
        || (qstrcmp(argv[1], "tohtml") != 0 && qstrcmp(argv[1], "fromhtml") != 0)) {
        std::fprintf(stderr, "用法: mdconv tohtml|fromhtml < in > out (UTF-8)\n");
        return 2;
    }
    QFile in;
    in.open(stdin, QIODevice::ReadOnly);
    const QString input = QString::fromUtf8(in.readAll());
    const QString out = (qstrcmp(argv[1], "tohtml") == 0)
        ? MarkdownHandler::toHtml(input)
        : MarkdownHandler::fromHtml(input);
    const QByteArray utf8 = out.toUtf8();
    std::fwrite(utf8.constData(), 1, utf8.size(), stdout);
    return 0;
}
