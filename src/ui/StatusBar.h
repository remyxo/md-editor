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
    void setReadMode(bool readOnly);

private:
    QLabel *m_cursorLabel;
    QLabel *m_wordLabel;
    QLabel *m_modeLabel;
};

#endif // MD_EDITOR_STATUSBAR_H
