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
    setFixedSize(400, 320);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(32, 24, 32, 24);

    // 图标 + 名称版本同行
    QHBoxLayout *headerLayout = new QHBoxLayout();
    QLabel *iconLabel = new QLabel();
    iconLabel->setPixmap(QPixmap(":/icons/md-editor.svg").scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setFixedSize(56, 56);
    headerLayout->addWidget(iconLabel);

    QVBoxLayout *titleLayout = new QVBoxLayout();
    QLabel *nameLabel = new QLabel("Markdown Editor");
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(18);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    titleLayout->addWidget(nameLabel);

    QLabel *versionLabel = new QLabel(
        QString("版本 %1").arg(MD_EDITOR_VERSION));
    versionLabel->setStyleSheet("color: #666; font-size: 13px;");
    titleLayout->addWidget(versionLabel);

    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();
    layout->addLayout(headerLayout);

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
