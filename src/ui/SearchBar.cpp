#include "SearchBar.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QKeyEvent>

SearchBar::SearchBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("searchBar");

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, &SearchBar::emitParamsNow);

    // 第一行：查找 + toggles + 导航 + 计数 + 关闭
    auto *row1 = new QHBoxLayout;
    row1->setContentsMargins(4, 2, 4, 2);

    m_findEdit = new QLineEdit(this);
    m_findEdit->setPlaceholderText("查找...");
    m_findEdit->setMaximumWidth(280);
    row1->addWidget(m_findEdit);

    auto mkToggle = [this, &row1](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text, this);
        b->setCheckable(true);
        b->setFixedWidth(40);
        b->setToolTip(tip);
        connect(b, &QPushButton::toggled, this, &SearchBar::scheduleEmitParams);
        row1->addWidget(b);
        return b;
    };
    m_caseBtn = mkToggle("Aa", "区分大小写");
    m_regexBtn = mkToggle(".*", "正则表达式");
    m_wordBtn = mkToggle("W|", "全词匹配");

    m_prevBtn = new QPushButton("↑", this);
    m_nextBtn = new QPushButton("↓", this);
    m_prevBtn->setToolTip("上一个 (Shift+F3)");
    m_nextBtn->setToolTip("下一个 (F3)");
    connect(m_nextBtn, &QPushButton::clicked, this, &SearchBar::findNextRequested);
    connect(m_prevBtn, &QPushButton::clicked, this, &SearchBar::findPrevRequested);
    row1->addWidget(m_prevBtn);
    row1->addWidget(m_nextBtn);

    m_countLabel = new QLabel("0/0", this);
    m_countLabel->setFixedWidth(60);
    m_countLabel->setAlignment(Qt::AlignCenter);
    row1->addWidget(m_countLabel);

    m_closeBtn = new QPushButton("✕", this);
    m_closeBtn->setFixedWidth(32);
    m_closeBtn->setToolTip("关闭 (Esc)");
    connect(m_closeBtn, &QPushButton::clicked, this, &SearchBar::closeRequested);
    row1->addWidget(m_closeBtn);

    row1->addStretch();

    // 第二行：替换（默认隐藏）
    m_replaceRow = new QWidget(this);
    auto *row2 = new QHBoxLayout(m_replaceRow);
    row2->setContentsMargins(4, 2, 4, 2);
    m_replaceEdit = new QLineEdit(this);
    m_replaceEdit->setPlaceholderText("替换为...");
    m_replaceEdit->setMaximumWidth(280);
    row2->addWidget(m_replaceEdit);
    m_replaceBtn = new QPushButton("替换", this);
    m_replaceAllBtn = new QPushButton("全部替换", this);
    connect(m_replaceBtn, &QPushButton::clicked, this, [this]() {
        emit replaceRequested(m_replaceEdit->text());
    });
    connect(m_replaceAllBtn, &QPushButton::clicked, this, [this]() {
        emit replaceAllRequested(m_replaceEdit->text());
    });
    row2->addWidget(m_replaceBtn);
    row2->addWidget(m_replaceAllBtn);
    row2->addStretch();
    m_replaceRow->setVisible(false);

    connect(m_findEdit, &QLineEdit::textChanged, this, &SearchBar::scheduleEmitParams);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addLayout(row1);
    outer->addWidget(m_replaceRow);

    setVisible(false);
}

void SearchBar::scheduleEmitParams() { m_debounce->start(); }

void SearchBar::emitParamsNow()
{
    emit searchParamsChanged(m_findEdit->text(), m_caseBtn->isChecked(),
                             m_regexBtn->isChecked(), m_wordBtn->isChecked());
}

void SearchBar::focusFindField()
{
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void SearchBar::setCount(int total, int current)
{
    m_countLabel->setText(QStringLiteral("%1/%2").arg(total > 0 ? current : 0).arg(total));
    const bool has = total > 0;
    m_nextBtn->setEnabled(has);
    m_prevBtn->setEnabled(has);
    m_replaceBtn->setEnabled(has);
    m_replaceAllBtn->setEnabled(has);
}

void SearchBar::setReplaceMode(bool on)
{
    m_replaceRow->setVisible(on);
    emit replaceModeToggleRequested(on);
}

void SearchBar::keyPressEvent(QKeyEvent *event)
{
    // 查找框：Enter 下一个 / Shift+Enter 上一个；替换框：Enter 替换
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_replaceEdit->hasFocus()) {
            emit replaceRequested(m_replaceEdit->text());
        } else {
            if (event->modifiers() & Qt::ShiftModifier) emit findPrevRequested();
            else emit findNextRequested();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
