#ifndef MD_EDITOR_SETTINGSDIALOG_H
#define MD_EDITOR_SETTINGSDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QFontComboBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QGroupBox>
#include <QTabWidget>

class FileManager;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(FileManager *fileManager, QWidget *parent = nullptr);

    // 语言切换后由 MainWindow 调用，重设全部界面文本
    void retranslateUi();

signals:
    // 用户在外观页确定新的主题模式（system/light/dark）——由 MainWindow 统一应用
    void themeModeChanged(const QString &mode);
    // 用户在外观页确定新的语言（zh_CN/en_US）——由 MainWindow 统一切换 + 持久化
    void languageChanged(const QString &lang);

private slots:
    void onAccepted();

private:
    void setupUi();
    void loadSettings();

    // 通用
    QTabWidget *m_tabs;
    QGroupBox *m_startupGroup;
    QCheckBox *m_restoreSessionCheck;

    // 编辑器
    QFontComboBox *m_fontCombo;
    QSpinBox *m_fontSizeSpin;
    QCheckBox *m_lineNumbersCheck;
    QCheckBox *m_wordWrapCheck;
    QSpinBox *m_tabSizeSpin;
    QSpinBox *m_widthSpin;
    QLabel *m_fontLabel;
    QLabel *m_fontSizeLabel;
    QLabel *m_lineNumbersLabel;
    QLabel *m_wordWrapLabel;
    QLabel *m_tabSizeLabel;
    QLabel *m_widthLabel;

    // 外观
    QComboBox *m_themeCombo;
    QComboBox *m_langCombo;
    QPlainTextEdit *m_customCssEdit;
    QLabel *m_themeLabel;
    QLabel *m_langLabel;
    QLabel *m_customCssLabel;

    // 导出
    QComboBox *m_paperSizeCombo;
    QPushButton *m_portraitBtn;
    QPushButton *m_landscapeBtn;
    int m_selectedOrientation;
    QDoubleSpinBox *m_marginTop;
    QDoubleSpinBox *m_marginBottom;
    QDoubleSpinBox *m_marginLeft;
    QDoubleSpinBox *m_marginRight;
    QCheckBox *m_pdfHeaderCheck = nullptr;
    QCheckBox *m_pdfFooterCheck = nullptr;
    QLabel *m_paperLabel;
    QGroupBox *m_marginGroup;
    QLabel *m_marginTopLbl;
    QLabel *m_marginBottomLbl;
    QLabel *m_marginLeftLbl;
    QLabel *m_marginRightLbl;
    QLabel *m_cmTopLbl;
    QLabel *m_cmBottomLbl;
    QLabel *m_cmLeftLbl;
    QLabel *m_cmRightLbl;
    QGroupBox *m_orientGroup;
    QLabel *m_portraitLbl;
    QLabel *m_landscapeLbl;

    FileManager *m_fileManager;
};

#endif // MD_EDITOR_SETTINGSDIALOG_H
