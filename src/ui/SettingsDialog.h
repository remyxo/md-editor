#ifndef MD_EDITOR_SETTINGSDIALOG_H
#define MD_EDITOR_SETTINGSDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QFontComboBox>

class FileManager;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(FileManager *fileManager, QWidget *parent = nullptr);

private slots:
    void onAccepted();

private:
    void setupUi();
    void loadSettings();

    // 通用
    QCheckBox *m_autoSaveCheck;
    QSpinBox *m_autoSaveIntervalSpin;
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

    // 导出
    QComboBox *m_paperSizeCombo;
    QComboBox *m_orientationCombo;

    FileManager *m_fileManager;
};

#endif // MD_EDITOR_SETTINGSDIALOG_H
