#include <QTest>
#include <QTemporaryFile>
#include <QFile>
#include "../src/core/PdfPageGeometry.h"
#include "../src/core/miniz/miniz.h"

// PdfPageGeometry 单元测试：手写最小 PDF 字节夹具，锁「文本之外的绘制内容
// 底沿」三源——路径（m/l/c/re × cm 变换）、Form XObject（BBox × Matrix）、
// 图片（Do 单位方）——以及 clip 路径不记账、Flate 流解压、失败退化语义。
// 这是 nativeHf 页脚定位的 SVG 缺口修复件（2026-09-20），坐标错误会直接
// 造成页码标记漂移、成品校验拦回慢链
class TestPdfPageGeometry : public QObject {
    Q_OBJECT

private slots:
    void testPlainPathBottom();
    void testClipPathNotCounted();
    void testFlateStream();
    void testImageDoUnitSquare();
    void testFormBBoxWithMatrix();
    void testGarbageReturnsEmptyOrInvalid();

private:
    // 造一本单页 PDF（612×792），内容流原样嵌入
    static QByteArray makePdf(const QByteArray &content, const QByteArray &extraObjs,
                              const QByteArray &resources)
    {
        QByteArray pdf("%PDF-1.4\n");
        const QByteArray page3 = QByteArray("<< /Type /Page /Parent 2 0 R "
                                            "/MediaBox [0 0 612 792] "
                                            "/Contents 4 0 R /Resources ")
            + resources + QByteArray(" >>");
        const QByteArray obj4 = QByteArray("<< /Length ")
            + QByteArray::number(content.size()) + QByteArray(" >>\nstream\n")
            + content + QByteArray("\nendstream");
        pdf += "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n";
        pdf += "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n";
        pdf += "3 0 obj " + page3 + " endobj\n";
        pdf += "4 0 obj " + obj4 + " endobj\n";
        if (!extraObjs.isEmpty())
            pdf += extraObjs;
        pdf += "trailer << /Root 1 0 R >>\n%%EOF";
        return pdf;
    }

    static QVector<PdfPageGeometry::PageBottom> measureOf(const QByteArray &pdf)
    {
        QTemporaryFile f;
        if (!f.open())
            return {};
        f.write(pdf);
        f.close();
        QFile rf(f.fileName());
        if (!rf.open(QIODevice::ReadOnly))
            return {};
        return PdfPageGeometry::measure(rf.readAll());
    }
};

// 路径底沿：cm 平移后矩形 y=200..245 → 顶系底 = 792−200 = 592
void TestPdfPageGeometry::testPlainPathBottom()
{
    const QByteArray content = "q 1 0 0 1 100 200 cm 0 0 300 45 re f Q";
    const auto r = measureOf(makePdf(content, "", "<< >>"));
    QCOMPARE(r.size(), 1);
    QVERIFY(r[0].valid);
    QVERIFY2(qAbs(r[0].bottomPt - 592.0) < 0.5,
             qPrintable(QString::number(r[0].bottomPt)));
}

// clip 路径不记账：W n 的裁剪框（贴页底 20pt）不得抬高底沿——若照录每页
// 会被量成满页，页码标记逐页下漂（本件的存在性根因）
void TestPdfPageGeometry::testClipPathNotCounted()
{
    const QByteArray content =
        "q 0 0 612 20 re W n Q\n"
        "1 0 0 1 100 200 cm 0 0 300 45 re f";
    const auto r = measureOf(makePdf(content, "", "<< >>"));
    QCOMPARE(r.size(), 1);
    QVERIFY(r[0].valid);
    QVERIFY2(qAbs(r[0].bottomPt - 592.0) < 0.5,
             qPrintable(QString::number(r[0].bottomPt)));
}

// FlateDecode 流：同一路径，压缩后底沿不变
void TestPdfPageGeometry::testFlateStream()
{
    const QByteArray content = "q 1 0 0 1 100 200 cm 0 0 300 45 re f Q";
    mz_ulong cap = mz_compressBound(content.size());
    QByteArray comp(int(cap), '\0');
    mz_ulong clen = cap;
    QCOMPARE(mz_compress(reinterpret_cast<mz_uint8 *>(comp.data()), &clen,
                         reinterpret_cast<const mz_uint8 *>(content.constData()),
                         mz_ulong(content.size())),
             MZ_OK);
    comp.resize(int(clen));
    const QByteArray page3 = QByteArray(
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Contents 4 0 R /Resources << >> >>");
    QByteArray pdf("%PDF-1.4\n");
    pdf += "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n";
    pdf += "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n";
    pdf += "3 0 obj " + page3 + " endobj\n";
    pdf += "4 0 obj << /Length " + QByteArray::number(comp.size())
        + " /Filter /FlateDecode >>\nstream\n" + comp + "\nendstream endobj\n";
    pdf += "trailer << /Root 1 0 R >>\n%%EOF";
    const auto r = measureOf(pdf);
    QCOMPARE(r.size(), 1);
    QVERIFY(r[0].valid);
    QVERIFY2(qAbs(r[0].bottomPt - 592.0) < 0.5,
             qPrintable(QString::number(r[0].bottomPt)));
}

// 图片 Do：cm 缩放平移把单位方映到 (10,60)-(60,80) → 底 = 792−60 = 732
void TestPdfPageGeometry::testImageDoUnitSquare()
{
    const QByteArray content = "q 50 0 0 20 10 60 cm /Im0 Do Q";
    const QByteArray extra =
        "5 0 obj << /Type /XObject /Subtype /Image /Width 1 /Height 1 "
        "/ColorSpace /DeviceGray /BitsPerComponent 8 /Length 1 >>\nstream\n"
        "\\x00\nendstream endobj\n";
    const QByteArray res = "<< /XObject << /Im0 5 0 R >> >>";
    const auto r = measureOf(makePdf(content, extra, res));
    QCOMPARE(r.size(), 1);
    QVERIFY(r[0].valid);
    QVERIFY2(qAbs(r[0].bottomPt - 732.0) < 0.5,
             qPrintable(QString::number(r[0].bottomPt)));
}

// Form XObject：BBox [0 0 10 10] × Matrix 平移(5,5) × 页 cm 平移(0,0) →
// y=5..15 → 底 = 792−5 = 787（Form 是 SVG 矢量内容在 PDF 的常见载体）
void TestPdfPageGeometry::testFormBBoxWithMatrix()
{
    const QByteArray content = "q /Fm0 Do Q";
    const QByteArray extra =
        "5 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 10 10] "
        "/Matrix [1 0 0 1 5 5] /Resources << >> /Length 10 >>\nstream\n"
        "0 0 2 2 re f\nendstream endobj\n";
    const QByteArray res = "<< /XObject << /Fm0 5 0 R >> >>";
    const auto r = measureOf(makePdf(content, extra, res));
    QCOMPARE(r.size(), 1);
    QVERIFY(r[0].valid);
    QVERIFY2(qAbs(r[0].bottomPt - 787.0) < 0.5,
             qPrintable(QString::number(r[0].bottomPt)));
}

// 垃圾输入：非 PDF → 空；解不开的页 → 该页 valid=false（调用方回退文本
// 几何，不劣于无此单元）
void TestPdfPageGeometry::testGarbageReturnsEmptyOrInvalid()
{
    const auto empty = PdfPageGeometry::measure(QByteArray("not a pdf at all"));
    QCOMPARE(empty.size(), 0);
    // 内容流为空串 → 无绘制操作 → 页占位 valid=false
    const auto r = measureOf(makePdf("", "", "<< >>"));
    QCOMPARE(r.size(), 1);
    QVERIFY(!r[0].valid);
}

QTEST_APPLESS_MAIN(TestPdfPageGeometry)
#include "test_pdfpagegeometry.moc"
