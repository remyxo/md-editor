#ifndef MD_EDITOR_SETTINGSDIALOG_H
#define MD_EDITOR_SETTINGSDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QFontComboBox>
#include <QPlainTextEdit>

class FileManager;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(FileManager *fileManager, QWidget *parent = nullptr);

signals:
    // 用户在外观页确定新的主题模式（system/light/dark）——由 MainWindow 统一应用
    void themeModeChanged(const QString &mode);

private slots:
    void onAccepted();

private:
    void setupUi();
    void loadSettings();

    // 通用
    QCheckBox *m_restoreSessionCheck;

    // 编辑器
    QFontComboBox *m_fontCombo;
    QSpinBox *m_fontSizeSpin;
    QCheckBox *m_lineNumbersCheck;
    QCheckBox *m_wordWrapCheck;
    QSpinBox *m_tabSizeSpin;
    QSpinBox *m_widthSpin;

    // 外观
    QComboBox *m_themeCombo;
    QComboBox *m_langCombo;
    QPlainTextEdit *m_customCssEdit;

    // 导出
    QComboBox *m_paperSizeCombo;
    QPushButton *m_portraitBtn;
    QPushButton *m_landscapeBtn;
    int m_selectedOrientation;
    QDoubleSpinBox *m_marginTop;
    QDoubleSpinBox *m_marginBottom;
    QDoubleSpinBox *m_marginLeft;
    QDoubleSpinBox *m_marginRight;

    FileManager *m_fileManager;
};

#endif // MD_EDITOR_SETTINGSDIALOG_H
