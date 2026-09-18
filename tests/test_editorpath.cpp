#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "ui/FilePathMatch.h"

// EditorPath::sameFilePath——TabWidget「同路径文件重开即聚焦」的核心谓词：
// canonicalFilePath 相等判定（相对路径/符号链接/冗余分隔符都归一）；
// 双侧文件均不存在时回退绝对路径字符串比较——防两个不同的不存在路径
// 都 canonical 成空串而误判相等（本谓词的关键边界）。
class TestEditorPath : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void sameFileDifferentSpellings();
    void differentFiles();
    void nonexistentPaths();

private:
    QTemporaryDir *m_dir = nullptr;
};

void TestEditorPath::initTestCase()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
}

void TestEditorPath::cleanupTestCase()
{
    delete m_dir;
}

void TestEditorPath::sameFileDifferentSpellings()
{
    const QString a = m_dir->filePath("doc.md");
    {
        QFile f(a);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    // 相对路径 / ./ 前缀 / 冗余分隔符 / .. 回溯——同一文件的多种写法
    QDir::setCurrent(m_dir->path());
    QVERIFY(FilePathMatch::sameFilePath(a, "doc.md"));
    QVERIFY(FilePathMatch::sameFilePath(a, "./doc.md"));
    QVERIFY(FilePathMatch::sameFilePath(a, m_dir->path() + "//doc.md"));
    QVERIFY(FilePathMatch::sameFilePath(a, m_dir->filePath("sub/../doc.md")));
    QDir::setCurrent(QDir::tempPath());   // 还原 cwd，不污染其他用例

    // 符号链接与真实路径指向同一文件
    const QString link = m_dir->filePath("link.md");
    QVERIFY(QFile::link(a, link));
    QVERIFY(FilePathMatch::sameFilePath(a, link));
}

void TestEditorPath::differentFiles()
{
    const QString a = m_dir->filePath("a.md");
    const QString b = m_dir->filePath("b.md");
    for (const QString &p : {a, b}) {
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    QVERIFY(!FilePathMatch::sameFilePath(a, b));

    // 同名不同目录
    QVERIFY(QDir(m_dir->path()).mkpath("sub"));
    const QString c = m_dir->filePath("sub/b.md");
    {
        QFile f(c);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    QVERIFY(!FilePathMatch::sameFilePath(b, c));
}

void TestEditorPath::nonexistentPaths()
{
    // 双不存在：同串 → 相等；异串 → 不相等（canonical 双空不得误判相等）
    const QString a = m_dir->filePath("ghost.md");
    const QString b = m_dir->filePath("other-ghost.md");
    QVERIFY(FilePathMatch::sameFilePath(a, a));
    QVERIFY(!FilePathMatch::sameFilePath(a, b));
    // 字面不同但指同一路径的写法（./ 由 absoluteFilePath 归一）
    QVERIFY(FilePathMatch::sameFilePath(a, m_dir->filePath("./ghost.md")));
}

QTEST_APPLESS_MAIN(TestEditorPath)
#include "test_editorpath.moc"
