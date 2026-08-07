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
#include <QDoubleSpinBox>

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

    m_widthSpin = new QSpinBox();
    m_widthSpin->setRange(50, 100);
    m_widthSpin->setSingleStep(5);
    m_widthSpin->setSuffix("%");
    m_widthSpin->setValue(80);
    editorForm->addRow("编辑区宽度:", m_widthSpin);
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

    exportForm->addRow("", new QLabel("页边距 (厘米):"));
    m_marginTop = new QDoubleSpinBox();
    m_marginTop->setRange(0.5, 5.0); m_marginTop->setSingleStep(0.1);
    m_marginTop->setSuffix(" cm");
    exportForm->addRow("上边距:", m_marginTop);
    m_marginBottom = new QDoubleSpinBox();
    m_marginBottom->setRange(0.5, 5.0); m_marginBottom->setSingleStep(0.1);
    m_marginBottom->setSuffix(" cm");
    exportForm->addRow("下边距:", m_marginBottom);
    m_marginLeft = new QDoubleSpinBox();
    m_marginLeft->setRange(0.5, 5.0); m_marginLeft->setSingleStep(0.1);
    m_marginLeft->setSuffix(" cm");
    exportForm->addRow("左边距:", m_marginLeft);
    m_marginRight = new QDoubleSpinBox();
    m_marginRight->setRange(0.5, 5.0); m_marginRight->setSingleStep(0.1);
    m_marginRight->setSuffix(" cm");
    exportForm->addRow("右边距:", m_marginRight);
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
    m_widthSpin->setValue(settings.value("editor/width", 80).toInt());

    // 导出
    int paperIdx = m_paperSizeCombo->findData(
        settings.value("export/paperSize", QPageSize::A4).toInt());
    if (paperIdx >= 0) m_paperSizeCombo->setCurrentIndex(paperIdx);
    int orientIdx = m_orientationCombo->findData(
        settings.value("export/orientation", QPageLayout::Portrait).toInt());
    if (orientIdx >= 0) m_orientationCombo->setCurrentIndex(orientIdx);
    m_marginTop->setValue(settings.value("export/marginTop", 2.0).toDouble());
    m_marginBottom->setValue(settings.value("export/marginBottom", 2.0).toDouble());
    m_marginLeft->setValue(settings.value("export/marginLeft", 2.0).toDouble());
    m_marginRight->setValue(settings.value("export/marginRight", 2.0).toDouble());

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
    settings.setValue("editor/width", m_widthSpin->value());
    settings.sync();

    // 应用主题并持久化
    QString theme = m_themeCombo->currentData().toString();
    Application::instance()->themeManager()->switchTheme(theme);
    // 导出
    settings.setValue("export/paperSize", m_paperSizeCombo->currentData().toInt());
    settings.setValue("export/orientation", m_orientationCombo->currentData().toInt());
    settings.setValue("export/marginTop", m_marginTop->value());
    settings.setValue("export/marginBottom", m_marginBottom->value());
    settings.setValue("export/marginLeft", m_marginLeft->value());
    settings.setValue("export/marginRight", m_marginRight->value());

    settings.setValue("appearance/theme", theme);

    accept();
}
