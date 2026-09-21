#ifndef MD_EDITOR_EXPORTPROGRESSDIALOG_H
#define MD_EDITOR_EXPORTPROGRESSDIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QMovie;

// PDF 导出进度对话框（2026-09-21）：paged.js 大文档导出以十秒计、nativeHf 也有
// 秒级两遍打印，全程静默会让用户误以为卡死而随手关闭程序——半成品文件 +
// 隐藏渲染视图随窗口销毁。模态显示导出进行中，导出完成前不可关闭。
//
// 生命周期约定（与 PdfExporter 信号一一对应）：
// - pagedProgress(k)  → showPagedProgress(k)   分页阶段（页数增长）
// - writingPdf(total) → showWriting(total)     写盘阶段（total=0 表示页数未知）
// - exportFinished(true) → showFinished()      成功：自动关闭
// - exportError/exportFinished(false) → showError(原因)
//   失败不自动关闭：转错误态（停动画、显示原因、出现「关闭」按钮），用户
//   确认后才消失——静默自动关闭会让用户不知道导出失败
//
// 生命周期短（单次导出），不做语言热切换的 retranslateUi（同 AboutDialog 的
// 常驻对话框才需要）
class ExportProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportProgressDialog(QWidget *parent = nullptr);

    // 分页进行中：paged.js 逐页 DOM 重组，页数增长时调用
    void showPagedProgress(int pages);
    // 进入写盘阶段：total>0 显示「共 N 页 · 正在写出 PDF」，0 表示页数未知
    // （Chromium printToPdf 是一次性黑盒，无逐页写出进度可言）
    void showWriting(int total);
    // 失败：转错误态（不自动关闭）
    void showError(const QString &reason);
    // 成功：自动关闭
    void showFinished();

protected:
    // Esc 触发 reject()：导出期间导出不可取消、不响应；错误态等价点「关闭」
    void reject() override;
    // 标题栏关闭按钮（Linux WM 未必可摘除）：仅错误态/完成后放行
    void closeEvent(QCloseEvent *event) override;

private:
    bool m_allowClose = false;   // 放行关闭（错误态确认/成功收尾）
    QLabel *m_iconLabel;
    QMovie *m_movie;
    QLabel *m_titleLabel;
    QLabel *m_phaseLabel;
    QLabel *m_errorLabel;
    QPushButton *m_closeBtn;
};

#endif // MD_EDITOR_EXPORTPROGRESSDIALOG_H
