#include "StatusBar.h"

StatusBar::StatusBar(QWidget *parent)
    : QStatusBar(parent)
{
    m_cursorLabel = new QLabel(this);
    m_sourceModeLabel = new QLabel(this);
    m_filePathLabel = new QLabel("", this);
    m_wordLabel = new QLabel(this);
    m_encodingLabel = new QLabel(this);

    m_sourceModeLabel->setVisible(false);

    addWidget(m_cursorLabel);
    addWidget(m_sourceModeLabel);
    addWidget(m_filePathLabel, 1);
    addPermanentWidget(m_encodingLabel);
    addPermanentWidget(m_wordLabel);

    retranslateUi();  // 统一重设文案（首次设置，语言切换后 MainWindow 再次调用）
}

// 语言切换后按缓存值重设全部文案
void StatusBar::retranslateUi()
{
    m_cursorLabel->setText(tr("行 %1, 列 %2").arg(m_line).arg(m_col));
    m_sourceModeLabel->setText(tr("源码模式"));
    m_encodingLabel->setText(m_encoding.isEmpty() ? QString()
                                                  : tr("编码: %1").arg(m_encoding));
    m_wordLabel->setText(tr("字符: %1 / 词: %2").arg(m_chars).arg(m_words));
}

void StatusBar::updateCursorPosition(int line, int col, int /*tabId*/)
{
    m_line = line;
    m_col = col;
    m_cursorLabel->setText(tr("行 %1, 列 %2").arg(line).arg(col));
}

void StatusBar::updateFilePath(const QString &path)
{
    m_filePathLabel->setText(path.isEmpty() ? QString() : path);
}

void StatusBar::updateWordCount(int chars, int words, int /*tabId*/)
{
    m_chars = chars;
    m_words = words;
    m_wordLabel->setText(tr("字符: %1 / 词: %2").arg(chars).arg(words));
}

void StatusBar::updateEncoding(const QString &encoding)
{
    m_encoding = encoding;
    m_encodingLabel->setText(encoding.isEmpty() ? QString()
                                                : tr("编码: %1").arg(encoding));
}

void StatusBar::setSourceMode(bool on)
{
    m_sourceModeLabel->setVisible(on);
}

