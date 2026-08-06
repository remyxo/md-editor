#include "AboutDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("关于 Markdown Editor");
    setFixedSize(400, 210);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(12, 16, 20, 16);
    mainLayout->setSpacing(14);

    // 左侧：图标
    QLabel *iconLabel = new QLabel();
    iconLabel->setPixmap(QPixmap(":/icons/md-editor.svg").scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setFixedSize(64, 64);
    mainLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    // 右侧：文字信息
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(4);

    QLabel *nameLabel = new QLabel("Markdown Editor");
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(16);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    rightLayout->addWidget(nameLabel);

    QLabel *versionLabel = new QLabel(
        QString("版本 %1").arg(MD_EDITOR_VERSION));
    versionLabel->setStyleSheet("color: #666; font-size: 13px;");
    rightLayout->addWidget(versionLabel);

    rightLayout->addSpacing(16);

    QLabel *descLabel = new QLabel(
        "一款基于 Qt6 + TipTap 的 Markdown 所见即所得编辑器");
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: #888; font-size: 13px;");
    rightLayout->addWidget(descLabel);

    QLabel *copyLabel = new QLabel("© 2026");
    copyLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    rightLayout->addWidget(copyLabel);

    rightLayout->addStretch();

    QPushButton *closeBtn = new QPushButton("确定");
    closeBtn->setFixedWidth(80);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    rightLayout->addWidget(closeBtn);

    mainLayout->addLayout(rightLayout);
}
