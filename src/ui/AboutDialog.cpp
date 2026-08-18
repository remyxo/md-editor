#include "AboutDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setFixedSize(400, 210);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 16, 20, 16);
    mainLayout->setSpacing(14);

    // 左侧：图标
    QLabel *iconLabel = new QLabel();
    iconLabel->setPixmap(QPixmap(":/icons/md-editor.svg").scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setFixedSize(64, 64);
    mainLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    // 右侧：文字信息
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(4);

    m_nameLabel = new QLabel("Markdown Editor");
    QFont nameFont = m_nameLabel->font();
    nameFont.setPointSize(16);
    nameFont.setBold(true);
    m_nameLabel->setFont(nameFont);
    rightLayout->addWidget(m_nameLabel);

    m_versionLabel = new QLabel();
    m_versionLabel->setStyleSheet("color: #666; font-size: 13px;");
    rightLayout->addWidget(m_versionLabel);

    rightLayout->addSpacing(20);

    m_descLabel = new QLabel();
    m_descLabel->setWordWrap(true);
    m_descLabel->setStyleSheet("color: #888; font-size: 13px;");
    rightLayout->addWidget(m_descLabel);

    m_copyLabel = new QLabel("© 2026");
    m_copyLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    rightLayout->addWidget(m_copyLabel);

    rightLayout->addStretch();

    m_closeBtn = new QPushButton();
    m_closeBtn->setFixedWidth(80);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    rightLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(rightLayout);

    retranslateUi();
}

void AboutDialog::retranslateUi()
{
    setWindowTitle(tr("关于 Markdown Editor"));
    // 应用名 Markdown Editor 为品牌名，不翻译
    m_versionLabel->setText(tr("版本 %1").arg(MD_EDITOR_VERSION));
    m_descLabel->setText(tr("一款基于 Qt6 + TipTap 的 Markdown 所见即所得编辑器"));
    m_closeBtn->setText(tr("确定"));
}
