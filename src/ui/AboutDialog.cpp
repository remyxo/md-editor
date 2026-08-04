#include "AboutDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("关于 Markdown Editor");
    setFixedSize(400, 260);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(32, 24, 32, 24);

    // 应用名称
    QLabel *nameLabel = new QLabel("Markdown Editor");
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(18);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    nameLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(nameLabel);

    // 版本号
    QLabel *versionLabel = new QLabel(
        QString("版本 %1").arg(MD_EDITOR_VERSION));
    QFont versionFont = versionLabel->font();
    versionFont.setPointSize(11);
    versionLabel->setFont(versionFont);
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setStyleSheet("color: #666;");
    layout->addWidget(versionLabel);

    // 描述
    QLabel *descLabel = new QLabel(
        "一款基于 Qt6 + TipTap 的\nMarkdown 所见即所得编辑器");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: #888; font-size: 13px;");
    layout->addWidget(descLabel);

    layout->addStretch();

    // 版权
    QLabel *copyLabel = new QLabel("© 2026 Markdown Editor");
    copyLabel->setAlignment(Qt::AlignCenter);
    copyLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    layout->addWidget(copyLabel);

    // 关闭按钮
    QPushButton *closeBtn = new QPushButton("确定");
    closeBtn->setFixedWidth(100);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);
}
