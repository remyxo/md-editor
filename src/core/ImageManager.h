#pragma once
#include <QString>
#include <QByteArray>

// 图片落盘工具：把粘贴/拖拽进来的图片字节写入文档同级的 images/ 目录，
// 返回相对文档所在目录的引用路径。纯静态工具类，便于单元测试。
class ImageManager {
public:
    // 将 bytes 写入 docFilePath 同级的 images/ 子目录。
    //   suggestedName: 期望文件名（含扩展名，如 "photo.png"）；为空时按时间戳生成。
    //   fallbackExt:    suggestedName 为空（剪贴板粘贴）时用的扩展名，默认 "png"。
    // 返回相对 docFilePath 所在目录的路径，如 "images/photo.png"；
    // 失败（空字节 / 建目录失败 / 写失败）返回空 QString。
    static QString saveImage(const QString &docFilePath, const QByteArray &bytes,
                             const QString &suggestedName,
                             const QString &fallbackExt = QStringLiteral("png"));
};
