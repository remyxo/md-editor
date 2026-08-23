#ifndef MD_EDITOR_STATUSBAR_H
#define MD_EDITOR_STATUSBAR_H

#include <QStatusBar>
#include <QLabel>

class StatusBar : public QStatusBar {
    Q_OBJECT
public:
    explicit StatusBar(QWidget *parent = nullptr);

    // 语言切换后重设全部文案（Task 10 调用）
    void retranslateUi();

public slots:
    void updateCursorPosition(int line, int col, int /*tabId*/);
    void updateFilePath(const QString &path);
    void updateWordCount(int chars, int words, int /*tabId*/);
    void updateEncoding(const QString &encoding);
    void setSourceMode(bool on);

private:
    QLabel *m_cursorLabel;
    QLabel *m_sourceModeLabel;
    QLabel *m_filePathLabel;
    QLabel *m_wordLabel;
    QLabel *m_encodingLabel;
    // 缓存当前值，供 retranslateUi 重设文案
    int m_line = 1;
    int m_col = 1;
    int m_chars = 0;
    int m_words = 0;
    QString m_encoding;
};

#endif // MD_EDITOR_STATUSBAR_H
