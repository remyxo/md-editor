#include "SearchBar.h"
#include <QHBoxLayout>
#include <QKeyEvent>

SearchBar::SearchBar(QWidget *parent)
    : QWidget(parent)
    , m_findEdit(new QLineEdit(this))
    , m_replaceEdit(new QLineEdit(this))
    , m_caseBtn(new QPushButton("Aa", this))
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_findEdit->setPlaceholderText("查找...");
    m_findEdit->setMaximumWidth(250);
    layout->addWidget(m_findEdit);

    m_replaceEdit->setPlaceholderText("替换为...");
    m_replaceEdit->setMaximumWidth(250);
    layout->addWidget(m_replaceEdit);

    m_caseBtn->setCheckable(true);
    m_caseBtn->setFixedWidth(40);
    m_caseBtn->setToolTip("区分大小写");
    layout->addWidget(m_caseBtn);

    QPushButton *findBtn = new QPushButton("查找", this);
    connect(findBtn, &QPushButton::clicked, this, [this]() {
        emit findRequested(m_findEdit->text(), m_caseBtn->isChecked());
    });
    layout->addWidget(findBtn);

    QPushButton *replaceBtn = new QPushButton("替换", this);
    connect(replaceBtn, &QPushButton::clicked, this, [this]() {
        emit replaceRequested(m_findEdit->text(), m_replaceEdit->text());
    });
    layout->addWidget(replaceBtn);

    QPushButton *replaceAllBtn = new QPushButton("全部替换", this);
    connect(replaceAllBtn, &QPushButton::clicked, this, [this]() {
        emit replaceAllRequested(m_findEdit->text(), m_replaceEdit->text());
    });
    layout->addWidget(replaceAllBtn);

    QPushButton *closeBtn = new QPushButton("✕", this);
    connect(closeBtn, &QPushButton::clicked, this, &SearchBar::closeRequested);
    layout->addWidget(closeBtn);

    layout->addStretch();

    setVisible(false);
}

void SearchBar::focusFindField()
{
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}
