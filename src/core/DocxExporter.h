#ifndef MD_EDITOR_DOCXEXPORTER_H
#define MD_EDITOR_DOCXEXPORTER_H

#include <QObject>

class DocxExporter : public QObject {
    Q_OBJECT
public:
    explicit DocxExporter(QObject *parent = nullptr);

    void exportToDocx(const QString &html, const QString &docDir, const QString &outputPath);

signals:
    void exportFinished(bool success, const QString &outputPath);
    void exportError(const QString &error);

private:
    bool writeDocx(const QString &html, const QString &docDir, const QString &outputPath);
};

#endif // MD_EDITOR_DOCXEXPORTER_H
