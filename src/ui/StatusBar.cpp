#include "StatusBar.h"

StatusBar::StatusBar(QWidget *parent)
    : QStatusBar(parent)
{
    m_cursorLabel = new QLabel("行 1, 列 1", this);
    m_filePathLabel = new QLabel("", this);
    m_wordLabel = new QLabel("字符: 0 / 词: 0", this);
    m_modeLabel = new QLabel("编辑模式", this);

    addWidget(m_cursorLabel);
    addWidget(m_filePathLabel, 1);
    addWidget(m_modeLabel);
    addPermanentWidget(m_wordLabel);
}

void StatusBar::updateCursorPosition(int line, int col, int /*tabId*/)
{
    m_cursorLabel->setText(QString("行 %1, 列 %2").arg(line).arg(col));
}

void StatusBar::updateFilePath(const QString &path)
{
    m_filePathLabel->setText(path.isEmpty() ? QString() : path);
}

void StatusBar::updateWordCount(int chars, int words, int /*tabId*/)
{
    m_wordLabel->setText(QString("字符: %1 / 词: %2").arg(chars).arg(words));
}

void StatusBar::setReadMode(bool readOnly)
{
    m_modeLabel->setText(readOnly ? "阅读模式" : "编辑模式");
}
