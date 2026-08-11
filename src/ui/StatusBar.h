#ifndef MD_EDITOR_STATUSBAR_H
#define MD_EDITOR_STATUSBAR_H

#include <QStatusBar>
#include <QLabel>

class StatusBar : public QStatusBar {
    Q_OBJECT
public:
    explicit StatusBar(QWidget *parent = nullptr);

public slots:
    void updateCursorPosition(int line, int col, int /*tabId*/);
    void updateFilePath(const QString &path);
    void updateWordCount(int chars, int words, int /*tabId*/);

private:
    QLabel *m_cursorLabel;
    QLabel *m_filePathLabel;
    QLabel *m_wordLabel;
};

#endif // MD_EDITOR_STATUSBAR_H
