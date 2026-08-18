#ifndef MD_EDITOR_ABOUTDIALOG_H
#define MD_EDITOR_ABOUTDIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);

private:
    void retranslateUi();

    QLabel *m_nameLabel;
    QLabel *m_versionLabel;
    QLabel *m_descLabel;
    QLabel *m_copyLabel;
    QPushButton *m_closeBtn;
};

#endif
