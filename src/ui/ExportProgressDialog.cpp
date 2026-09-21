#include "ExportProgressDialog.h"

#include <QBoxLayout>
#include <QCloseEvent>
#include <QLabel>
#include <QMovie>
#include <QPushButton>
#include <QStyle>

ExportProgressDialog::ExportProgressDialog(QWidget *parent)
    : QDialog(parent)
{
    // 摘除标题栏关闭按钮（Windows 生效；Linux WM 可能仍显示，closeEvent 兜底
    // 拦截）。对话框由成功/错误收尾路径自销毁（WA_DeleteOnClose）
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);
    setWindowTitle(tr("正在导出 PDF"));
    setFixedWidth(460);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(48, 48);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_movie = new QMovie(QStringLiteral(":/icons/export-progress.gif"), QByteArray(), this);
    m_movie->setScaledSize(QSize(48, 48));
    m_iconLabel->setMovie(m_movie);
    m_movie->start();

    m_titleLabel = new QLabel(tr("正在导出 PDF"), this);
    QFont f = m_titleLabel->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    m_titleLabel->setFont(f);

    m_phaseLabel = new QLabel(tr("正在准备…"), this);
    m_phaseLabel->setWordWrap(true);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_errorLabel->hide();

    m_closeBtn = new QPushButton(tr("关闭"), this);
    m_closeBtn->setObjectName(QStringLiteral("closeBtn"));
    m_closeBtn->hide();
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        m_allowClose = true;
        close();
    });

    auto *textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(6);
    textLayout->addWidget(m_titleLabel);
    textLayout->addWidget(m_phaseLabel);
    textLayout->addWidget(m_errorLabel);

    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(24, 20, 24, 20);
    rootLayout->setSpacing(16);
    rootLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);
    rootLayout->addLayout(textLayout, 1);
    rootLayout->addWidget(m_closeBtn, 0, Qt::AlignBottom);
}

void ExportProgressDialog::showPagedProgress(int pages)
{
    if (m_allowClose)
        return;
    m_phaseLabel->setText(tr("正在排版分页 · 已生成 %1 页").arg(pages));
}

void ExportProgressDialog::showWriting(int total)
{
    if (m_allowClose)
        return;
    m_phaseLabel->setText(total > 0
                              ? tr("共 %1 页 · 正在写出 PDF…").arg(total)
                              : tr("正在写出 PDF…"));
}

void ExportProgressDialog::showError(const QString &reason)
{
    if (m_allowClose)
        return;
    m_allowClose = true;   // 错误态允许关闭（按钮/标题栏/Esc 三径）
    m_movie->stop();
    m_iconLabel->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxCritical)
                               .pixmap(48, 48));
    m_titleLabel->setText(tr("导出失败"));
    m_phaseLabel->setText(tr("PDF 未生成。"));
    m_errorLabel->setText(reason);
    m_errorLabel->show();
    m_closeBtn->show();
    m_closeBtn->setDefault(true);
    m_closeBtn->setFocus();
}

void ExportProgressDialog::showFinished()
{
    // 成功自动关闭：用户无须确认（与错误态的差异所在）
    m_allowClose = true;
    close();
}

void ExportProgressDialog::reject()
{
    // 导出期间 Esc 无效（导出不可取消）；错误态等价点「关闭」
    if (!m_allowClose)
        return;
    QDialog::reject();
}

void ExportProgressDialog::closeEvent(QCloseEvent *event)
{
    if (!m_allowClose) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}
