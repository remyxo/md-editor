#include "SettingsDialog.h"
#include "../core/FileManager.h"
#include "../app/Application.h"
#include "../app/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QPageSize>
#include <QPageLayout>

SettingsDialog::SettingsDialog(FileManager *fileManager, QWidget *parent)
    : QDialog(parent)
    , m_fileManager(fileManager)
{
    setWindowTitle("设置");
    setMinimumWidth(500);
    setupUi();
    loadSettings();
}

void SettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QTabWidget *tabs = new QTabWidget(this);

    // === 通用 ===
    QWidget *generalTab = new QWidget();
    QVBoxLayout *generalLayout = new QVBoxLayout(generalTab);

    QGroupBox *autoSaveGroup = new QGroupBox("自动保存");
    QFormLayout *autoSaveForm = new QFormLayout();
    m_autoSaveCheck = new QCheckBox("启用自动保存");
    autoSaveForm->addRow(m_autoSaveCheck);
    m_autoSaveIntervalSpin = new QSpinBox();
    m_autoSaveIntervalSpin->setRange(10, 300);
    m_autoSaveIntervalSpin->setSuffix(" 秒");
    autoSaveForm->addRow("保存间隔:", m_autoSaveIntervalSpin);
    autoSaveGroup->setLayout(autoSaveForm);
    generalLayout->addWidget(autoSaveGroup);

    QGroupBox *startupGroup = new QGroupBox("启动");
    QVBoxLayout *startupLayout = new QVBoxLayout();
    m_restoreSessionCheck = new QCheckBox("启动时恢复上次会话");
    startupLayout->addWidget(m_restoreSessionCheck);
    startupGroup->setLayout(startupLayout);
    generalLayout->addWidget(startupGroup);

    generalLayout->addStretch();
    tabs->addTab(generalTab, "通用");

    // === 编辑器 ===
    QWidget *editorTab = new QWidget();
    QFormLayout *editorForm = new QFormLayout(editorTab);
    m_fontCombo = new QFontComboBox();
    editorForm->addRow("字体:", m_fontCombo);
    m_fontSizeSpin = new QSpinBox();
    m_fontSizeSpin->setRange(10, 32);
    m_fontSizeSpin->setSuffix(" px");
    editorForm->addRow("字号:", m_fontSizeSpin);
    m_lineNumbersCheck = new QCheckBox();
    m_lineNumbersCheck->setChecked(true);
    editorForm->addRow("显示行号:", m_lineNumbersCheck);
    m_wordWrapCheck = new QCheckBox();
    m_wordWrapCheck->setChecked(true);
    editorForm->addRow("自动换行:", m_wordWrapCheck);
    m_tabSizeSpin = new QSpinBox();
    m_tabSizeSpin->setRange(2, 8);
    m_tabSizeSpin->setSuffix(" 空格");
    editorForm->addRow("Tab 宽度:", m_tabSizeSpin);
    tabs->addTab(editorTab, "编辑器");

    // === 外观 ===
    QWidget *appearanceTab = new QWidget();
    QFormLayout *appearanceForm = new QFormLayout(appearanceTab);
    m_themeCombo = new QComboBox();
    m_themeCombo->addItem("亮色", "light");
    m_themeCombo->addItem("暗色", "dark");
    appearanceForm->addRow("主题:", m_themeCombo);
    m_langCombo = new QComboBox();
    m_langCombo->addItem("简体中文", "zh_CN");
    m_langCombo->addItem("English", "en_US");
    appearanceForm->addRow("界面语言:", m_langCombo);
    tabs->addTab(appearanceTab, "外观");

    // === 导出 ===
    QWidget *exportTab = new QWidget();
    QFormLayout *exportForm = new QFormLayout(exportTab);
    m_paperSizeCombo = new QComboBox();
    m_paperSizeCombo->addItem("A4", QPageSize::A4);
    m_paperSizeCombo->addItem("Letter", QPageSize::Letter);
    exportForm->addRow("默认纸张:", m_paperSizeCombo);
    m_orientationCombo = new QComboBox();
    m_orientationCombo->addItem("纵向", QPageLayout::Portrait);
    m_orientationCombo->addItem("横向", QPageLayout::Landscape);
    exportForm->addRow("默认方向:", m_orientationCombo);
    tabs->addTab(exportTab, "导出");

    mainLayout->addWidget(tabs);

    // 按钮
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

void SettingsDialog::loadSettings()
{
    // 从 FileManager 和 QSettings 加载
    m_autoSaveCheck->setChecked(m_fileManager->isAutoSaveEnabled());
    m_autoSaveIntervalSpin->setValue(m_fileManager->autoSaveInterval());

    QSettings settings("md-editor", "md-editor");
    m_restoreSessionCheck->setChecked(
        settings.value("session/restore", true).toBool());
    m_fontCombo->setCurrentText(
        settings.value("editor/font", "Sans Serif").toString());
    m_fontSizeSpin->setValue(settings.value("editor/fontSize", 18).toInt());
    m_lineNumbersCheck->setChecked(
        settings.value("editor/lineNumbers", true).toBool());
    m_wordWrapCheck->setChecked(
        settings.value("editor/wordWrap", true).toBool());
    m_tabSizeSpin->setValue(settings.value("editor/tabSize", 4).toInt());

    QString theme = Application::instance()->themeManager()->currentTheme();
    m_themeCombo->setCurrentIndex(m_themeCombo->findData(theme));
}

void SettingsDialog::onAccepted()
{
    // 保存设置
    m_fileManager->setAutoSaveEnabled(m_autoSaveCheck->isChecked());
    m_fileManager->setAutoSaveInterval(m_autoSaveIntervalSpin->value());
    m_fileManager->saveSettings();

    QSettings settings("md-editor", "md-editor");
    settings.setValue("session/restore", m_restoreSessionCheck->isChecked());
    settings.setValue("editor/font", m_fontCombo->currentText());
    settings.setValue("editor/fontSize", m_fontSizeSpin->value());
    settings.setValue("editor/lineNumbers", m_lineNumbersCheck->isChecked());
    settings.setValue("editor/wordWrap", m_wordWrapCheck->isChecked());
    settings.setValue("editor/tabSize", m_tabSizeSpin->value());
    settings.sync();

    // 应用主题并持久化
    QString theme = m_themeCombo->currentData().toString();
    Application::instance()->themeManager()->switchTheme(theme);
    settings.setValue("appearance/theme", theme);

    accept();
}
