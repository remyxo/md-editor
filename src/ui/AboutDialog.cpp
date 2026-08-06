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
    setFixedSize(420, 240);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 左侧：图标
    QLabel *iconLabel = new QLabel();
    iconLabel->setPixmap(QPixmap(":/icons/md-editor.svg").scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setFixedSize(80, 80);
    iconLabel->setAlignment(Qt::AlignTop);
    mainLayout->addWidget(iconLabel);

    // 右侧：文字信息
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(6);

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
