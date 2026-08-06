#include "StatusBar.h"

StatusBar::StatusBar(QWidget *parent)
    : QStatusBar(parent)
{
    m_cursorLabel = new QLabel("行 1, 列 1", this);
    m_fileLabel = new QLabel("", this);
    m_wordLabel = new QLabel("字数: 0 / 字符: 0", this);

    addWidget(m_cursorLabel, 1);
    addPermanentWidget(m_wordLabel);
    addPermanentWidget(m_fileLabel);
}

void StatusBar::updateCursorPosition(int line, int col, int /*tabId*/)
{
    m_cursorLabel->setText(QString("行 %1, 列 %2").arg(line).arg(col));
}

void StatusBar::updateFilePath(const QString &path)
{
    m_fileLabel->setText(path.isEmpty() ? "" : path);
}

void StatusBar::updateWordCount(int chars, int words, int /*tabId*/)
{
    m_wordLabel->setText(QString("字符: %1 / 词: %2").arg(chars).arg(words));
}
